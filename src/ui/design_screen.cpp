// "Design": the settings of the Home Assistant FilamentLager card, on the
// scale - colour of the frames, frame width, list view, colour names, the
// silica threshold - plus the scale's own full screen switch. The card keeps
// them in Home Assistant ("Gilt auf allen Geräten"), so a change here shows on
// the phone and the other way round. Reached and written through the bridge
// (pi/ha_bridge, /api/v1/design).
//
// Pi only, Home Assistant mode only.
#ifdef SPOOLMANSCALE_PI

#include "design_screen.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <lvgl.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "services/spoolman_api.h"

#include "app/app_state.h"
#include "app/deferred_actions.h"
#include "ha_design.h"
#include "hardware/sd_logger.h"
#include "navigation.h"
#include "services/backend.h"
#include "settings_screen.h"
#include "theme.h"
#include "ui_common.h"

void resetActivityTimer();
bool piNativeSaved();
void piNativeSave(bool on);
extern bool g_pi_native;

namespace {

// The card's choice of frame colours (FIL_AKZENT).
const uint32_t kAccents[] = { 0x4caf50, 0x00bcd4, 0x2196f3, 0x3f51b5, 0x9c27b0, 0xe91e63,
                              0xf44336, 0xff9800, 0xffc107, 0x8bc34a, 0x9e9e9e, 0xffffff };
const int kAccentCount = sizeof(kAccents) / sizeof(kAccents[0]);

lv_obj_t* s_scr = nullptr;
lv_obj_t* s_msg = nullptr;
HaDesign s_d;
int s_silica = -1;
bool s_loaded = false;
bool s_native = false;
bool s_restart = false;   // colour or screen mode changed: the palette is read at boot
std::string s_err;

int luma(uint32_t c) {
  return (int)(0.299f * ((c >> 16) & 0xFF) + 0.587f * ((c >> 8) & 0xFF) + 0.114f * (c & 0xFF));
}

void fromJson(JsonObjectConst o) {
  const char* f = o["farbe"] | "";
  if (strlen(f) == 7 && f[0] == '#') s_d.accent = (uint32_t)strtoul(f + 1, nullptr, 16);
  s_d.frame = o["rand"] | 2;
  if (s_d.frame < 1 || s_d.frame > 3) s_d.frame = 2;
  s_d.compact = o["kompakt"] | false;
  s_d.bilingual = o["zweisprachig"] | true;
  s_silica = o["silica"].is<int>() ? (int)o["silica"] : -1;
}

void load() {
  s_d = haDesign();
  s_native = piNativeSaved();
  JsonDocument doc;
  DeserializationError de;
  const int code = spoolmanGetJson(backendBaseUrl(), "/api/v1/design", doc, 10000, nullptr, &de);
  if (code == 200 && !de) {
    fromJson(doc.as<JsonObjectConst>());
    if (haDesignStore(s_d)) s_restart = s_d.accent != haDesignBootAccent();
    s_err.clear();
  } else {
    s_err = "Home Assistant nicht erreichbar - Änderungen gelten nur auf der Waage.";
    logSDf("Design: load failed, code=%d", code);
  }
  s_loaded = true;
}

// One change to Home Assistant, as the card writes it.
void save(const char* json_body) {
  HTTPClient http;
  http.begin(String(backendBaseUrl()) + "/api/v1/design");
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000);
  const int code = http.PUT(String(json_body));
  if (code == 200) {
    JsonDocument doc;
    if (!deserializeJson(doc, http.getString())) fromJson(doc.as<JsonObjectConst>());
    s_err.clear();
  } else {
    s_err = "Nicht in Home Assistant gespeichert (HTTP " + std::to_string(code) + "), nur auf der Waage.";
  }
  http.end();
  logSDf("Design: %s -> HTTP %d", json_body, code);
  if (haDesignStore(s_d)) s_restart = s_d.accent != haDesignBootAccent();
}

void build();

// ---- building blocks ------------------------------------------------------------

lv_obj_t* plain(lv_obj_t* parent) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_shadow_width(o, 0, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

void section(lv_obj_t* parent, const char* icon, const char* title) {
  lv_obj_t* l = lv_label_create(parent);
  std::string t = std::string(icon) + "  " + title;
  lv_label_set_text(l, t.c_str());
  lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_CAPTION), 0);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_ext_12, 0);
  lv_obj_set_style_pad_top(l, 6, 0);
}

lv_obj_t* row(lv_obj_t* parent, int h) {
  lv_obj_t* r = plain(parent);
  lv_obj_set_size(r, LV_PCT(100), h);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(r, 6, 0);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  return r;
}

// A segment button as the card draws it: a pill, filled with the accent when on.
lv_obj_t* seg(lv_obj_t* parent, int w, const char* text, bool on, lv_event_cb_t cb, intptr_t ud) {
  lv_obj_t* b = lv_btn_create(parent);
  lv_obj_set_size(b, w, 32);
  lv_obj_set_style_radius(b, 16, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_color(b, lv_color_hex(on ? s_d.accent : 0x555555), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(on ? s_d.accent : UI_COL_GROUND), 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_ext_14, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(on ? (luma(s_d.accent) > 170 ? 0x111111 : 0xffffff)
                                                 : UI_COL_INK), 0);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void*)ud);
  return b;
}

void swatch(lv_obj_t* parent, uint32_t c) {
  const bool on = c == s_d.accent;
  lv_obj_t* b = lv_btn_create(parent);
  lv_obj_set_size(b, 30, 30);
  lv_obj_set_style_radius(b, 8, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(c), 0);
  lv_obj_set_style_border_width(b, on ? 3 : 1, 0);
  lv_obj_set_style_border_color(b, lv_color_hex(on ? 0xffffff : 0x555555), 0);
  lv_obj_add_event_cb(b, [](lv_event_t* e) {
    const uint32_t c2 = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    char body[40];
    snprintf(body, sizeof(body), "{\"farbe\":\"#%06lx\"}", (unsigned long)c2);
    s_d.accent = c2;
    save(body);
    build();
  }, LV_EVENT_CLICKED, (void*)(uintptr_t)c);
}

// ---- the page --------------------------------------------------------------------

void build() {
  releaseScreen(&s_scr);
  s_scr = buildOverlayScreen();
  lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_HIDDEN);
  buildSubHeader(s_scr, "Design", [](lv_event_t*) {
    logSD("BTN: Design -> Back");
    const bool restart = s_restart;
    designScreenClose();
    showSettingsScreen();
    // Colours and the screen mode are read at the start; asked like a
    // language change.
    if (restart) show_reboot_pending = true;
  });

  lv_obj_t* sub = lv_label_create(s_scr);
  lv_label_set_text(sub, s_err.empty() ? "Gilt auch in Home Assistant auf allen Geräten" : s_err.c_str());
  lv_obj_set_style_text_color(sub, lv_color_hex(s_err.empty() ? UI_COL_CAPTION : UI_COL_WARN), 0);
  lv_obj_set_style_text_font(sub, &lv_font_montserrat_ext_12, 0);
  lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);
  lv_obj_set_width(sub, 460);
  lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 46);

  lv_obj_t* list = lv_obj_create(s_scr);
  lv_obj_set_size(list, 464, 252);
  lv_obj_set_pos(list, 8, 64);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, s_d.frame, 0);
  lv_obj_set_style_border_color(list, lv_color_hex(s_d.accent), 0);
  lv_obj_set_style_radius(list, 14, 0);
  lv_obj_set_style_pad_all(list, 10, 0);
  lv_obj_set_style_pad_row(list, 6, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);

  section(list, LV_SYMBOL_IMAGE, "FARBE DER UMRANDUNGEN");
  {
    lv_obj_t* r = row(list, 34);
    lv_obj_set_style_pad_column(r, 4, 0);
    bool known = false;
    for (int i = 0; i < kAccentCount; i++) {
      swatch(r, kAccents[i]);
      if (kAccents[i] == s_d.accent) known = true;
    }
    if (!known) swatch(r, s_d.accent);   // an own colour chosen in the card
  }

  section(list, LV_SYMBOL_STOP, "RAHMENSTÄRKE");
  {
    lv_obj_t* r = row(list, 34);
    const char* names[] = { "Dünn", "Normal", "Kräftig" };
    for (int v = 1; v <= 3; v++)
      seg(r, 136, names[v - 1], s_d.frame == v, [](lv_event_t* e) {
        const int v2 = (int)(intptr_t)lv_event_get_user_data(e);
        char body[24];
        snprintf(body, sizeof(body), "{\"rand\":%d}", v2);
        s_d.frame = (uint8_t)v2;
        save(body);
        build();
      }, v);
  }

  section(list, LV_SYMBOL_LIST, "ANSICHT DER LISTE");
  {
    lv_obj_t* r = row(list, 34);
    for (int v = 0; v <= 1; v++)
      seg(r, 136, v ? "Kompakt" : "Normal", s_d.compact == (v == 1), [](lv_event_t* e) {
        const int v2 = (int)(intptr_t)lv_event_get_user_data(e);
        s_d.compact = v2 == 1;
        save(v2 ? "{\"kompakt\":true}" : "{\"kompakt\":false}");
        build();
      }, v);
  }

  section(list, LV_SYMBOL_EDIT, "FARBNAMEN");
  {
    lv_obj_t* r = row(list, 34);
    for (int v = 0; v <= 1; v++)
      seg(r, v ? 190 : 136, v ? "Deutsch + Englisch" : "Nur Original", s_d.bilingual == (v == 1),
          [](lv_event_t* e) {
            const int v2 = (int)(intptr_t)lv_event_get_user_data(e);
            s_d.bilingual = v2 == 1;
            save(v2 ? "{\"zweisprachig\":true}" : "{\"zweisprachig\":false}");
            build();
          }, v);
  }

  if (s_silica >= 0) {
    section(list, LV_SYMBOL_WARNING, "SILICA-WARNSCHWELLE");
    lv_obj_t* r = row(list, 34);
    auto step = [](lv_event_t* e) {
      const int d = (int)(intptr_t)lv_event_get_user_data(e);
      const int v = s_silica + d;
      if (v < 15 || v > 60) return;
      char body[24];
      snprintf(body, sizeof(body), "{\"silica\":%d}", v);
      s_silica = v;
      save(body);
      build();
    };
    seg(r, 52, "-", false, step, -1);
    lv_obj_t* val = lv_label_create(r);
    char b[16];
    snprintf(b, sizeof(b), "%d %%", s_silica);
    lv_label_set_text(val, b);
    lv_obj_set_width(val, 70);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(val, lv_color_hex(UI_COL_INK), 0);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_ext_18, 0);
    seg(r, 52, "+", false, step, 1);
    lv_obj_t* hint = lv_label_create(r);
    lv_label_set_text(hint, "ab hier wird ein AMS rot");
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_COL_CAPTION), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_ext_12, 0);
  }

  section(list, LV_SYMBOL_EYE_OPEN, "BILDSCHIRM DER WAAGE");
  {
    lv_obj_t* r = row(list, 34);
    for (int v = 1; v >= 0; v--)
      seg(r, v ? 190 : 190, v ? "Vollbild, scharf" : "Klassisch (480x320)", s_native == (v == 1),
          [](lv_event_t* e) {
            const bool on = (int)(intptr_t)lv_event_get_user_data(e) == 1;
            if (on == s_native) return;
            s_native = on;
            piNativeSave(on);
            s_restart = true;
            logSDf("Design: screen mode -> %s", on ? "full" : "classic");
            build();
          }, v);
  }

  {
    lv_obj_t* r = row(list, 40);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    seg(r, 240, "Auf Standard zurücksetzen", false, [](lv_event_t*) {
      s_d = HaDesign();
      save("{\"farbe\":\"#4caf50\",\"rand\":2,\"kompakt\":false,\"zweisprachig\":true}");
      build();
    }, 0);
  }
  if (s_restart) {
    lv_obj_t* l = lv_label_create(list);
    lv_label_set_text(l, "Farbe und Bildschirm gelten nach einem Neustart - wird beim Verlassen angeboten.");
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_WARN), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_ext_12, 0);
  }
  resetActivityTimer();
}

}  // namespace

void showDesignScreen() {
  logSD("SHOW: DesignScreen");
  hideAllOverlays();
  s_restart = false;
  load();
  build();
}

void designScreenClose() {
  if (!s_scr) return;
  lv_obj_add_flag(s_scr, LV_OBJ_FLAG_HIDDEN);
  releaseScreen(&s_scr);
}

#endif  // SPOOLMANSCALE_PI
