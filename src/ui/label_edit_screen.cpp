#include "label_edit_screen.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include <cstdio>
#include <cstring>

#include "app/app_state.h"
#include "app/deferred_actions.h"
#include "hardware/sd_logger.h"
#include "lang.h"
#include "navigation.h"
#include "printer_screen.h"
#include "product_name.h"
#include "services/label_printer.h"
#include "services/label_render.h"
#include "theme.h"
#include "ui/info_popup.h"
#include "ui_common.h"

void resetActivityTimer();

namespace {

enum Mode { M_SPOOL, M_FREE, M_TEMPLATE };
enum Field { F_TOP, F_BAND, F_TITLE, F_L0, F_L1, F_L2, F_L3, F_QR, F_FOOTER, F_NONE };

const char* const kFieldName[] = {
  "Oben (Hersteller)", "Balken (Material)", "Titel (Name)",
  "Zeile 1", "Zeile 2", "Zeile 3", "Zeile 4", "QR-Code Inhalt", "Text der letzten Zeile",
};

lv_obj_t* s_scr = nullptr;      // the editor
lv_obj_t* s_kb_scr = nullptr;   // the keyboard page
lv_obj_t* s_ta = nullptr;
Mode s_mode = M_FREE;
Field s_field = F_NONE;
LabelDoc s_doc{};
SpoolLabelData s_spool{};
LabelTemplate s_tpl{};
bool s_spool_req = false;
bool s_print_req = false;

// The preview's pixels, rebuilt with every change. LVGL caches decoded
// images by source, so a new picture under the same descriptor is announced.
lv_img_dsc_t s_img{};
lv_color_t* s_img_buf = nullptr;

// What a template is shown on: one spool everybody knows.
SpoolLabelData sampleSpool() {
  SpoolLabelData d{};
  d.id = 42;
  snprintf(d.vendor, sizeof(d.vendor), "Bambu Lab");
  snprintf(d.material, sizeof(d.material), "PLA");
  snprintf(d.name, sizeof(d.name), "PLA Basic Jade White");
  snprintf(d.color, sizeof(d.color), "FFFFFF");
  snprintf(d.date, sizeof(d.date), "21.10.2026");
  return d;
}

LabelPrinterConfig previewConfig() {
  LabelPrinterConfig c = labelPrinterLoadConfig();
  if (c.model == LP_MODEL_NONE) c.model = LP_MODEL_M220;
  if (!c.media_width_mm || !c.media_length_mm) { c.media_width_mm = 40; c.media_length_mm = 30; }
  return c;
}

char* fieldText(Field f, size_t* cap) {
  switch (f) {
    case F_TOP:    *cap = sizeof(s_doc.top);   return s_doc.top;
    case F_BAND:   *cap = sizeof(s_doc.band);  return s_doc.band;
    case F_TITLE:  *cap = sizeof(s_doc.title); return s_doc.title;
    case F_L0: case F_L1: case F_L2: case F_L3:
      *cap = sizeof(s_doc.lines[0]); return s_doc.lines[f - F_L0];
    case F_QR:     *cap = sizeof(s_doc.qr);    return s_doc.qr;
    case F_FOOTER: *cap = sizeof(s_tpl.footer); return s_tpl.footer;
    default:       *cap = 0; return nullptr;
  }
}

void refreshTemplateDoc() {
  s_spool = sampleSpool();
  labelDocFromSpool(s_spool, &s_doc);
}

void build();
void buildKeyboard();

// ---- the preview -----------------------------------------------------------------

// The label in grey levels, each preview pixel the share of black dots under it.
void preview(lv_obj_t* box) {
  lv_obj_update_layout(box);
  const int bw = lv_obj_get_content_width(box);    // real pixels, not scaled
  const int bh = lv_obj_get_content_height(box);
  const LabelPrinterConfig c = previewConfig();
  LabelRaster r{};
  if (bw < 8 || bh < 8 || !labelRenderDoc(c, s_doc, &r)) {
    lv_obj_t* l = lv_label_create(box);
    lv_label_set_text(l, "Keine Vorschau");
    lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_CAPTION), 0);
    lv_obj_center(l);
    labelRasterFree(&r);
    return;
  }
  const int x0 = labelPrinterContentX(c);
  const int cw = r.content_width ? r.content_width : r.width;
  const int ch = r.height;
  // Fit, keeping the label's shape.
  int tw = bw, th = (int)((long)ch * bw / cw);
  if (th > bh) { th = bh; tw = (int)((long)cw * bh / ch); }
  if (s_img_buf) { heap_caps_free(s_img_buf); s_img_buf = nullptr; }
  s_img_buf = (lv_color_t*)heap_caps_malloc((size_t)tw * th * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  if (!s_img_buf) { labelRasterFree(&r); return; }
  for (int ty = 0; ty < th; ty++) {
    const int sy0 = ty * ch / th, sy1 = (ty + 1) * ch / th > sy0 ? (ty + 1) * ch / th : sy0 + 1;
    for (int tx = 0; tx < tw; tx++) {
      const int sx0 = tx * cw / tw, sx1 = (tx + 1) * cw / tw > sx0 ? (tx + 1) * cw / tw : sx0 + 1;
      int black = 0, all = 0;
      for (int sy = sy0; sy < sy1 && sy < ch; sy++) {
        const uint8_t* row = r.pixels + (size_t)sy * r.row_bytes;
        for (int sx = sx0; sx < sx1 && sx < cw; sx++) {
          const int x = x0 + sx;
          black += (row[x >> 3] >> (7 - (x & 7))) & 1;
          all++;
        }
      }
      const uint8_t g = all ? (uint8_t)(255 - black * 255 / all) : 255;
      s_img_buf[(size_t)ty * tw + tx] = lv_color_make(g, g, g);
    }
  }
  labelRasterFree(&r);
  s_img = lv_img_dsc_t{};
  s_img.header.always_zero = 0;
  s_img.header.w = tw;
  s_img.header.h = th;
  s_img.header.cf = LV_IMG_CF_TRUE_COLOR;
  s_img.data_size = (uint32_t)tw * th * sizeof(lv_color_t);
  s_img.data = (const uint8_t*)s_img_buf;
  lv_img_cache_invalidate_src(&s_img);
  lv_obj_t* img = lv_img_create(box);
  lv_img_set_src(img, &s_img);
  lv_obj_center(img);
}

// ---- rows ---------------------------------------------------------------------------

lv_obj_t* rowBase(lv_obj_t* list, int h) {
  lv_obj_t* b = lv_btn_create(list);
  lv_obj_set_size(b, LV_PCT(100), h);
  lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_ROW), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_ROW_PRESS_FILL), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_color(b, lv_color_hex(UI_COL_LINE), 0);
  lv_obj_set_style_radius(b, 8, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  return b;
}

void caption(lv_obj_t* b, const char* text) {
  lv_obj_t* c = lv_label_create(b);
  lv_label_set_text(c, text);
  lv_obj_set_style_text_font(c, &lv_font_montserrat_ext_12, 0);
  lv_obj_set_style_text_color(c, lv_color_hex(UI_COL_CAPTION), 0);
  lv_obj_set_pos(c, 10, 4);
}

// A line of the label: its name and its text, a tap opens the keyboard.
void textRow(lv_obj_t* list, Field f) {
  size_t cap;
  const char* t = fieldText(f, &cap);
  lv_obj_t* b = rowBase(list, 44);
  caption(b, kFieldName[f]);
  lv_obj_t* v = lv_label_create(b);
  const bool empty = !t || !t[0];
  lv_label_set_text(v, empty ? "leer - antippen" : t);
  lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
  lv_obj_set_width(v, 200);
  lv_obj_set_style_text_font(v, &lv_font_montserrat_ext_14, 0);
  lv_obj_set_style_text_color(v, lv_color_hex(empty ? UI_COL_CAPTION : UI_COL_INK), 0);
  lv_obj_set_pos(v, 10, 21);
  lv_obj_t* ic = lv_label_create(b);
  lv_label_set_text(ic, LV_SYMBOL_EDIT);
  lv_obj_set_style_text_color(ic, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_align(ic, LV_ALIGN_RIGHT_MID, -8, 0);
  lv_obj_add_event_cb(b, [](lv_event_t* e) {
    s_field = (Field)(intptr_t)lv_event_get_user_data(e);
    logSDf("BTN: Label edit -> %s", kFieldName[s_field]);
    buildKeyboard();
  }, LV_EVENT_CLICKED, (void*)(intptr_t)f);
}

// An on/off line. `flag` points into the template or the label.
void toggleRow(lv_obj_t* list, const char* title, bool* flag) {
  lv_obj_t* b = rowBase(list, 40);
  lv_obj_t* t = lv_label_create(b);
  lv_label_set_text(t, title);
  lv_obj_set_style_text_font(t, &lv_font_montserrat_ext_14, 0);
  lv_obj_set_style_text_color(t, lv_color_hex(UI_COL_INK), 0);
  lv_obj_align(t, LV_ALIGN_LEFT_MID, 10, 0);
  lv_obj_t* pill = lv_obj_create(b);
  lv_obj_set_size(pill, 50, 24);
  lv_obj_align(pill, LV_ALIGN_RIGHT_MID, -8, 0);
  lv_obj_clear_flag(pill, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_set_style_radius(pill, 12, 0);
  lv_obj_set_style_border_width(pill, 1, 0);
  lv_obj_set_style_border_color(pill, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_set_style_bg_color(pill, lv_color_hex(*flag ? UI_COL_ACCENT : UI_COL_ROW), 0);
  lv_obj_set_style_pad_all(pill, 0, 0);
  lv_obj_t* pl = lv_label_create(pill);
  lv_label_set_text(pl, *flag ? "An" : "Aus");
  lv_obj_set_style_text_font(pl, &lv_font_montserrat_ext_12, 0);
  lv_obj_set_style_text_color(pl, lv_color_hex(*flag ? UI_COL_ON_ACCENT : UI_COL_CAPTION), 0);
  lv_obj_center(pl);
  lv_obj_add_event_cb(b, [](lv_event_t* e) {
    bool* f = (bool*)lv_event_get_user_data(e);
    *f = !*f;
    if (s_mode == M_TEMPLATE) {
      if (!labelTemplateSave(s_tpl)) showInfoPopup(STR_PRN_TITLE, STR_ERR_SAVE, INFO_WARN);
      refreshTemplateDoc();
    }
    build();
  }, LV_EVENT_CLICKED, flag);
}

void actionRow(lv_obj_t* list, const char* icon, const char* title, lv_event_cb_t cb) {
  lv_obj_t* b = rowBase(list, 36);
  lv_obj_t* t = lv_label_create(b);
  char buf[64];
  snprintf(buf, sizeof(buf), "%s  %s", icon, title);
  lv_label_set_text(t, buf);
  lv_obj_set_style_text_font(t, &lv_font_montserrat_ext_14, 0);
  lv_obj_set_style_text_color(t, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_center(t);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
}

// ---- the editor ---------------------------------------------------------------------

void goBack() {
  const Mode m = s_mode;
  labelEditClose();
  if (m == M_SPOOL) hideAllOverlays();
  else show_printer_pending = true;
}

void build() {
  releaseScreen(&s_scr);
  s_scr = buildOverlayScreen();
  lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_HIDDEN);
  const char* title = s_mode == M_TEMPLATE ? "Etikett-Vorlage"
                    : s_mode == M_FREE ? "Freies Etikett" : "Etikett";
  buildSubHeader(s_scr, title, [](lv_event_t*) {
    logSD("BTN: Label edit -> Back");
    goBack();
  });

  // Left: the label as it prints.
  lv_obj_t* box = lv_obj_create(s_scr);
  lv_obj_set_size(box, 224, 172);
  lv_obj_set_pos(box, 8, 52);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(box, lv_color_hex(UI_COL_EMPTY), 0);
  lv_obj_set_style_border_width(box, 1, 0);
  lv_obj_set_style_border_color(box, lv_color_hex(UI_COL_LINE), 0);
  lv_obj_set_style_radius(box, 10, 0);
  lv_obj_set_style_pad_all(box, 8, 0);
  preview(box);

  const LabelPrinterConfig c = previewConfig();
  lv_obj_t* info = lv_label_create(s_scr);
  char ib[80];
  snprintf(ib, sizeof(ib), "%u x %u mm  %s%s", (unsigned)c.media_width_mm,
           (unsigned)c.media_length_mm, labelPrinterProfile(c.model).name,
           s_mode == M_TEMPLATE ? "  -  Beispiel" : "");
  lv_label_set_text(info, ib);
  lv_obj_set_style_text_font(info, &lv_font_montserrat_ext_12, 0);
  lv_obj_set_style_text_color(info, lv_color_hex(UI_COL_CAPTION), 0);
  lv_obj_set_width(info, 224);
  lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(info, 8, 230);

  lv_obj_t* pb = lv_btn_create(s_scr);
  lv_obj_set_size(pb, 224, 50);
  lv_obj_set_pos(pb, 8, 262);
  lv_obj_set_style_bg_color(pb, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_set_style_radius(pb, 10, 0);
  lv_obj_set_style_shadow_width(pb, 0, 0);
  lv_obj_t* pl = lv_label_create(pb);
  lv_label_set_text(pl, s_mode == M_TEMPLATE ? LV_SYMBOL_IMAGE "  Probe drucken"
                                             : LV_SYMBOL_IMAGE "  Drucken");
  lv_obj_set_style_text_font(pl, &lv_font_montserrat_ext_18, 0);
  lv_obj_set_style_text_color(pl, lv_color_hex(UI_COL_ON_ACCENT), 0);
  lv_obj_center(pl);
  lv_obj_add_event_cb(pb, [](lv_event_t*) {
    logSD("BTN: Label edit -> print");
    s_print_req = true;
  }, LV_EVENT_CLICKED, nullptr);

  // Right: the lines.
  lv_obj_t* list = lv_obj_create(s_scr);
  lv_obj_set_size(list, 232, 264);
  lv_obj_set_pos(list, 240, 50);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 2, 0);
  lv_obj_set_style_pad_row(list, 5, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);

  if (s_mode == M_TEMPLATE) {
    toggleRow(list, "Spulennummer", &s_tpl.id);
    toggleRow(list, "Farbe", &s_tpl.color);
    toggleRow(list, "Datum", &s_tpl.date);
    toggleRow(list, "Letzte Zeile", &s_tpl.footer_on);
    if (s_tpl.footer_on) textRow(list, F_FOOTER);
    toggleRow(list, "QR-Code", &s_tpl.qr);
    actionRow(list, LV_SYMBOL_REFRESH, "Standard", [](lv_event_t*) {
      LabelTemplate t{};
      t.id = t.color = t.date = t.footer_on = t.qr = true;
      snprintf(t.footer, sizeof(t.footer), "%s", FW_PRODUCT_NAME);
      s_tpl = t;
      labelTemplateSave(s_tpl);
      refreshTemplateDoc();
      build();
    });
  } else {
    textRow(list, F_TOP);
    textRow(list, F_BAND);
    textRow(list, F_TITLE);
    for (int i = 0; i < LABEL_EDIT_LINES; i++) textRow(list, (Field)(F_L0 + i));
    toggleRow(list, "QR-Code", &s_doc.show_qr);
    if (s_doc.show_qr || !s_doc.qr[0]) textRow(list, F_QR);
    if (s_mode == M_SPOOL)
      actionRow(list, LV_SYMBOL_REFRESH, "Wie Vorlage", [](lv_event_t*) {
        labelDocFromSpool(s_spool, &s_doc);
        build();
      });
    else
      actionRow(list, LV_SYMBOL_TRASH, "Alles leeren", [](lv_event_t*) {
        labelDocBlank(&s_doc);
        build();
      });
  }
  resetActivityTimer();
}

// ---- the keyboard -------------------------------------------------------------------

// German keys: QWERTZ with the umlauts, ß and the degree sign, the glyphs the
// label fonts carry. The control keys keep LVGL's labels, so its default
// handler switches pages, deletes and confirms.
#define LE_KEY      (LV_BTNMATRIX_CTRL_POPOVER | 4)
#define LE_MAP_BOTTOM   LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
#define LE_CTRL_BOTTOM  LV_BTNMATRIX_CTRL_CHECKED | 2, 8, \
                        LV_BTNMATRIX_CTRL_CHECKED | 2, LV_KEYBOARD_CTRL_BTN_FLAGS | 2
const char* const kb_lc[] = {
  "1#", "q", "w", "e", "r", "t", "z", "u", "i", "o", "p", "\xC3\xBC", LV_SYMBOL_BACKSPACE, "\n",
  "ABC", "a", "s", "d", "f", "g", "h", "j", "k", "l", "\xC3\xB6", "\xC3\xA4", "\n",
  "-", "y", "x", "c", "v", "b", "n", "m", ".", ",", "\xC3\x9F", "\n",
  LE_MAP_BOTTOM
};
const char* const kb_uc[] = {
  "1#", "Q", "W", "E", "R", "T", "Z", "U", "I", "O", "P", "\xC3\x9C", LV_SYMBOL_BACKSPACE, "\n",
  "abc", "A", "S", "D", "F", "G", "H", "J", "K", "L", "\xC3\x96", "\xC3\x84", "\n",
  "_", "Y", "X", "C", "V", "B", "N", "M", ":", ";", "?", "\n",
  LE_MAP_BOTTOM
};
const lv_btnmatrix_ctrl_t kb_ctrl_text[] = {
  LV_KEYBOARD_CTRL_BTN_FLAGS | 5,
  LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY,
  LV_BTNMATRIX_CTRL_CHECKED | 6,
  LV_KEYBOARD_CTRL_BTN_FLAGS | 6,
  LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY,
  LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY,
  LE_CTRL_BOTTOM
};
const char* const kb_spec[] = {
  "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
  "abc", "#", "/", "(", ")", "+", "%", "&", "\xC2\xB0", "=", "\"", "\n",
  "!", "?", "@", "*", ":", ";", "'", "<", ">", "~", "|", "\n",
  LE_MAP_BOTTOM
};
const lv_btnmatrix_ctrl_t kb_ctrl_spec[] = {
  LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY,
  LV_BTNMATRIX_CTRL_CHECKED | 6,
  LV_KEYBOARD_CTRL_BTN_FLAGS | 6,
  LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY,
  LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY, LE_KEY,
  LE_CTRL_BOTTOM
};

void closeKeyboard() {
  s_ta = nullptr;
  releaseScreen(&s_kb_scr);
}

void keyboardDone(lv_event_t*) {
  if (!s_ta) return;
  size_t cap;
  char* dst = fieldText(s_field, &cap);
  if (dst && cap) {
    snprintf(dst, cap, "%s", lv_textarea_get_text(s_ta));
    logSDf("Label edit: %s = \"%s\"", kFieldName[s_field], dst);
  }
  if (s_field == F_FOOTER) {
    if (!labelTemplateSave(s_tpl)) showInfoPopup(STR_PRN_TITLE, STR_ERR_SAVE, INFO_WARN);
    refreshTemplateDoc();
  }
  if (s_field == F_QR) s_doc.show_qr = s_doc.qr[0] != '\0';
  closeKeyboard();
  build();
}

void buildKeyboard() {
  size_t cap;
  const char* cur = fieldText(s_field, &cap);
  if (!cur) return;
  releaseScreen(&s_kb_scr);
  s_kb_scr = buildOverlayScreen();
  lv_obj_clear_flag(s_kb_scr, LV_OBJ_FLAG_HIDDEN);
  buildSubHeader(s_kb_scr, kFieldName[s_field], [](lv_event_t*) {
    logSD("BTN: Label keyboard -> Back");
    closeKeyboard();
  });

  s_ta = lv_textarea_create(s_kb_scr);
  lv_textarea_set_one_line(s_ta, true);
  lv_textarea_set_max_length(s_ta, cap - 1);
  lv_textarea_set_text(s_ta, cur);
  lv_textarea_set_placeholder_text(s_ta, "Text eingeben");
  lv_obj_set_size(s_ta, 364, 44);
  lv_obj_set_pos(s_ta, 8, 54);
  lv_obj_set_style_text_font(s_ta, &lv_font_montserrat_ext_16, 0);
  lv_obj_set_style_text_color(s_ta, lv_color_hex(UI_COL_INK_MAX), 0);
  lv_obj_set_style_text_color(s_ta, lv_color_hex(UI_COL_INK_SOFT), LV_PART_TEXTAREA_PLACEHOLDER);
  lv_obj_set_style_bg_color(s_ta, lv_color_hex(UI_COL_ROW_PRESSED), 0);
  lv_obj_set_style_border_color(s_ta, lv_color_hex(UI_COL_ACCENT), 0);

  lv_obj_t* clr = lv_btn_create(s_kb_scr);
  lv_obj_set_size(clr, 92, 44);
  lv_obj_set_pos(clr, 380, 54);
  lv_obj_set_style_bg_color(clr, lv_color_hex(UI_COL_ROW), 0);
  lv_obj_set_style_border_width(clr, 1, 0);
  lv_obj_set_style_border_color(clr, lv_color_hex(UI_COL_LINE), 0);
  lv_obj_set_style_radius(clr, 8, 0);
  lv_obj_set_style_shadow_width(clr, 0, 0);
  lv_obj_t* cl = lv_label_create(clr);
  lv_label_set_text(cl, LV_SYMBOL_TRASH " Leeren");
  lv_obj_set_style_text_font(cl, &lv_font_montserrat_ext_14, 0);
  lv_obj_set_style_text_color(cl, lv_color_hex(UI_COL_INK), 0);
  lv_obj_center(cl);
  lv_obj_add_event_cb(clr, [](lv_event_t*) { if (s_ta) lv_textarea_set_text(s_ta, ""); },
                      LV_EVENT_CLICKED, nullptr);

  lv_obj_t* kb = lv_keyboard_create(s_kb_scr);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER, (const char**)kb_lc, kb_ctrl_text);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_UPPER, (const char**)kb_uc, kb_ctrl_text);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_SPECIAL, (const char**)kb_spec, kb_ctrl_spec);
  lv_keyboard_set_textarea(kb, s_ta);
  lv_obj_set_size(kb, 480, 206);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(kb, lv_color_hex(UI_COL_EMPTY), 0);
  lv_obj_set_style_border_width(kb, 0, 0);
  lv_obj_set_style_text_font(kb, &lv_font_montserrat_ext_16, 0);
  lv_obj_add_event_cb(kb, keyboardDone, LV_EVENT_READY, nullptr);
  lv_obj_add_event_cb(s_ta, keyboardDone, LV_EVENT_READY, nullptr);
  resetActivityTimer();
}

void open() {
  hideAllOverlays();
  build();
}

}  // namespace

void labelEditRequestSpool() { s_spool_req = true; }

void labelEditOpenTemplate() {
  logSD("SHOW: Label template");
  s_mode = M_TEMPLATE;
  s_tpl = labelTemplateLoad();
  refreshTemplateDoc();
  open();
}

void labelEditOpenFree() {
  logSD("SHOW: Free label");
  s_mode = M_FREE;
  labelDocBlank(&s_doc);
  snprintf(s_doc.top, sizeof(s_doc.top), "%s", FW_PRODUCT_NAME);
  open();
}

void labelEditLoop() {
  if (s_spool_req) {
    s_spool_req = false;
    if (!printerSpoolLabelData(&s_spool)) {
      showInfoPopup(STR_PRN_TITLE, STR_PRN_ERR_NO_SPOOL, INFO_WARN);
      return;
    }
    logSDf("SHOW: Label of spool #%d", s_spool.id);
    s_mode = M_SPOOL;
    labelDocFromSpool(s_spool, &s_doc);
    open();
  }
  if (s_print_req) {
    s_print_req = false;
    printerPrintDoc(s_doc);
  }
}

void labelEditClose() {
  closeKeyboard();
  if (!s_scr) return;
  lv_obj_add_flag(s_scr, LV_OBJ_FLAG_HIDDEN);
  releaseScreen(&s_scr);
}
