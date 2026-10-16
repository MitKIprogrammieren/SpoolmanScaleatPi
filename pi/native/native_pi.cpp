// Runtime side of native_scale.h: the factors and the font map.
// This file is compiled without the wrappers.
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <mutex>

#include "lvgl.h"

LV_FONT_DECLARE(lv_font_montserrat_ext_38)
LV_FONT_DECLARE(lv_font_montserrat_ext_42)
LV_FONT_DECLARE(lv_font_montserrat_ext_44)
LV_FONT_DECLARE(lv_font_montserrat_ext_48)

static bool envNative() {
  const char* e = getenv("SPOOLMANSCALE_NATIVE");
  return e && *e && strcmp(e, "0") != 0;
}

bool g_pi_native = envNative();
float g_pi_sx = g_pi_native ? 800.0f / 480.0f : 1.0f;
float g_pi_sy = g_pi_native ? 480.0f / 320.0f : 1.0f;
float g_pi_s  = g_pi_native ? 1.5f : 1.0f;

// Called by the display driver once it knows the panel's size.
void piNativeSetPanel(int w, int h) {
  if (!g_pi_native || w <= 0 || h <= 0) return;
  g_pi_sx = w / 480.0f;
  g_pi_sy = h / 320.0f;
  g_pi_s = g_pi_sx < g_pi_sy ? g_pi_sx : g_pi_sy;
}

namespace {
struct Entry { const lv_font_t* from; const lv_font_t* to; const lv_font_t* fb; };

// Montserrat sizes at 1.5x, rounded down to an existing size; the
// French supplement glyphs come along as the fallback of the larger face.
const Entry kMap[] = {
  { &lv_font_montserrat_ext_8,  &lv_font_montserrat_ext_12, &lv_font_fr_supp_12 },
  { &lv_font_montserrat_ext_10, &lv_font_montserrat_ext_14, &lv_font_fr_supp_14 },
  { &lv_font_montserrat_ext_12, &lv_font_montserrat_ext_18, &lv_font_fr_supp_18 },
  { &lv_font_montserrat_ext_14, &lv_font_montserrat_ext_20, &lv_font_fr_supp_20 },
  { &lv_font_montserrat_ext_16, &lv_font_montserrat_ext_24, &lv_font_fr_supp_24 },
  { &lv_font_montserrat_ext_18, &lv_font_montserrat_ext_26, &lv_font_fr_supp_24 },
  { &lv_font_montserrat_ext_20, &lv_font_montserrat_ext_30, &lv_font_fr_supp_24 },
  { &lv_font_montserrat_ext_22, &lv_font_montserrat_ext_32, &lv_font_fr_supp_24 },
  { &lv_font_montserrat_ext_24, &lv_font_montserrat_ext_36, &lv_font_fr_supp_24 },
  { &lv_font_montserrat_ext_26, &lv_font_montserrat_ext_38, &lv_font_fr_supp_24 },
  { &lv_font_montserrat_ext_28, &lv_font_montserrat_ext_42, &lv_font_fr_supp_24 },
  { &lv_font_montserrat_ext_30, &lv_font_montserrat_ext_44, &lv_font_fr_supp_24 },
  { &lv_font_montserrat_ext_32, &lv_font_montserrat_ext_48, &lv_font_fr_supp_24 },
  { &lv_font_montserrat_ext_34, &lv_font_montserrat_ext_48, &lv_font_fr_supp_24 },
  { &lv_font_montserrat_ext_36, &lv_font_montserrat_ext_48, &lv_font_fr_supp_24 },
  { &lv_font_fr_supp_10, &lv_font_fr_supp_14, nullptr },
  { &lv_font_fr_supp_12, &lv_font_fr_supp_18, nullptr },
  { &lv_font_fr_supp_14, &lv_font_fr_supp_20, nullptr },
  { &lv_font_fr_supp_16, &lv_font_fr_supp_24, nullptr },
  { &lv_font_fr_supp_18, &lv_font_fr_supp_24, nullptr },
  { &lv_font_fr_supp_20, &lv_font_fr_supp_24, nullptr },
};
constexpr size_t kCount = sizeof(kMap) / sizeof(kMap[0]);
lv_font_t s_copy[kCount];
std::once_flag s_once;

void build() {
  for (size_t i = 0; i < kCount; i++) {
    s_copy[i] = *kMap[i].to;
    if (!s_copy[i].fallback) s_copy[i].fallback = kMap[i].fb;
  }
}
}  // namespace

const lv_font_t* piNativeFont(const lv_font_t* f) {
  if (!g_pi_native || !f) return f;
  std::call_once(s_once, build);
  for (size_t i = 0; i < kCount; i++)
    if (kMap[i].from == f) return &s_copy[i];
  // already a native face (e.g. set twice) or one without a pair
  for (size_t i = 0; i < kCount; i++)
    if (&s_copy[i] == f) return f;
  return f;
}

// Debug aid (SPOOLMANSCALE_NATIVE_AUDIT=1): lists objects that reach past
// the edge of the screen, which would make the screen scroll.
static void auditObj(lv_obj_t* o, int depth) {
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  const lv_coord_t W = lv_disp_get_hor_res(nullptr), H = lv_disp_get_ver_res(nullptr);
  if (depth > 0 && (a.x2 >= W || a.y2 >= H || a.x1 < 0 || a.y1 < 0) && !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN))
    fprintf(stderr, "Audit: depth %d obj %p class %p area %d,%d..%d,%d\n", depth, (void*)o,
            (const void*)lv_obj_get_class(o), a.x1, a.y1, a.x2, a.y2);
  if (lv_obj_get_scroll_bottom(o) > 0 || lv_obj_get_scroll_right(o) > 0)
    fprintf(stderr, "Audit: depth %d obj %p scrolls b=%d r=%d area %d,%d..%d,%d\n", depth, (void*)o,
            lv_obj_get_scroll_bottom(o), lv_obj_get_scroll_right(o), a.x1, a.y1, a.x2, a.y2);
  if (depth >= 4) return;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) auditObj(lv_obj_get_child(o, (int32_t)i), depth + 1);
}
void piNativeAudit() {
  static bool on = getenv("SPOOLMANSCALE_NATIVE_AUDIT") != nullptr;
  static time_t last = 0;
  if (!on || time(nullptr) - last < 4) return;
  last = time(nullptr);
  fprintf(stderr, "Audit: run\n");
  auditObj(lv_scr_act(), 0);
}
