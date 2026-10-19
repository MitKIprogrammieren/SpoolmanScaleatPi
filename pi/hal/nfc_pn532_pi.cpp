// PN532 NFC reader for the Raspberry Pi build.
//
// Default transport is HSU (UART) on /dev/serial0 = GPIO14 (TXD) / GPIO15 (RXD),
// 115200 baud: the Pi's I2C controller does not handle the clock stretching the
// PN532 does, which makes it unreliable there. Set the module's DIP switches to
// HSU (SW1 OFF, SW2 OFF).
//
// SPOOLMANSCALE_PN532=/dev/ttyUSB0   another serial port (e.g. a USB adapter)
// SPOOLMANSCALE_PN532=i2c            the shared I2C bus at 0x24, as on the ESP
//
// A second reader (a spool lies with its tag anywhere around the hub, two
// readers see more of the circle) sits on UART3: GPIO4/TXD3 (pin 7) and
// GPIO5/RXD3 (pin 29), enabled by dtoverlay=uart3 (install.sh sets it).
// It is found on its own; SPOOLMANSCALE_PN532_2=/dev/ttyXXX names it,
// SPOOLMANSCALE_PN532_2=off switches the search off. Both are polled in turn;
// the one that sees a tag serves the reads that follow.
//
// The functions are those of src/hardware/nfc.h, with the same retry rules the
// ESP version had around the Adafruit library.

#include "hardware/nfc.h"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <string>
#include <termios.h>
#include <unistd.h>
#include <vector>

#include "app_config.h"

namespace {
constexpr uint8_t CMD_GET_FIRMWARE = 0x02;
constexpr uint8_t CMD_SAM_CONFIG = 0x14;
constexpr uint8_t CMD_RF_CONFIG = 0x32;
constexpr uint8_t CMD_IN_DATA_EXCHANGE = 0x40;
constexpr uint8_t CMD_IN_LIST_PASSIVE = 0x4A;
constexpr uint8_t MIFARE_AUTH_A = 0x60;
constexpr uint8_t MIFARE_AUTH_B = 0x61;
constexpr uint8_t MIFARE_READ = 0x30;
constexpr uint8_t ULTRALIGHT_WRITE = 0xA2;
constexpr uint8_t MIFARE_SECTOR_READ_ATTEMPTS = 3;

enum Transport { T_NONE, T_UART, T_I2C };

struct Reader {
  Transport t = T_NONE;
  int fd = -1;
  std::string dev;
  bool ok = false;
  bool need_wake = true;
};
Reader s_r[2];
int s_cur = 0;            // the reader the next command goes to
TwoWire* s_wire = nullptr;
bool s_ok = false;
bool s_second_tried = false;

// The code below was written for one reader; these name the current one.
#define s_t (s_r[s_cur].t)
#define s_fd (s_r[s_cur].fd)
#define s_dev (s_r[s_cur].dev)
#define s_need_wake (s_r[s_cur].need_wake)

// ---- UART --------------------------------------------------------------------
bool uartOpen() {
  // O_NONBLOCK: without it, opening a serial port can wait for a carrier
  // signal that a PN532 never raises. Reads use poll() anyway.
  s_fd = open(s_dev.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
  if (s_fd < 0) {
    fprintf(stderr, "PN532: cannot open %s (%s)\n", s_dev.c_str(), strerror(errno));
    return false;
  }
  struct termios t;
  tcgetattr(s_fd, &t);
  cfmakeraw(&t);
  cfsetispeed(&t, B115200);
  cfsetospeed(&t, B115200);
  t.c_cflag |= CLOCAL | CREAD;
  t.c_cflag &= ~(CSTOPB | CRTSCTS | PARENB);
  t.c_cc[VMIN] = 0;
  t.c_cc[VTIME] = 0;
  tcsetattr(s_fd, TCSANOW, &t);
  tcflush(s_fd, TCIOFLUSH);
  fprintf(stderr, "PN532: using %s\n", s_dev.c_str());
  return true;
}

bool uartWrite(const uint8_t* b, size_t n) {
  size_t done = 0;
  while (done < n) {
    ssize_t w = write(s_fd, b + done, n - done);
    if (w < 0) {
      if (errno == EINTR) continue;
      if (errno == EAGAIN) {
        struct pollfd p = {s_fd, POLLOUT, 0};
        if (poll(&p, 1, 100) != 1) return false;
        continue;
      }
      return false;
    }
    done += (size_t)w;
  }
  return true;
}

int uartReadByte(int timeout_ms) {
  struct pollfd p = {s_fd, POLLIN, 0};
  if (poll(&p, 1, timeout_ms) != 1) return -1;
  uint8_t c;
  return read(s_fd, &c, 1) == 1 ? c : -1;
}

// ---- I2C ---------------------------------------------------------------------
bool i2cReady(int timeout_ms) {
  const unsigned long t0 = millis();
  do {
    if (s_wire->requestFrom((uint8_t)I2C_ADDR_PN532, (size_t)1) == 1 && (s_wire->read() & 0x01)) return true;
    delay(2);
  } while ((int)(millis() - t0) < timeout_ms);
  return false;
}

// ---- framing -----------------------------------------------------------------
bool sendFrame(const uint8_t* data, size_t n) {
  std::vector<uint8_t> f;
  if (s_t == T_UART && s_need_wake) {
    // HSU wake-up: 0x55 and a run of zeros before the first frame.
    static const uint8_t wake[] = {0x55, 0x55, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    f.insert(f.end(), wake, wake + sizeof(wake));
    s_need_wake = false;
  }
  const uint8_t len = (uint8_t)(n + 1);
  f.push_back(0x00);
  f.push_back(0x00);
  f.push_back(0xFF);
  f.push_back(len);
  f.push_back((uint8_t)(~len + 1));
  f.push_back(0xD4);
  uint8_t sum = 0xD4;
  for (size_t i = 0; i < n; i++) { f.push_back(data[i]); sum = (uint8_t)(sum + data[i]); }
  f.push_back((uint8_t)(~sum + 1));
  f.push_back(0x00);
  if (s_t == T_UART) {
    tcflush(s_fd, TCIFLUSH);
    return uartWrite(f.data(), f.size());
  }
  s_wire->beginTransmission(I2C_ADDR_PN532);
  s_wire->write(f.data(), f.size());
  return s_wire->endTransmission() == 0;
}

void sendAck() {
  static const uint8_t ack[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
  if (s_t == T_UART) uartWrite(ack, sizeof(ack));
  else if (s_t == T_I2C) {
    s_wire->beginTransmission(I2C_ADDR_PN532);
    s_wire->write(ack, sizeof(ack));
    s_wire->endTransmission();
  }
}

// Reads one frame from the chip: returns the payload after TFI, or an empty
// vector with ack=true for an ACK frame. For UART the bytes are a stream; for
// I2C one read returns the status byte plus up to `max` bytes.
bool readFrame(std::vector<uint8_t>& payload, bool& is_ack, int timeout_ms) {
  payload.clear();
  is_ack = false;
  std::vector<uint8_t> raw;
  if (s_t == T_I2C) {
    if (!i2cReady(timeout_ms)) return false;
    const size_t want = 64;
    if (s_wire->requestFrom((uint8_t)I2C_ADDR_PN532, want) != want) return false;
    s_wire->read();   // status
    while (s_wire->available()) raw.push_back((uint8_t)s_wire->read());
  }
  size_t pos = 0;
  auto next = [&](int tmo) -> int {
    if (s_t == T_UART) return uartReadByte(tmo);
    return pos < raw.size() ? raw[pos++] : -1;
  };
  // Find 00 FF.
  int prev = -1, c;
  const unsigned long t0 = millis();
  for (;;) {
    int left = timeout_ms - (int)(millis() - t0);
    if (left < 1) left = 1;
    c = next(left);
    if (c < 0) return false;
    if (prev == 0x00 && c == 0xFF) break;
    prev = c;
  }
  int len = next(20), lcs = next(20);
  if (len < 0 || lcs < 0) return false;
  if (len == 0x00 && lcs == 0xFF) { next(5); is_ack = true; return true; }
  if (((len + lcs) & 0xFF) != 0) return false;
  uint8_t sum = 0;
  for (int i = 0; i < len; i++) {
    int b = next(20);
    if (b < 0) return false;
    payload.push_back((uint8_t)b);
    sum = (uint8_t)(sum + b);
  }
  int dcs = next(20);
  next(5);   // postamble
  if (dcs < 0 || ((sum + dcs) & 0xFF) != 0) return false;
  if (payload.empty() || payload[0] != 0xD5) return false;
  payload.erase(payload.begin());   // TFI
  return true;
}

// Sends a command and returns the response data (after the response code).
bool command(const std::vector<uint8_t>& cmd, std::vector<uint8_t>& resp, int timeout_ms) {
  if (s_t == T_NONE) return false;
  if (!sendFrame(cmd.data(), cmd.size())) return false;
  std::vector<uint8_t> p;
  bool ack = false;
  if (!readFrame(p, ack, 50) || !ack) {
    s_need_wake = true;
    return false;
  }
  if (!readFrame(p, ack, timeout_ms) || ack || p.empty() || p[0] != (uint8_t)(cmd[0] + 1)) {
    sendAck();   // abort whatever the chip is still waiting for
    if (s_t == T_UART) { delay(5); tcflush(s_fd, TCIFLUSH); }
    return false;
  }
  resp.assign(p.begin() + 1, p.end());
  return true;
}

uint32_t firmwareVersion() {
  std::vector<uint8_t> r;
  if (!command({CMD_GET_FIRMWARE}, r, 100) || r.size() < 4) return 0;
  return ((uint32_t)r[0] << 24) | ((uint32_t)r[1] << 16) | ((uint32_t)r[2] << 8) | r[3];
}

bool samConfig() {
  std::vector<uint8_t> r;
  return command({CMD_SAM_CONFIG, 0x01, 0x14, 0x01}, r, 100);
}

// A few passive activation attempts per InListPassiveTarget, so a call with
// no tag on the pad returns instead of waiting forever.
void limitRetries() {
  std::vector<uint8_t> r;
  command({CMD_RF_CONFIG, 0x05, 0xFF, 0x01, 0x02}, r, 100);
}

bool exchange(const std::vector<uint8_t>& data, std::vector<uint8_t>& out, int timeout_ms = 200) {
  std::vector<uint8_t> cmd = {CMD_IN_DATA_EXCHANGE, 0x01};
  cmd.insert(cmd.end(), data.begin(), data.end());
  std::vector<uint8_t> r;
  if (!command(cmd, r, timeout_ms) || r.empty() || (r[0] & 0x3F) != 0) return false;
  out.assign(r.begin() + 1, r.end());
  return true;
}

// The second UART, if one exists that is not the first reader's port.
std::string secondPort() {
  const char* env = getenv("SPOOLMANSCALE_PN532_2");
  if (env && *env) return strcmp(env, "off") == 0 ? std::string() : std::string(env);
  char first[PATH_MAX] = "";
  if (s_r[0].t == T_UART && !realpath(s_r[0].dev.c_str(), first)) first[0] = 0;
  for (const char* c : {"/dev/ttyAMA3", "/dev/ttyAMA1", "/dev/ttyAMA2", "/dev/ttyAMA4"}) {
    char real[PATH_MAX];
    if (!realpath(c, real)) continue;
    if (first[0] && strcmp(real, first) == 0) continue;
    return c;
  }
  return std::string();
}

bool startTransport() {
  const char* env = getenv("SPOOLMANSCALE_PN532");
  std::string sel = env && *env ? env : "/dev/serial0";
  if (sel == "i2c") {
    if (!s_wire) return false;
    s_t = T_I2C;
    return true;
  }
  s_dev = sel;
  if (s_fd >= 0) { close(s_fd); s_fd = -1; }
  if (!uartOpen()) return false;
  s_t = T_UART;
  s_need_wake = true;
  return true;
}

bool bringUp(uint32_t* fw) {
  uint32_t v = 0;
  for (int i = 0; i < 3 && !v; i++) {
    v = firmwareVersion();
    if (!v) { s_need_wake = true; delay(50); }
  }
  if (fw) *fw = v;
  if (!v) return false;
  if (!samConfig()) return false;
  limitRetries();
  return true;
}

bool authenticate(uint8_t block, uint8_t cmd, uint8_t key[6], uint8_t uid[4]) {
  std::vector<uint8_t> d = {cmd, block};
  d.insert(d.end(), key, key + 6);
  d.insert(d.end(), uid, uid + 4);
  std::vector<uint8_t> out;
  return exchange(d, out);
}

bool readBlock16(uint8_t block, uint8_t data[16]) {
  std::vector<uint8_t> out;
  if (!exchange({MIFARE_READ, block}, out) || out.size() < 16) return false;
  memcpy(data, out.data(), 16);
  return true;
}
}  // namespace

// The second reader: looked for once, kept when it answers.
static void startSecond() {
  if (s_second_tried) return;
  s_second_tried = true;
  const std::string dev = secondPort();
  if (dev.empty()) return;
  const int keep = s_cur;
  s_cur = 1;
  s_dev = dev;
  if (uartOpen()) {
    s_t = T_UART;
    s_need_wake = true;
    uint32_t fw = 0;
    s_r[1].ok = bringUp(&fw);
    if (s_r[1].ok) {
      fprintf(stderr, "PN532: second reader on %s, firmware %08x\n", dev.c_str(), (unsigned)fw);
    } else {
      fprintf(stderr, "PN532: nothing answers on %s, one reader\n", dev.c_str());
      close(s_fd);
      s_fd = -1;
      s_t = T_NONE;
    }
  }
  s_cur = keep;
}

static bool firstUp(uint32_t* firmware_version) {
  s_cur = 0;
  if (s_t == T_NONE && !startTransport()) {
    if (firmware_version) *firmware_version = 0;
    return false;
  }
  if (s_t == T_UART) {
    tcflush(s_fd, TCIOFLUSH);
    s_need_wake = true;
  }
  s_r[0].ok = bringUp(firmware_version);
  return s_r[0].ok;
}

bool nfcHardwareBegin(TwoWire* wire, int8_t, int8_t, uint32_t* firmware_version) {
  s_wire = wire;
  firstUp(firmware_version);
  startSecond();
  // With only the second one answering the scale still reads tags.
  s_ok = s_r[0].ok || s_r[1].ok;
  if (!s_r[0].ok && s_r[1].ok) {
    s_cur = 1;
    if (firmware_version) *firmware_version = firmwareVersion();
  }
  return s_ok;
}

bool nfcHardwarePing() {
  for (int i = 0; i < 2; i++) {
    if (!s_r[i].ok && i == 1) continue;
    const int keep = s_cur;
    s_cur = i;
    const bool alive = s_t != T_NONE && firmwareVersion() != 0;
    s_cur = keep;
    if (alive) return true;
  }
  return false;
}

bool nfcHardwareReinit(uint32_t* firmware_version) {
  const bool first = firstUp(firmware_version);
  if (s_r[1].t != T_NONE) {
    s_cur = 1;
    tcflush(s_fd, TCIOFLUSH);
    s_need_wake = true;
    s_r[1].ok = bringUp(nullptr);
  }
  s_cur = first ? 0 : (s_r[1].ok ? 1 : 0);
  s_ok = first || s_r[1].ok;
  return s_ok;
}

static bool listPassive(uint8_t* uid, uint8_t* uid_len, uint16_t timeout_ms);

// Asks the reader that saw the last tag first, then the other one; the one that
// finds a tag stays current, so the sector and page reads go to it.
bool nfcReadPassiveTarget(uint8_t* uid, uint8_t* uid_len, uint16_t timeout_ms) {
  const int first = s_cur;
  if (s_r[first].t != T_NONE && listPassive(uid, uid_len, timeout_ms)) return true;
  const int other = 1 - first;
  if (s_r[other].t == T_NONE || !s_r[other].ok) return false;
  s_cur = other;
  if (listPassive(uid, uid_len, timeout_ms)) return true;
  s_cur = first;
  return false;
}

static bool listPassive(uint8_t* uid, uint8_t* uid_len, uint16_t timeout_ms) {
  std::vector<uint8_t> r;
  if (!command({CMD_IN_LIST_PASSIVE, 0x01, 0x00}, r, timeout_ms ? timeout_ms : 1000)) return false;
  // NbTg, Tg, SENS_RES(2), SEL_RES, NFCIDLength, NFCID...
  if (r.size() < 6 || r[0] != 1) return false;
  uint8_t n = r[5];
  if (n > NFC_UID_MAX || r.size() < (size_t)6 + n) return false;
  memcpy(uid, &r[6], n);
  *uid_len = n;
  return true;
}

static bool reselectMifareUid(const uint8_t expected_uid[4], uint16_t timeout_ms) {
  uint8_t uid[NFC_UID_MAX] = {0};
  uint8_t uid_len = 0;
  return listPassive(uid, &uid_len, timeout_ms) && uid_len == 4 && memcmp(uid, expected_uid, 4) == 0;
}

static bool authenticateMifareTrailer(uint8_t trailer_block, uint8_t key[6], uint8_t uid[4], uint8_t key_type) {
  if (key_type == NFC_KEY_A) return authenticate(trailer_block, MIFARE_AUTH_A, key, uid);
  // As on the ESP: key B, and key B once more (see the note in the ESP nfc.cpp).
  if (authenticate(trailer_block, MIFARE_AUTH_B, key, uid)) return true;
  return authenticate(trailer_block, MIFARE_AUTH_B, key, uid);
}

bool nfcReadMifareSector(int sector, uint8_t key[6], uint8_t uid[4], uint8_t blocks[4][16], uint8_t key_type) {
  const uint8_t trailer_block = (uint8_t)(sector * 4 + 3);
  if (s_t == T_NONE) return false;
  for (uint8_t attempt = 0; attempt < MIFARE_SECTOR_READ_ATTEMPTS; attempt++) {
    if (attempt > 0) {
      delay(20);
      if (!reselectMifareUid(uid, 120)) continue;
    }
    if (!authenticateMifareTrailer(trailer_block, key, uid, key_type)) continue;
    bool ok = true;
    for (int b = 0; b < 3; b++) {
      if (!readBlock16((uint8_t)(sector * 4 + b), blocks[b])) {
        memset(blocks[b], 0, 16);
        ok = false;
        break;
      }
    }
    if (ok) return true;
  }
  memset(blocks, 0, 4 * 16);
  return false;
}

bool nfcReadMifareBlock(uint8_t block, uint8_t data[16]) { return s_t != T_NONE && readBlock16(block, data); }

bool nfcReadNtagPage(uint8_t page, uint8_t* data) {
  if (s_t == T_NONE) return false;
  for (int retry = 0; retry < 3; retry++) {
    uint8_t b[16];
    if (readBlock16(page, b)) {
      memcpy(data, b, 4);
      return true;
    }
    delay(2);
  }
  return false;
}

bool nfcWriteNtagPage(uint8_t page, uint8_t* data) {
  if (s_t == T_NONE) return false;
  std::vector<uint8_t> out;
  return exchange({ULTRALIGHT_WRITE, page, data[0], data[1], data[2], data[3]}, out);
}
