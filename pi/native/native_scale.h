// Native 800x480 mode for the Pi (SPOOLMANSCALE_NATIVE=1).
//
// The firmware lays its screens out for 480x320. Instead of drawing them at
// that size and stretching the picture, this header - force-included into
// every firmware C++ file - wraps the LVGL calls that take or return pixels,
// so LVGL itself works at the display's 800x480 and every edge, font and
// corner is drawn sharp at full resolution:
//
//   x/width        * 800/480      y/height, radius, border, padding * 1.5
//   fonts          Montserrat at 1.5x, rounded down (14 -> 20, 20 -> 30, ...)
//   reads back     (sizes, coordinates, text metrics, touch) divided again,
//                  so the firmware keeps thinking in 480x320
//
// Without SPOOLMANSCALE_NATIVE every factor is 1 and nothing changes.
#pragma once
#ifdef __cplusplus

#include <cmath>
#include "lvgl.h"

extern float g_pi_sx;   // horizontal factor
extern float g_pi_sy;   // vertical factor
extern float g_pi_s;    // radius, border, padding, fonts
extern bool g_pi_native;
const lv_font_t* piNativeFont(const lv_font_t* f);

namespace pinat {
inline bool special(lv_coord_t v) {
  return LV_COORD_IS_SPEC(v) || v >= 4000 || v <= -4000;
}
inline lv_coord_t up(lv_coord_t v, float f) {
  if (!g_pi_native || special(v)) return v;
  return (lv_coord_t)lroundf(v * f);
}
// Lengths round down, so a box placed at a rounded position never pokes one
// pixel past its parent's edge and makes it scroll.
inline lv_coord_t len(lv_coord_t v, float f) {
  if (!g_pi_native || special(v)) return v;
  return (lv_coord_t)floorf(v * f + 0.01f);
}
inline lv_coord_t down(lv_coord_t v, float f) {
  if (!g_pi_native || special(v)) return v;
  return (lv_coord_t)lroundf(v / f);
}
inline lv_coord_t X(lv_coord_t v) { return up(v, g_pi_sx); }
inline lv_coord_t Y(lv_coord_t v) { return up(v, g_pi_sy); }
inline lv_coord_t S(lv_coord_t v) { return up(v, g_pi_s); }
inline lv_coord_t W(lv_coord_t v) { return len(v, g_pi_sx); }
inline lv_coord_t H(lv_coord_t v) { return len(v, g_pi_sy); }
inline lv_coord_t P(lv_coord_t v) { return len(v, g_pi_s); }

// Small squares (icons, dots, swatches) stay square.
inline void setSize(lv_obj_t* o, lv_coord_t w, lv_coord_t h) {
  if (g_pi_native && w == h && !special(w) && w <= 80)
    lv_obj_set_size(o, P(w), P(h));
  else
    lv_obj_set_size(o, W(w), H(h));
}
inline lv_coord_t lineHeight(const lv_font_t* f) {
  return down(lv_font_get_line_height(piNativeFont(f)), g_pi_sy);
}
inline lv_coord_t txtWidth(const char* t, uint32_t n, const lv_font_t* f,
                           lv_coord_t ls, lv_text_flag_t fl) {
  return down(lv_txt_get_width(t, n, piNativeFont(f), S(ls), fl), g_pi_sx);
}
inline void txtSize(lv_point_t* p, const char* t, const lv_font_t* f, lv_coord_t ls,
                    lv_coord_t lsp, lv_coord_t maxw, lv_text_flag_t fl) {
  lv_txt_get_size(p, t, piNativeFont(f), S(ls), S(lsp), X(maxw), fl);
  p->x = down(p->x, g_pi_sx);
  p->y = down(p->y, g_pi_sy);
}
inline void indevPoint(const lv_indev_t* i, lv_point_t* p) {
  lv_indev_get_point(i, p);
  p->x = down(p->x, g_pi_sx);
  p->y = down(p->y, g_pi_sy);
}
inline void coords(const lv_obj_t* o, lv_area_t* a) {
  lv_obj_get_coords(o, a);
  a->x1 = down(a->x1, g_pi_sx); a->x2 = down(a->x2, g_pi_sx);
  a->y1 = down(a->y1, g_pi_sy); a->y2 = down(a->y2, g_pi_sy);
}
}  // namespace pinat

// The close (X) and back (<) buttons in the FilamentLager look: dark box with
// a frame in the accent colour, like the card's close button, instead of the
// red one. Restyled once the screen code has finished with them.
void piHeaderButtonRestyle(lv_obj_t* label, const char* text);
namespace pinat {
inline void labelText(lv_obj_t* l, const char* t) {
  lv_label_set_text(l, t);
  piHeaderButtonRestyle(l, t);
}
}  // namespace pinat

// ---- setters: logical -> display pixels ----------------------------------------
#define lv_label_set_text(l, t)         pinat::labelText((l), (t))
#define lv_obj_set_size(o, w, h)        pinat::setSize((o), (w), (h))
#define lv_obj_set_pos(o, x, y)         lv_obj_set_pos((o), pinat::X(x), pinat::Y(y))
#define lv_obj_set_x(o, x)              lv_obj_set_x((o), pinat::X(x))
#define lv_obj_set_y(o, y)              lv_obj_set_y((o), pinat::Y(y))
#define lv_obj_set_width(o, w)          lv_obj_set_width((o), pinat::W(w))
#define lv_obj_set_height(o, h)         lv_obj_set_height((o), pinat::H(h))
#define lv_obj_align(o, a, x, y)        lv_obj_align((o), (a), pinat::X(x), pinat::Y(y))
#define lv_obj_align_to(o, b, a, x, y)  lv_obj_align_to((o), (b), (a), pinat::X(x), pinat::Y(y))
#define lv_obj_scroll_to_y(o, y, an)    lv_obj_scroll_to_y((o), pinat::Y(y), (an))
#define lv_obj_scroll_by(o, x, y, an)   lv_obj_scroll_by((o), pinat::X(x), pinat::Y(y), (an))
#define lv_obj_set_ext_click_area(o, v) lv_obj_set_ext_click_area((o), pinat::S(v))
#define lv_qrcode_create(p, s, d, l)    lv_qrcode_create((p), pinat::S(s), (d), (l))

#define lv_obj_set_style_radius(o, v, s)        lv_obj_set_style_radius((o), pinat::S(v), (s))
#define lv_obj_set_style_border_width(o, v, s)  lv_obj_set_style_border_width((o), pinat::S(v), (s))
#define lv_obj_set_style_outline_width(o, v, s) lv_obj_set_style_outline_width((o), pinat::S(v), (s))
#define lv_obj_set_style_shadow_width(o, v, s)  lv_obj_set_style_shadow_width((o), pinat::S(v), (s))
#define lv_obj_set_style_shadow_spread(o, v, s) lv_obj_set_style_shadow_spread((o), pinat::S(v), (s))
#define lv_obj_set_style_shadow_ofs_x(o, v, s)  lv_obj_set_style_shadow_ofs_x((o), pinat::S(v), (s))
#define lv_obj_set_style_shadow_ofs_y(o, v, s)  lv_obj_set_style_shadow_ofs_y((o), pinat::S(v), (s))
#define lv_obj_set_style_arc_width(o, v, s)     lv_obj_set_style_arc_width((o), pinat::S(v), (s))
#define lv_obj_set_style_line_width(o, v, s)    lv_obj_set_style_line_width((o), pinat::S(v), (s))
#define lv_obj_set_style_pad_all(o, v, s)       lv_obj_set_style_pad_all((o), pinat::P(v), (s))
#define lv_obj_set_style_pad_left(o, v, s)      lv_obj_set_style_pad_left((o), pinat::P(v), (s))
#define lv_obj_set_style_pad_right(o, v, s)     lv_obj_set_style_pad_right((o), pinat::P(v), (s))
#define lv_obj_set_style_pad_top(o, v, s)       lv_obj_set_style_pad_top((o), pinat::P(v), (s))
#define lv_obj_set_style_pad_bottom(o, v, s)    lv_obj_set_style_pad_bottom((o), pinat::P(v), (s))
#define lv_obj_set_style_pad_hor(o, v, s)       lv_obj_set_style_pad_hor((o), pinat::P(v), (s))
#define lv_obj_set_style_pad_ver(o, v, s)       lv_obj_set_style_pad_ver((o), pinat::P(v), (s))
#define lv_obj_set_style_pad_row(o, v, s)       lv_obj_set_style_pad_row((o), pinat::P(v), (s))
#define lv_obj_set_style_pad_column(o, v, s)    lv_obj_set_style_pad_column((o), pinat::P(v), (s))
#define lv_obj_set_style_pad_gap(o, v, s)       lv_obj_set_style_pad_gap((o), pinat::P(v), (s))
#define lv_obj_set_style_max_width(o, v, s)     lv_obj_set_style_max_width((o), pinat::W(v), (s))
#define lv_obj_set_style_text_letter_space(o, v, s) lv_obj_set_style_text_letter_space((o), pinat::S(v), (s))
#define lv_obj_set_style_text_line_space(o, v, s)   lv_obj_set_style_text_line_space((o), pinat::S(v), (s))
#define lv_obj_set_style_text_font(o, f, s)     lv_obj_set_style_text_font((o), piNativeFont(f), (s))
#define lv_theme_default_init(d, p, s, dk, f)   lv_theme_default_init((d), (p), (s), (dk), piNativeFont(f))

// ---- getters: display pixels -> logical ----------------------------------------
#define lv_obj_get_width(o)        pinat::down(lv_obj_get_width(o), g_pi_sx)
#define lv_obj_get_height(o)       pinat::down(lv_obj_get_height(o), g_pi_sy)
#define lv_obj_get_x(o)            pinat::down(lv_obj_get_x(o), g_pi_sx)
#define lv_obj_get_y(o)            pinat::down(lv_obj_get_y(o), g_pi_sy)
#define lv_obj_get_scroll_y(o)     pinat::down(lv_obj_get_scroll_y(o), g_pi_sy)
#define lv_obj_get_style_radius(o, p) pinat::down(lv_obj_get_style_radius((o), (p)), g_pi_s)
#define lv_obj_get_coords(o, a)    pinat::coords((o), (a))
#define lv_font_get_line_height(f) pinat::lineHeight(f)
#define lv_txt_get_width(t, n, f, ls, fl) pinat::txtWidth((t), (n), (f), (ls), (fl))
#define lv_txt_get_size(p, t, f, ls, lsp, mw, fl) pinat::txtSize((p), (t), (f), (ls), (lsp), (mw), (fl))
#define lv_indev_get_point(i, p)   pinat::indevPoint((i), (p))

#undef LV_HOR_RES
#undef LV_VER_RES
#define LV_HOR_RES pinat::down(lv_disp_get_hor_res(lv_disp_get_default()), g_pi_sx)
#define LV_VER_RES pinat::down(lv_disp_get_ver_res(lv_disp_get_default()), g_pi_sy)

#endif  // __cplusplus
