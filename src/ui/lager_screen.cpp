// The FilamentLager stock as a list, in the look of the Home Assistant card:
// one card per filament kind with its colour, rolls and open rests, and the
// stock in kg as a coloured badge. Filter chips by material group, the way
// the card's "Bestand aktualisieren" automation groups them.
//
// Pi only: the data comes from the Home Assistant bridge (pi/ha_bridge),
// which is what backendBaseUrl() points at in Home Assistant mode.
#ifdef SPOOLMANSCALE_PI

#include "lager_screen.h"

#include <ArduinoJson.h>
#include <lvgl.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "services/spoolman_api.h"

#include "app/app_state.h"
#include "hardware/sd_logger.h"
#include "navigation.h"
#include "services/backend.h"
#include "settings_screen.h"
#include "theme.h"
#include "ui_common.h"

void resetActivityTimer();

namespace {

struct Entry {
  std::string name, vendor, material;
  uint32_t color = 0x888888;
  int spools = 0, weight = 1000, grams = 0, tags = 0;
  std::vector<int> rests;
};

enum Group { G_ALL, G_PLA, G_PETG, G_ABS, G_ASA, G_TPU, G_OTHER, G_EMPTY, G_COUNT };
const char* const kGroupName[G_COUNT] = { "Alle", "PLA", "PETG", "ABS", "ASA", "TPU", "Andere", "Leer" };

std::vector<Entry> s_items;
lv_obj_t* s_scr = nullptr;
lv_obj_t* s_list = nullptr;
lv_obj_t* s_summary = nullptr;
lv_obj_t* s_chips[G_COUNT] = {};
int s_group = G_ALL;

// Same families as the card's automation: PLA/APLA, PETG/PCTG, ABS, ASA,
// TPU/PEBA - everything else under "Andere".
Group groupOf(const std::string& m) {
  std::string u;
  for (char c : m) u += (char)toupper((unsigned char)c);
  auto starts = [&](const char* p) { return u.compare(0, strlen(p), p) == 0; };
  if (starts("PLA") || u == "APLA") return G_PLA;
  if (starts("PETG") || u == "PCTG") return G_PETG;
  if (starts("ABS")) return G_ABS;
  if (starts("ASA")) return G_ASA;
  if (starts("TPU") || u == "PEBA") return G_TPU;
  return G_OTHER;
}

bool inGroup(const Entry& e, int g) {
  if (g == G_EMPTY) return e.spools <= 0;
  if (e.spools <= 0) return false;
  return g == G_ALL || groupOf(e.material) == g;
}

void fmtKg(char* out, size_t n, int grams) {
  if (grams >= 1000) {
    const int tenth = (grams + 50) / 100;
    snprintf(out, n, "%d,%d kg", tenth / 10, tenth % 10);
  } else {
    snprintf(out, n, "%d g", grams);
  }
}

uint32_t badgeColour(const Entry& e) {
  if (e.grams <= 0 || e.grams < 250) return UI_COL_BAD;
  if (e.grams < e.weight) return UI_COL_WARN;
  return UI_COL_GOOD;
}

int load(std::string& err) {
  s_items.clear();
  JsonDocument doc;
  DeserializationError de;
  const int code = spoolmanGetJson(backendBaseUrl(), "/api/v1/lager", doc, 15000, nullptr, &de);
  if (code != 200 || de) {
    err = code == 503 ? "Home Assistant nicht erreichbar" : code <= 0 ? "Brücke nicht erreichbar"
                                                                       : "Fehler " + std::to_string(code);
    if (doc["detail"].is<const char*>()) err += std::string("\n") + (const char*)doc["detail"];
    return code;
  }
  for (JsonObjectConst o : doc.as<JsonArrayConst>()) {
    Entry e;
    e.name = o["n"] | "";
    e.vendor = o["v"] | "";
    e.material = o["m"] | "";
    const char* c = o["c"] | "";
    if (strlen(c) == 6) e.color = (uint32_t)strtoul(c, nullptr, 16);
    e.spools = o["s"] | 0;
    e.weight = o["w"] | 1000;
    e.grams = o["g"] | 0;
    e.tags = o["t"] | 0;
    for (int r : o["r"].as<JsonArrayConst>()) e.rests.push_back(r);
    s_items.push_back(std::move(e));
  }
  return code;
}

void addRow(const Entry& e) {
  lv_obj_t* row = lv_obj_create(s_list);
  lv_obj_set_size(row, 456, 50);
  lv_obj_set_style_bg_color(row, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_border_color(row, lv_color_hex(UI_COL_LINE), 0);
  lv_obj_set_style_border_width(row, 1, 0);
  lv_obj_set_style_radius(row, 10, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_shadow_width(row, 0, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t* sw = lv_obj_create(row);
  lv_obj_set_size(sw, 24, 24);
  lv_obj_align(sw, LV_ALIGN_LEFT_MID, 10, 0);
  lv_obj_set_style_radius(sw, 6, 0);
  lv_obj_set_style_bg_color(sw, lv_color_hex(e.color), 0);
  lv_obj_set_style_border_color(sw, lv_color_hex(UI_COL_LINE_SOFT), 0);
  lv_obj_set_style_border_width(sw, 1, 0);
  lv_obj_clear_flag(sw, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(sw, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t* name = lv_label_create(row);
  lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
  lv_obj_set_width(name, 316);
  lv_label_set_text(name, e.name.c_str());
  lv_obj_set_style_text_color(name, lv_color_hex(UI_COL_INK), 0);
  lv_obj_set_style_text_font(name, &lv_font_montserrat_ext_14, 0);
  lv_obj_set_pos(name, 44, 6);

  char sub[96];
  if (e.spools <= 0) {
    snprintf(sub, sizeof(sub), "keine Rolle mehr");
  } else {
    int n = snprintf(sub, sizeof(sub), "%d %s", e.spools, e.spools == 1 ? "Rolle" : "Rollen");
    if (!e.rests.empty()) {
      n += snprintf(sub + n, sizeof(sub) - n, " | offen:");
      for (size_t i = 0; i < e.rests.size() && n < (int)sizeof(sub) - 12; i++)
        n += snprintf(sub + n, sizeof(sub) - n, "%s %d g", i ? "," : "", e.rests[i]);
    }
    if (e.tags > 0 && n < (int)sizeof(sub) - 16)
      snprintf(sub + n, sizeof(sub) - n, " | %d Tag%s", e.tags, e.tags == 1 ? "" : "s");
  }
  lv_obj_t* s = lv_label_create(row);
  lv_label_set_long_mode(s, LV_LABEL_LONG_DOT);
  lv_obj_set_width(s, 316);
  lv_label_set_text(s, sub);
  lv_obj_set_style_text_color(s, lv_color_hex(UI_COL_CAPTION), 0);
  lv_obj_set_style_text_font(s, &lv_font_montserrat_ext_12, 0);
  lv_obj_set_pos(s, 44, 28);

  char kg[16];
  if (e.spools <= 0) snprintf(kg, sizeof(kg), "leer");
  else fmtKg(kg, sizeof(kg), e.grams);
  lv_obj_t* badge = lv_obj_create(row);
  lv_obj_set_size(badge, 80, 26);
  lv_obj_align(badge, LV_ALIGN_RIGHT_MID, -10, 0);
  lv_obj_set_style_radius(badge, 13, 0);
  lv_obj_set_style_bg_color(badge, lv_color_hex(badgeColour(e)), 0);
  lv_obj_set_style_border_width(badge, 0, 0);
  lv_obj_set_style_pad_all(badge, 0, 0);
  lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t* bl = lv_label_create(badge);
  lv_label_set_text(bl, kg);
  lv_obj_set_style_text_color(bl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(bl, &lv_font_montserrat_ext_14, 0);
  lv_obj_center(bl);
}

void paintChips() {
  for (int g = 0; g < G_COUNT; g++) {
    if (!s_chips[g]) continue;
    const bool on = g == s_group;
    lv_obj_set_style_bg_color(s_chips[g], lv_color_hex(on ? UI_COL_OK_BG : UI_COL_SURFACE), 0);
    lv_obj_set_style_border_color(s_chips[g], lv_color_hex(on ? UI_COL_OK_BG : UI_COL_LINE), 0);
    lv_obj_t* l = lv_obj_get_child(s_chips[g], 0);
    if (l) lv_obj_set_style_text_color(l, lv_color_hex(on ? UI_COL_OK_TEXT : UI_COL_INK_2), 0);
  }
}

void fillList() {
  lv_obj_clean(s_list);
  int kinds = 0, rolls = 0, grams = 0;
  for (const Entry& e : s_items) {
    if (!inGroup(e, s_group)) continue;
    addRow(e);
    kinds++;
    rolls += e.spools > 0 ? e.spools : 0;
    grams += e.grams;
  }
  char buf[96], kg[16];
  fmtKg(kg, sizeof(kg), grams);
  if (s_group == G_EMPTY)
    snprintf(buf, sizeof(buf), "%d %s ohne Bestand", kinds, kinds == 1 ? "Sorte" : "Sorten");
  else
    snprintf(buf, sizeof(buf), "%d %s | %d Rollen | %s", kinds, kinds == 1 ? "Sorte" : "Sorten", rolls, kg);
  lv_label_set_text(s_summary, buf);
  lv_obj_scroll_to_y(s_list, 0, LV_ANIM_OFF);
  paintChips();
}

void build() {
  releaseScreen(&s_scr);
  s_scr = buildOverlayScreen();
  memset(s_chips, 0, sizeof(s_chips));

  buildSubHeader(s_scr, "Lager", [](lv_event_t*) {
    logSD("BTN: Lager -> Back");
    lagerScreenClose();
    showSettingsScreen();
  });

  s_summary = lv_label_create(s_scr);
  lv_label_set_text(s_summary, "");
  lv_obj_set_style_text_color(s_summary, lv_color_hex(UI_COL_CAPTION), 0);
  lv_obj_set_style_text_font(s_summary, &lv_font_montserrat_ext_12, 0);
  lv_obj_set_pos(s_summary, 12, 86);

  const int CHIP_W = 54, CHIP_GAP = 4;
  for (int g = 0; g < G_COUNT; g++) {
    lv_obj_t* c = lv_btn_create(s_scr);
    s_chips[g] = c;
    lv_obj_set_size(c, CHIP_W, 30);
    lv_obj_set_pos(c, 8 + g * (CHIP_W + CHIP_GAP), 50);
    lv_obj_set_style_radius(c, 15, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_shadow_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_t* l = lv_label_create(c);
    lv_label_set_text(l, kGroupName[g]);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_ext_12, 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(c, [](lv_event_t* e) {
      s_group = (int)(intptr_t)lv_event_get_user_data(e);
      logSDf("BTN: Lager -> %s", kGroupName[s_group]);
      fillList();
      resetActivityTimer();
    }, LV_EVENT_CLICKED, (void*)(intptr_t)g);
  }

  s_list = lv_obj_create(s_scr);
  lv_obj_set_size(s_list, 480, 216);
  lv_obj_set_pos(s_list, 0, 104);
  lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_list, 0, 0);
  lv_obj_set_style_pad_all(s_list, 0, 0);
  lv_obj_set_style_pad_left(s_list, 12, 0);
  lv_obj_set_style_pad_row(s_list, 6, 0);
  lv_obj_set_style_pad_bottom(s_list, 8, 0);
  lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(s_list, LV_DIR_VER);

  std::string err;
  const int code = load(err);
  if (code != 200) {
    lv_obj_t* l = lv_label_create(s_list);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, 440);
    std::string text = err + "\n\nIst unter Filamentverwaltung \"Home Assistant\" gewählt und "
                       "die Brücke eingerichtet (filamentlager-ha-bridge --setup)?";
    lv_label_set_text(l, text.c_str());
    lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_BAD_TEXT), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_ext_14, 0);
    lv_label_set_text(s_summary, "");
    paintChips();
    logSDf("Lager: load failed, code=%d", code);
    return;
  }
  logSDf("Lager: %d kinds", (int)s_items.size());
  fillList();
}

}  // namespace

void showLagerScreen() {
  logSD("SHOW: LagerScreen");
  hideAllOverlays();
  build();
  lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_HIDDEN);
  resetActivityTimer();
}

void lagerScreenClose() {
  if (!s_scr) return;
  lv_obj_add_flag(s_scr, LV_OBJ_FLAG_HIDDEN);
  releaseScreen(&s_scr);
  s_list = nullptr;
  s_summary = nullptr;
  memset(s_chips, 0, sizeof(s_chips));
  s_items.clear();
  s_items.shrink_to_fit();
}

#endif  // SPOOLMANSCALE_PI
