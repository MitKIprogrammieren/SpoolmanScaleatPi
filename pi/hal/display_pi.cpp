// Display and touch for the Raspberry Pi build.
//
// The UI keeps rendering at 480x320, as on the WT32-SC01 Plus. Every flushed
// area is copied into a 480x320 shadow buffer and scaled from there onto the
// Linux framebuffer (/dev/fb0), centred. On the BIGTREETECH PI TFT50 (800x480)
// that is a factor of 1.5: 720x480 with a 40 px border left and right.
//
// Touch comes from an evdev device (the panel's capacitive controller shows up
// as /dev/input/eventN). Its coordinates are mapped back through the same
// scale, so LVGL sees 480x320 points.
//
// Environment (all optional):
//   SPOOLMANSCALE_FB=/dev/fb0           framebuffer device
//   SPOOLMANSCALE_FB=file:/tmp/fb.raw   test mode: an 800x480 32-bit image in a file
//   SPOOLMANSCALE_TOUCH=/dev/input/event0  touch device (default: first touch screen found)
//   SPOOLMANSCALE_TOUCH=pipe:/tmp/touch    test mode: "tap X Y" lines (panel pixels) into a FIFO
//   SPOOLMANSCALE_ROTATE=180            turn the picture (and touch) upside down
//   SPOOLMANSCALE_SCALE=nearest         sharp pixel doubling instead of smooth scaling
//   SPOOLMANSCALE_TOUCH_SWAP_XY=1, SPOOLMANSCALE_TOUCH_INVERT_X=1, SPOOLMANSCALE_TOUCH_INVERT_Y=1
//   SPOOLMANSCALE_BACKLIGHT=/sys/class/backlight/<name>  backlight to dim

#include "hardware/display.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <dirent.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <lvgl.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "app_config.h"
#include "ui/touch_feedback.h"

namespace {
constexpr int UI_W = 480;
constexpr int UI_H = 320;

void (*s_touch_activity)() = nullptr;

// ---- framebuffer -------------------------------------------------------------
int s_fb_fd = -1;
uint8_t* s_fb = nullptr;
size_t s_fb_len = 0;
int s_fb_w = UI_W, s_fb_h = UI_H, s_fb_bpp = 16, s_fb_stride = UI_W * 2;
int s_fb_roff = 11, s_fb_goff = 5, s_fb_boff = 0;   // for 32 bpp
bool s_rot180 = false;
bool s_nearest = false;

// Where the UI lands on the panel.
float s_scale = 1.0f;
int s_out_x = 0, s_out_y = 0, s_out_w = UI_W, s_out_h = UI_H;

// The UI as LVGL drew it, so scaling can read neighbours across flush areas.
std::vector<uint16_t> s_shadow(UI_W * UI_H, 0);

// Precomputed source coordinates (16.16 fixed point) for each output column/row.
std::vector<int32_t> s_src_x, s_src_y;

int s_tty_fd = -1;

// ---- backlight ---------------------------------------------------------------
std::string s_bl_path;
int s_bl_max = 0;
bool s_blanked = false;

// ---- gamma (same as the ESP build) -------------------------------------------
uint8_t s_lut5[32];
uint8_t s_lut6[64];
uint16_t s_gain = 100;

DisplayFlushStats s_stats = {0, 0, 0};
char s_buf_info[24] = "";

inline uint16_t applyGain(uint16_t c) {
  if (s_gain <= 100) return c;
  return (uint16_t)((s_lut5[(c >> 11) & 0x1F] << 11) | (s_lut6[(c >> 5) & 0x3F] << 5) | s_lut5[c & 0x1F]);
}

inline void putPixel(int x, int y, uint16_t c) {
  if (s_rot180) { x = s_fb_w - 1 - x; y = s_fb_h - 1 - y; }
  if (s_fb_bpp == 16) {
    *(uint16_t*)(s_fb + y * s_fb_stride + x * 2) = c;
  } else {
    const uint32_t r = ((c >> 11) & 0x1F) * 255 / 31;
    const uint32_t g = ((c >> 5) & 0x3F) * 255 / 63;
    const uint32_t b = (c & 0x1F) * 255 / 31;
    *(uint32_t*)(s_fb + y * s_fb_stride + x * 4) = (r << s_fb_roff) | (g << s_fb_goff) | (b << s_fb_boff) | 0xFF000000u;
  }
}

inline uint16_t lerp565(uint16_t a, uint16_t b, int t /*0..256*/) {
  if (t <= 0 || a == b) return a;
  if (t >= 256) return b;
  const int ar = a >> 11, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  const int br = b >> 11, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  const int r = ar + (((br - ar) * t) >> 8);
  const int g = ag + (((bg - ag) * t) >> 8);
  const int bl = ab + (((bb - ab) * t) >> 8);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

// Repaints the panel rectangle that shows UI area (x1..x2, y1..y2).
void blitArea(int x1, int y1, int x2, int y2) {
  if (!s_fb) return;
  // One source pixel of margin, so smooth scaling blends across the edge.
  const int ox1 = (int)floorf((x1 - 1) * s_scale), ox2 = (int)ceilf((x2 + 2) * s_scale);
  const int oy1 = (int)floorf((y1 - 1) * s_scale), oy2 = (int)ceilf((y2 + 2) * s_scale);
  const int dx1 = std::max(0, ox1), dx2 = std::min(s_out_w - 1, ox2);
  const int dy1 = std::max(0, oy1), dy2 = std::min(s_out_h - 1, oy2);
  for (int dy = dy1; dy <= dy2; dy++) {
    const int32_t fy = s_src_y[dy];
    const int sy = fy >> 16;
    const int ty = (fy & 0xFFFF) >> 8;
    const uint16_t* row0 = &s_shadow[sy * UI_W];
    const uint16_t* row1 = &s_shadow[std::min(sy + 1, UI_H - 1) * UI_W];
    for (int dx = dx1; dx <= dx2; dx++) {
      const int32_t fx = s_src_x[dx];
      const int sx = fx >> 16;
      uint16_t c;
      if (s_nearest) {
        c = row0[sx];
      } else {
        const int tx = (fx & 0xFFFF) >> 8;
        const int sx1 = std::min(sx + 1, UI_W - 1);
        c = lerp565(lerp565(row0[sx], row0[sx1], tx), lerp565(row1[sx], row1[sx1], tx), ty);
      }
      putPixel(s_out_x + dx, s_out_y + dy, c);
    }
  }
}

void clearPanel() {
  if (!s_fb) return;
  memset(s_fb, 0, s_fb_len);
}

void lvglFlush(lv_disp_drv_t* drv, const lv_area_t* a, lv_color_t* px) {
  const uint32_t t0 = micros();
  const int w = a->x2 - a->x1 + 1;
  for (int y = a->y1; y <= a->y2; y++) {
    uint16_t* dst = &s_shadow[y * UI_W + a->x1];
    const lv_color_t* src = px + (size_t)(y - a->y1) * w;
    for (int x = 0; x < w; x++) dst[x] = applyGain(src[x].full);
  }
  if (!s_blanked) blitArea(a->x1, a->y1, a->x2, a->y2);
  const uint32_t us = micros() - t0;
  if (us > s_stats.max_us) s_stats.max_us = us;
  s_stats.sum_us += us;
  s_stats.count++;
  lv_disp_flush_ready(drv);
}

// Test mode without a panel: the "framebuffer" is a file of 800x480 XRGB8888.
bool openFileFramebuffer(const char* path) {
  s_fb_w = 800;
  s_fb_h = 480;
  s_fb_bpp = 32;
  s_fb_stride = s_fb_w * 4;
  s_fb_roff = 16;
  s_fb_goff = 8;
  s_fb_boff = 0;
  s_fb_len = (size_t)s_fb_stride * s_fb_h;
  int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
  if (fd < 0 || ftruncate(fd, (off_t)s_fb_len) != 0) {
    fprintf(stderr, "Display: cannot create %s\n", path);
    if (fd >= 0) close(fd);
    return false;
  }
  void* m = mmap(nullptr, s_fb_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (m == MAP_FAILED) { close(fd); return false; }
  s_fb = (uint8_t*)m;
  s_fb_fd = -1;
  fprintf(stderr, "Display: test image %s (800x480)\n", path);
  return true;
}

bool openFramebuffer() {
  const char* dev = getenv("SPOOLMANSCALE_FB");
  if (!dev || !*dev) dev = "/dev/fb0";
  if (strncmp(dev, "file:", 5) == 0) return openFileFramebuffer(dev + 5);
  s_fb_fd = open(dev, O_RDWR | O_CLOEXEC);
  if (s_fb_fd < 0) {
    fprintf(stderr, "Display: cannot open %s (%s)\n", dev, strerror(errno));
    return false;
  }
  struct fb_var_screeninfo v;
  struct fb_fix_screeninfo f;
  if (ioctl(s_fb_fd, FBIOGET_VSCREENINFO, &v) != 0 || ioctl(s_fb_fd, FBIOGET_FSCREENINFO, &f) != 0) {
    fprintf(stderr, "Display: %s is not a framebuffer\n", dev);
    close(s_fb_fd);
    s_fb_fd = -1;
    return false;
  }
  s_fb_w = (int)v.xres;
  s_fb_h = (int)v.yres;
  s_fb_bpp = (int)v.bits_per_pixel;
  s_fb_stride = (int)f.line_length;
  s_fb_roff = (int)v.red.offset;
  s_fb_goff = (int)v.green.offset;
  s_fb_boff = (int)v.blue.offset;
  if (s_fb_bpp != 16 && s_fb_bpp != 32) {
    fprintf(stderr, "Display: %d bpp is not supported (16 or 32)\n", s_fb_bpp);
    close(s_fb_fd);
    s_fb_fd = -1;
    return false;
  }
  s_fb_len = (size_t)f.line_length * v.yres;
  void* m = mmap(nullptr, s_fb_len, PROT_READ | PROT_WRITE, MAP_SHARED, s_fb_fd, 0);
  if (m == MAP_FAILED) {
    fprintf(stderr, "Display: mmap failed (%s)\n", strerror(errno));
    close(s_fb_fd);
    s_fb_fd = -1;
    return false;
  }
  s_fb = (uint8_t*)m;
  fprintf(stderr, "Display: %s %dx%d, %d bpp\n", dev, s_fb_w, s_fb_h, s_fb_bpp);
  return true;
}

// Keep the text console from drawing its cursor over the UI.
void consoleToGraphics() {
  const char* ttys[] = {"/dev/tty0", "/dev/tty1"};
  for (const char* t : ttys) {
    int fd = open(t, O_RDWR | O_CLOEXEC);
    if (fd < 0) continue;
    if (ioctl(fd, KDSETMODE, KD_GRAPHICS) == 0) {
      s_tty_fd = fd;
      atexit([] {
        if (s_tty_fd >= 0) ioctl(s_tty_fd, KDSETMODE, KD_TEXT);
      });
      return;
    }
    close(fd);
  }
  int fd = open("/sys/class/graphics/fbcon/cursor_blink", O_WRONLY);
  if (fd >= 0) {
    if (write(fd, "0", 1) < 0) {}
    close(fd);
  }
}

void computeLayout() {
  s_scale = std::min((float)s_fb_w / UI_W, (float)s_fb_h / UI_H);
  if (s_scale <= 0) s_scale = 1.0f;
  s_out_w = (int)floorf(UI_W * s_scale + 0.001f);
  s_out_h = (int)floorf(UI_H * s_scale + 0.001f);
  s_out_x = (s_fb_w - s_out_w) / 2;
  s_out_y = (s_fb_h - s_out_h) / 2;
  s_src_x.resize(s_out_w);
  s_src_y.resize(s_out_h);
  // Pixel centres map onto pixel centres.
  for (int i = 0; i < s_out_w; i++) {
    float sx = (i + 0.5f) / s_scale - 0.5f;
    if (sx < 0) sx = 0;
    if (sx > UI_W - 1) sx = UI_W - 1;
    s_src_x[i] = (int32_t)(sx * 65536.0f);
  }
  for (int i = 0; i < s_out_h; i++) {
    float sy = (i + 0.5f) / s_scale - 0.5f;
    if (sy < 0) sy = 0;
    if (sy > UI_H - 1) sy = UI_H - 1;
    s_src_y[i] = (int32_t)(sy * 65536.0f);
  }
  fprintf(stderr, "Display: UI 480x320 shown at %dx%d (x%.2f), offset %d,%d\n", s_out_w, s_out_h, s_scale,
          s_out_x, s_out_y);
}

void findBacklight() {
  const char* env = getenv("SPOOLMANSCALE_BACKLIGHT");
  std::string dir;
  if (env && *env) {
    dir = env;
  } else if (DIR* d = opendir("/sys/class/backlight")) {
    while (struct dirent* e = readdir(d)) {
      if (e->d_name[0] == '.') continue;
      dir = std::string("/sys/class/backlight/") + e->d_name;
      break;
    }
    closedir(d);
  }
  if (dir.empty()) return;
  FILE* f = fopen((dir + "/max_brightness").c_str(), "r");
  if (!f) return;
  if (fscanf(f, "%d", &s_bl_max) != 1) s_bl_max = 0;
  fclose(f);
  if (s_bl_max > 0) {
    s_bl_path = dir + "/brightness";
    fprintf(stderr, "Display: backlight %s (max %d)\n", dir.c_str(), s_bl_max);
  }
}

void writeBacklight(uint8_t level) {
  if (s_bl_path.empty()) return;
  FILE* f = fopen(s_bl_path.c_str(), "w");
  if (!f) return;
  int v = (int)((long)level * s_bl_max / 255);
  if (level > 0 && v == 0) v = 1;
  fprintf(f, "%d", v);
  fclose(f);
}

void blank(bool on) {
  if (s_fb_fd >= 0) ioctl(s_fb_fd, FBIOBLANK, on ? FB_BLANK_POWERDOWN : FB_BLANK_UNBLANK);
  if (s_blanked && !on) {
    s_blanked = false;
    clearPanel();
    blitArea(0, 0, UI_W - 1, UI_H - 1);
  }
  s_blanked = on;
}

// ---- touch -------------------------------------------------------------------
int s_touch_fd = -1;
int s_tx_min = 0, s_tx_max = 799, s_ty_min = 0, s_ty_max = 479;
int s_raw_x = 0, s_raw_y = 0;
bool s_down = false;
bool s_swap_xy = false, s_inv_x = false, s_inv_y = false;
int s_last_x = 0, s_last_y = 0;

bool envOn(const char* n) {
  const char* v = getenv(n);
  return v && (*v == '1' || *v == 'y' || *v == 'Y' || *v == 't' || *v == 'T');
}

bool testBit(const unsigned long* bits, int b) {
  return (bits[b / (8 * sizeof(long))] >> (b % (8 * sizeof(long)))) & 1;
}

bool isTouchDevice(int fd) {
  unsigned long ev[(EV_MAX + 8 * sizeof(long)) / (8 * sizeof(long))] = {0};
  unsigned long abs[(ABS_MAX + 8 * sizeof(long)) / (8 * sizeof(long))] = {0};
  if (ioctl(fd, EVIOCGBIT(0, sizeof(ev)), ev) < 0 || !testBit(ev, EV_ABS)) return false;
  if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs)), abs) < 0) return false;
  return testBit(abs, ABS_MT_POSITION_X) || testBit(abs, ABS_X);
}

// Test input: a FIFO that takes "tap X Y" in panel pixels.
int s_pipe_fd = -1;
std::string s_pipe_buf;
int s_pipe_hold = 0;   // polls left with the finger down

void openPipe(const char* path) {
  mkfifo(path, 0666);
  s_pipe_fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);   // RDWR: never sees EOF
  if (s_pipe_fd >= 0) {
    s_tx_min = 0; s_tx_max = s_fb_w - 1; s_ty_min = 0; s_ty_max = s_fb_h - 1;
    fprintf(stderr, "Touch: test pipe %s\n", path);
  }
}

void pollPipe() {
  if (s_pipe_fd < 0) return;
  if (s_pipe_hold > 0) {
    if (--s_pipe_hold == 0) s_down = false;
    return;
  }
  char b[256];
  ssize_t n = read(s_pipe_fd, b, sizeof(b));
  if (n > 0) s_pipe_buf.append(b, (size_t)n);
  size_t nl = s_pipe_buf.find('\n');
  if (nl == std::string::npos) return;
  std::string line = s_pipe_buf.substr(0, nl);
  s_pipe_buf.erase(0, nl + 1);
  int x, y;
  if (sscanf(line.c_str(), "tap %d %d", &x, &y) == 2) {
    s_raw_x = x;
    s_raw_y = y;
    s_down = true;
    s_pipe_hold = 3;
  }
}

void openTouch() {
  const char* env = getenv("SPOOLMANSCALE_TOUCH");
  if (env && strncmp(env, "pipe:", 5) == 0) {
    openPipe(env + 5);
    return;
  }
  std::vector<std::string> cand;
  if (env && *env) {
    cand.push_back(env);
  } else if (DIR* d = opendir("/dev/input")) {
    while (struct dirent* e = readdir(d))
      if (strncmp(e->d_name, "event", 5) == 0) cand.push_back(std::string("/dev/input/") + e->d_name);
    closedir(d);
    std::sort(cand.begin(), cand.end());
  }
  for (auto& p : cand) {
    int fd = open(p.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) continue;
    if (!isTouchDevice(fd)) { close(fd); continue; }
    struct input_absinfo ax, ay;
    bool mt = ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &ax) == 0 && ax.maximum > ax.minimum &&
              ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &ay) == 0 && ay.maximum > ay.minimum;
    if (!mt) {
      if (ioctl(fd, EVIOCGABS(ABS_X), &ax) != 0 || ioctl(fd, EVIOCGABS(ABS_Y), &ay) != 0) { close(fd); continue; }
    }
    s_tx_min = ax.minimum; s_tx_max = ax.maximum;
    s_ty_min = ay.minimum; s_ty_max = ay.maximum;
    char name[128] = "?";
    ioctl(fd, EVIOCGNAME(sizeof(name)), name);
    fprintf(stderr, "Touch: %s (%s), x %d..%d, y %d..%d\n", p.c_str(), name, s_tx_min, s_tx_max, s_ty_min, s_ty_max);
    s_touch_fd = fd;
    return;
  }
  fprintf(stderr, "Touch: no touch screen found under /dev/input\n");
}

void pollTouch() {
  if (s_touch_fd < 0) return;
  struct input_event ev[64];
  for (;;) {
    ssize_t n = read(s_touch_fd, ev, sizeof(ev));
    if (n <= 0) break;
    for (size_t i = 0; i < (size_t)n / sizeof(ev[0]); i++) {
      const input_event& e = ev[i];
      if (e.type == EV_ABS) {
        if (e.code == ABS_MT_POSITION_X || e.code == ABS_X) s_raw_x = e.value;
        else if (e.code == ABS_MT_POSITION_Y || e.code == ABS_Y) s_raw_y = e.value;
        else if (e.code == ABS_MT_TRACKING_ID) s_down = e.value >= 0;
      } else if (e.type == EV_KEY && e.code == BTN_TOUCH) {
        s_down = e.value != 0;
      }
    }
  }
}

void lvglTouch(lv_indev_drv_t*, lv_indev_data_t* data) {
  pollTouch();
  pollPipe();
  if (s_down) {
    // Raw -> panel pixels -> UI points.
    float nx = (float)(s_raw_x - s_tx_min) / (float)(s_tx_max - s_tx_min);
    float ny = (float)(s_raw_y - s_ty_min) / (float)(s_ty_max - s_ty_min);
    if (s_swap_xy) std::swap(nx, ny);
    if (s_inv_x) nx = 1.0f - nx;
    if (s_inv_y) ny = 1.0f - ny;
    if (s_rot180) { nx = 1.0f - nx; ny = 1.0f - ny; }
    const float px = nx * s_fb_w, py = ny * s_fb_h;
    int x = (int)((px - s_out_x) / s_scale);
    int y = (int)((py - s_out_y) / s_scale);
    x = std::max(0, std::min(UI_W - 1, x));
    y = std::max(0, std::min(UI_H - 1, y));
    s_last_x = x;
    s_last_y = y;
    data->state = LV_INDEV_STATE_PR;
    if (s_touch_activity) s_touch_activity();
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
  data->point.x = (lv_coord_t)s_last_x;
  data->point.y = (lv_coord_t)s_last_y;
}
}  // namespace

// ---- the display.h API ---------------------------------------------------------
const char* displayDrawBufInfo() { return s_buf_info; }

uint16_t displayGetUiGain() { return s_gain; }

void displaySetUiGain(uint16_t gamma_x100) {
  if (gamma_x100 < 100) gamma_x100 = 100;
  if (gamma_x100 > 300) gamma_x100 = 300;
  s_gain = gamma_x100;
  if (s_gain > 100) {
    const float inv = 100.0f / (float)s_gain;
    for (int i = 0; i < 32; i++) s_lut5[i] = (uint8_t)lroundf(powf((float)i / 31.0f, inv) * 31.0f);
    for (int i = 0; i < 64; i++) s_lut6[i] = (uint8_t)lroundf(powf((float)i / 63.0f, inv) * 63.0f);
  }
  if (lv_disp_get_default()) lv_obj_invalidate(lv_scr_act());
}

DisplayFlushStats displayFlushStatsTake() {
  const DisplayFlushStats t = s_stats;
  s_stats = {0, 0, 0};
  return t;
}

bool displayHardwareBegin(void (*touch_activity_cb)()) {
  s_touch_activity = touch_activity_cb;
  s_rot180 = getenv("SPOOLMANSCALE_ROTATE") && atoi(getenv("SPOOLMANSCALE_ROTATE")) == 180;
  const char* sc = getenv("SPOOLMANSCALE_SCALE");
  s_nearest = sc && strcmp(sc, "nearest") == 0;
  s_swap_xy = envOn("SPOOLMANSCALE_TOUCH_SWAP_XY");
  s_inv_x = envOn("SPOOLMANSCALE_TOUCH_INVERT_X");
  s_inv_y = envOn("SPOOLMANSCALE_TOUCH_INVERT_Y");

  const bool have_fb = openFramebuffer();
  if (have_fb && s_fb_fd >= 0) {
    consoleToGraphics();
    blank(false);
  }
  computeLayout();
  clearPanel();
  findBacklight();
  writeBacklight(BRIGHT_NORMAL_DEFAULT);
  openTouch();

  lv_init();
  static lv_disp_draw_buf_t draw_buf;
  static std::vector<lv_color_t> buf(UI_W * UI_H);
  lv_disp_draw_buf_init(&draw_buf, buf.data(), nullptr, UI_W * UI_H);
  snprintf(s_buf_info, sizeof(s_buf_info), "pi/%d", UI_H);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = UI_W;
  disp_drv.ver_res = UI_H;
  disp_drv.flush_cb = lvglFlush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = lvglTouch;
  indev_drv.feedback_cb = touchFeedback;
  lv_indev_drv_register(&indev_drv);
  // Without a panel the program still runs (web interface, backends), which
  // helps when testing over SSH.
  return true;
}

void displaySetBrightness(uint8_t brightness) {
  if (brightness == 0) {
    writeBacklight(0);
    blank(true);
  } else {
    if (s_blanked) blank(false);
    writeBacklight(brightness);
  }
}

void displayBacklightOff() { displaySetBrightness(0); }
void displayBacklightOn(uint8_t brightness) { displaySetBrightness(brightness ? brightness : BRIGHT_NORMAL_DEFAULT); }
void displayPrepareDeepSleep() { displaySetBrightness(0); }

// For the Pi's own tools: the current picture as the UI drew it (480x320 RGB565).
const uint16_t* displayPiShadow() { return s_shadow.data(); }
