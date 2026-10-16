// The FilamentLager stock as a list, built like the Home Assistant card
// ("Ernsthaft gelagert"): a sort bar with Material, Marke, Farbe,
// Besonderheit and Bestand; groups under a heading with their weight, each
// group a card with a green frame; per kind a colour swatch, the name, chips
// for the special features, rolls with spool / refill and the opened rests,
// and on the right the stock with a bar that turns orange and red as it runs
// low.
//
// Pi only: the data comes from the Home Assistant bridge (pi/ha_bridge),
// which is what backendBaseUrl() points at in Home Assistant mode.
#ifdef SPOOLMANSCALE_PI

#include "lager_screen.h"

#include <ArduinoJson.h>
#include <lvgl.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "services/spoolman_api.h"

#include "app/app_state.h"
#include "hardware/sd_logger.h"
#include "navigation.h"
#include "services/backend.h"
#include "services/prefs_store.h"
#include "settings_screen.h"
#include "theme.h"
#include "ui_common.h"

void resetActivityTimer();

namespace {

// One AMS slot the kind sits in, as the card matches it (Bambu id + colour).
struct Place {
  std::string printer, slot, ams;
  int hum = -1;        // % or -1
  char hum_col = 0;    // 'g', 'o', 'r'
};

struct Entry {
  std::vector<Place> places;
  std::string name, vendor, material, family;
  std::vector<std::string> features;
  uint32_t color = 0x888888;
  int spools = 0, refills = 0, weight = 1000, grams = 0, tags = 0, hue = 0, light = 0;
  std::vector<int> rests;
};

// Stored as a number: Drucker was added after Bestand, so the order of the
// buttons (the card's) and the stored values differ.
enum Sort { S_MATERIAL, S_MARKE, S_FARBE, S_ART, S_BESTAND, S_DRUCKER, S_COUNT };
const int kSortOrder[S_COUNT] = { S_MATERIAL, S_MARKE, S_FARBE, S_ART, S_DRUCKER, S_BESTAND };
const char* const kSortName[S_COUNT] = { "Material", "Marke", "Farbe", "Besonderheit", "Bestand", "Drucker" };
const int kSortW[S_COUNT] = { 72, 62, 56, 100, 72, 70 };
const char* const kNotInAms = "Nicht im AMS";
const char* const kFamilies[] = { "Schwarz", "Grau", "Weiß", "Braun", "Rot", "Orange",
                                  "Gelb", "Grün", "Blau", "Lila", "Pink" };
#define PREF_SORT "lager_sort"
#define PREF_EMPTY "lager_leer"

std::vector<Entry> s_items;
lv_obj_t* s_scr = nullptr;
lv_obj_t* s_list = nullptr;
lv_obj_t* s_summary = nullptr;
lv_obj_t* s_sort_btn[S_COUNT] = {};
lv_obj_t* s_empty_btn = nullptr;
int s_sort = S_MATERIAL;
bool s_show_empty = false;

// ---- formatting --------------------------------------------------------------

void fmtWeight(char* out, size_t n, int grams) {
  if (grams >= 1000) {
    const int tenth = (grams + 50) / 100;
    snprintf(out, n, "%d,%d kg", tenth / 10, tenth % 10);
  } else {
    snprintf(out, n, "%d g", grams);
  }
}

std::string lower(const std::string& s) {
  std::string o(s);
  for (char& c : o) c = (char)tolower((unsigned char)c);
  return o;
}

bool nameLess(const Entry* a, const Entry* b) {
  return lower(a->vendor + " " + a->material + " " + a->name) <
         lower(b->vendor + " " + b->material + " " + b->name);
}

// ---- data --------------------------------------------------------------------

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
    e.refills = o["f"] | 0;
    e.weight = o["w"] | 1000;
    e.grams = o["g"] | 0;
    e.tags = o["t"] | 0;
    for (int r : o["r"].as<JsonArrayConst>()) e.rests.push_back(r);
    for (const char* b : o["b"].as<JsonArrayConst>()) if (b && b[0]) e.features.push_back(b);
    JsonArrayConst ff = o["ff"];
    e.family = ff[0] | "Grau";
    e.hue = ff[1] | 0;
    e.light = ff[2] | 0;
    for (JsonObjectConst a : o["a"].as<JsonArrayConst>()) {
      Place pl;
      pl.printer = a["d"] | "";
      pl.slot = a["p"] | "";
      pl.ams = a["m"] | "";
      pl.hum = a["h"].is<int>() ? (int)a["h"] : -1;
      const char* hc = a["hc"] | "";
      pl.hum_col = hc[0];
      e.places.push_back(std::move(pl));
    }
    s_items.push_back(std::move(e));
  }
  return code;
}

struct Item {
  const Entry* e;
  const Place* place;   // Drucker sorting: the slot this copy stands for
};
using Group = std::pair<std::string, std::vector<Item>>;

// "X1C 2" before "X1C 10", the way localeCompare(..., {numeric: true}) sorts.
bool natLess(const std::string& a, const std::string& b) {
  size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    if (isdigit((unsigned char)a[i]) && isdigit((unsigned char)b[j])) {
      long x = 0, y = 0;
      while (i < a.size() && isdigit((unsigned char)a[i])) x = x * 10 + (a[i++] - '0');
      while (j < b.size() && isdigit((unsigned char)b[j])) y = y * 10 + (b[j++] - '0');
      if (x != y) return x < y;
    } else {
      const char ca = (char)tolower((unsigned char)a[i++]), cb = (char)tolower((unsigned char)b[j++]);
      if (ca != cb) return ca < cb;
    }
  }
  return a.size() - i < b.size() - j;
}

// The card's "Nach Drucker": a kind in an AMS under each printer it is in
// (once per printer and AMS), ordered by slot; everything else under "Nicht im AMS".
std::vector<Group> groupByPrinter() {
  std::map<std::string, std::vector<Item>> g;
  for (const Entry& e : s_items) {
    if (e.spools <= 0 && !s_show_empty) continue;
    if (e.places.empty()) { g[kNotInAms].push_back({&e, nullptr}); continue; }
    std::vector<std::string> seen;
    for (const Place& p : e.places) {
      const std::string key = p.printer + "|" + p.ams;
      if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
      seen.push_back(key);
      g[p.printer].push_back({&e, &p});
    }
  }
  std::vector<Group> out;
  for (auto& kv : g)
    if (kv.first != kNotInAms) out.emplace_back(kv.first, kv.second);
  std::sort(out.begin(), out.end(), [](const Group& a, const Group& b) { return natLess(a.first, b.first); });
  for (auto& grp : out)
    std::sort(grp.second.begin(), grp.second.end(), [](const Item& a, const Item& b) {
      if (a.place->slot != b.place->slot) return natLess(a.place->slot, b.place->slot);
      return nameLess(a.e, b.e); });
  if (g.count(kNotInAms)) {
    auto rest = g[kNotInAms];
    std::sort(rest.begin(), rest.end(), [](const Item& a, const Item& b) { return nameLess(a.e, b.e); });
    out.emplace_back(kNotInAms, rest);
  }
  return out;
}

// Groups and their order, as the card's _gruppen() builds them.
std::vector<Group> groupItems() {
  if (s_sort == S_DRUCKER) return groupByPrinter();
  std::map<std::string, std::vector<const Entry*>> g;
  for (const Entry& e : s_items) {
    if (e.spools <= 0 && !s_show_empty) continue;
    std::string key;
    switch (s_sort) {
      case S_MARKE:   key = e.vendor.empty() ? "Ohne Marke" : e.vendor; break;
      case S_FARBE:   key = e.family; break;
      case S_ART:     key = e.features.empty() ? "Ohne Besonderheit" : e.features[0]; break;
      case S_BESTAND: key = "Nach Bestand"; break;
      default:        key = e.material.empty() ? "Ohne Material" : e.material; break;
    }
    g[key].push_back(&e);
  }
  std::vector<std::pair<std::string, std::vector<const Entry*>>> sorted;
  auto& out = sorted;
  if (s_sort == S_FARBE) {
    for (const char* f : kFamilies)
      if (g.count(f)) out.emplace_back(f, g[f]);
  } else {
    for (auto& kv : g)
      if (kv.first != "Ohne Besonderheit") out.emplace_back(kv.first, kv.second);
    if (s_sort != S_MARKE && s_sort != S_MATERIAL)
      std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return lower(a.first) < lower(b.first); });
    else
      std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return lower(a.first) < lower(b.first); });
    if (g.count("Ohne Besonderheit")) out.emplace_back("Ohne Besonderheit", g["Ohne Besonderheit"]);
  }
  for (auto& grp : out) {
    auto& v = grp.second;
    switch (s_sort) {
      case S_MARKE:
        std::sort(v.begin(), v.end(), [](const Entry* a, const Entry* b) {
          return lower(a->material + " " + a->name) < lower(b->material + " " + b->name); });
        break;
      case S_FARBE:
        std::sort(v.begin(), v.end(), [](const Entry* a, const Entry* b) {
          if (a->hue != b->hue) return a->hue < b->hue;
          if (a->light != b->light) return a->light < b->light;
          return nameLess(a, b); });
        break;
      case S_BESTAND:
        std::sort(v.begin(), v.end(), [](const Entry* a, const Entry* b) {
          if (a->grams != b->grams) return a->grams > b->grams;
          return nameLess(a, b); });
        break;
      default:
        std::sort(v.begin(), v.end(), nameLess);
        break;
    }
  }
  std::vector<Group> res;
  for (auto& grp : sorted) {
    std::vector<Item> items;
    for (const Entry* e : grp.second) items.push_back({e, nullptr});
    res.emplace_back(grp.first, std::move(items));
  }
  return res;
}

// ---- drawing -----------------------------------------------------------------

lv_obj_t* plain(lv_obj_t* parent) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_shadow_width(o, 0, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

// A chip as the card draws it: small rounded tag, tinted fill or outline.
void chip(lv_obj_t* parent, const char* text, uint32_t fg, uint32_t bg, bool outline) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_ext_10, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(fg), 0);
  lv_obj_set_style_radius(l, 5, 0);
  lv_obj_set_style_pad_hor(l, 5, 0);
  lv_obj_set_style_pad_ver(l, 1, 0);
  if (outline) {
    lv_obj_set_style_border_width(l, 1, 0);
    lv_obj_set_style_border_color(l, lv_color_hex(bg), 0);
  } else {
    lv_obj_set_style_bg_color(l, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
  }
}

void featureColours(const std::string& f, uint32_t* fg, uint32_t* bg) {
  const std::string l = lower(f);
  if (l == "gf" || l == "cf" || l.find("glas") != std::string::npos || l.find("carbon") != std::string::npos ||
      l.find(" gf") != std::string::npos || l.find(" cf") != std::string::npos) {
    *fg = 0xffa726; *bg = 0x3d2a10;                       // fibre: orange
  } else if (l.find("glitzer") != std::string::npos || l.find("galaxy") != std::string::npos ||
             l.find("sparkle") != std::string::npos) {
    *fg = 0xce93d8; *bg = 0x34203a;                       // glitter: purple
  } else if (l.find("matt") != std::string::npos) {
    *fg = 0x64b5f6; *bg = 0x182c40;                       // matte: blue
  } else {
    *fg = 0xe0e0e0; *bg = 0x333333;
  }
}

uint32_t humColour(char c) {
  return c == 'g' ? 0x4caf50 : c == 'o' ? 0xffa726 : c == 'r' ? 0xef5350 : UI_COL_CAPTION;
}

void addRow(lv_obj_t* grp, const Entry& e, const Place* in_slot, bool last) {
  lv_obj_t* row = plain(grp);
  lv_obj_set_size(row, LV_PCT(100), 58);
  if (!last) {
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(UI_COL_DIVIDER), 0);
  }

  lv_obj_t* sw = plain(row);
  lv_obj_set_size(sw, 24, 24);
  lv_obj_align(sw, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_radius(sw, 6, 0);
  lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(sw, lv_color_hex(e.color), 0);
  lv_obj_set_style_border_width(sw, 1, 0);
  lv_obj_set_style_border_color(sw, lv_color_hex(0x707070), 0);

  lv_obj_t* name = lv_label_create(row);
  lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
  lv_obj_set_width(name, 300);
  lv_label_set_text(name, e.name.c_str());
  lv_obj_set_style_text_color(name, lv_color_hex(UI_COL_INK), 0);
  lv_obj_set_style_text_font(name, &lv_font_montserrat_ext_14, 0);
  lv_obj_set_pos(name, 34, 8);

  // chips in a row that clips what does not fit
  lv_obj_t* chips = plain(row);
  lv_obj_set_size(chips, 310, 18);
  lv_obj_set_pos(chips, 34, 32);
  lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(chips, 4, 0);
  lv_obj_set_flex_align(chips, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  for (const std::string& f : e.features) {
    uint32_t fg, bg;
    featureColours(f, &fg, &bg);
    chip(chips, f.c_str(), fg, bg, false);
  }
  char buf[80];
  const int with_spool = std::max(0, e.spools - e.refills);
  if (e.spools <= 0) {
    chip(chips, "aufgebraucht", 0xef5350, 0x3a1818, false);
  } else {
    if (with_spool > 0) {
      snprintf(buf, sizeof(buf), "%d mit Spule", with_spool);
      chip(chips, buf, UI_COL_INK_SOFT, 0x6a6a6a, true);
    }
    if (e.refills > 0) {
      snprintf(buf, sizeof(buf), "%d Refill", e.refills);
      chip(chips, buf, 0x4db6ac, 0x123a36, false);
    }
    if (!e.rests.empty()) {
      int n = snprintf(buf, sizeof(buf), "angebrochen");
      for (size_t i = 0; i < e.rests.size() && n < (int)sizeof(buf) - 12; i++)
        n += snprintf(buf + n, sizeof(buf) - n, "%s %d g", i ? " +" : "", e.rests[i]);
      chip(chips, buf, 0xffa726, 0x8a5a14, true);
    }
  }

  // Where it sits in an AMS. Sorted by printer the heading names printer and
  // AMS already, so the chip carries the slot alone.
  if (in_slot) {
    chip(chips, in_slot->slot.c_str(), 0x4caf50, 0x1e3a20, false);
  } else {
    for (const Place& p : e.places) {
      std::string t = p.printer + " " + p.slot;
      if (p.hum >= 0) t += "  " + std::to_string(p.hum) + " %";
      chip(chips, t.c_str(), 0x4caf50, 0x1e3a20, false);
    }
  }

  // stock and bar on the right
  char kg[16];
  fmtWeight(kg, sizeof(kg), e.grams);
  lv_obj_t* k = lv_label_create(row);
  lv_label_set_text(k, kg);
  lv_obj_set_style_text_color(k, lv_color_hex(UI_COL_INK), 0);
  lv_obj_set_style_text_font(k, &lv_font_montserrat_ext_14, 0);
  lv_obj_align(k, LV_ALIGN_RIGHT_MID, 0, -8);

  const int cap = std::max(1, e.spools) * std::max(1, e.weight);
  const int pct = e.spools <= 0 ? 0 : std::max(2, (int)((e.grams * 100L + cap / 2) / cap));
  const uint32_t col = pct > 50 ? 0x4caf50 : pct > 20 ? 0xffa726 : 0xef5350;
  lv_obj_t* bar = plain(row);
  lv_obj_set_size(bar, 56, 5);
  lv_obj_align(bar, LV_ALIGN_RIGHT_MID, 0, 12);
  lv_obj_set_style_radius(bar, 3, 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x3a3a3a), 0);
  lv_obj_set_style_clip_corner(bar, true, 0);
  if (pct > 0) {
    lv_obj_t* fill = plain(bar);
    lv_obj_set_size(fill, std::max(2, 56 * std::min(pct, 100) / 100), 5);
    lv_obj_set_style_radius(fill, 3, 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(fill, lv_color_hex(col), 0);
  }
}

void paintSortBar() {
  for (int i = 0; i < S_COUNT; i++) {
    if (!s_sort_btn[i]) continue;
    const bool on = i == s_sort;
    lv_obj_set_style_bg_color(s_sort_btn[i], lv_color_hex(on ? 0x4caf50 : UI_COL_GROUND), 0);
    lv_obj_set_style_border_color(s_sort_btn[i], lv_color_hex(on ? 0x4caf50 : 0x555555), 0);
    lv_obj_t* l = lv_obj_get_child(s_sort_btn[i], 0);
    if (l) lv_obj_set_style_text_color(l, lv_color_hex(on ? 0xffffff : UI_COL_INK), 0);
  }
  if (s_empty_btn) {
    lv_obj_set_style_bg_color(s_empty_btn, lv_color_hex(s_show_empty ? 0x3a1818 : UI_COL_GROUND), 0);
    lv_obj_set_style_border_color(s_empty_btn, lv_color_hex(s_show_empty ? 0xef5350 : 0x555555), 0);
    lv_obj_t* l = lv_obj_get_child(s_empty_btn, 0);
    if (l) lv_obj_set_style_text_color(l, lv_color_hex(s_show_empty ? 0xef5350 : UI_COL_INK), 0);
  }
}

void fillList() {
  lv_obj_clean(s_list);
  const auto groups = groupItems();
  for (const auto& grp : groups) {
    int g = 0;
    int slots = 0;
    for (const Item& it : grp.second) {
      g += it.e->grams;
      if (it.place) slots++;
    }

    lv_obj_t* head = plain(s_list);
    lv_obj_set_size(head, LV_PCT(100), 22);
    std::string title = grp.first;
    for (char& c : title) c = (char)toupper((unsigned char)c);
    lv_obj_t* hl = lv_label_create(head);
    lv_label_set_text(hl, title.c_str());
    lv_obj_set_style_text_color(hl, lv_color_hex(UI_COL_CAPTION), 0);
    lv_obj_set_style_text_font(hl, &lv_font_montserrat_ext_12, 0);
    lv_obj_align(hl, LV_ALIGN_BOTTOM_LEFT, 6, -2);
    char kg[16], right[40];
    fmtWeight(kg, sizeof(kg), g);
    if (slots) snprintf(right, sizeof(right), "%d %s | %s", slots, slots == 1 ? "Platz" : "Plätze", kg);
    else snprintf(right, sizeof(right), "%s", kg);
    lv_obj_t* hr = lv_label_create(head);
    lv_label_set_text(hr, right);
    lv_obj_set_style_text_color(hr, lv_color_hex(UI_COL_CAPTION), 0);
    lv_obj_set_style_text_font(hr, &lv_font_montserrat_ext_12, 0);
    lv_obj_align(hr, LV_ALIGN_BOTTOM_RIGHT, -6, -2);

    lv_obj_t* card = plain(s_list);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x4caf50), 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_pad_hor(card, 10, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    std::string last_ams;
    for (size_t i = 0; i < grp.second.size(); i++) {
      const Item& it = grp.second[i];
      // Sub heading per AMS, with its humidity, as the card has it.
      if (it.place && it.place->ams != last_ams) {
        last_ams = it.place->ams;
        lv_obj_t* sh = plain(card);
        lv_obj_set_size(sh, LV_PCT(100), 22);
        lv_obj_set_style_border_side(sh, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(sh, 1, 0);
        lv_obj_set_style_border_color(sh, lv_color_hex(UI_COL_DIVIDER), 0);
        std::string t = it.place->ams;
        for (char& c : t) c = (char)toupper((unsigned char)c);
        lv_obj_t* sl = lv_label_create(sh);
        lv_label_set_text(sl, t.c_str());
        lv_obj_set_style_text_color(sl, lv_color_hex(UI_COL_CAPTION), 0);
        lv_obj_set_style_text_font(sl, &lv_font_montserrat_ext_10, 0);
        lv_obj_align(sl, LV_ALIGN_BOTTOM_LEFT, 0, -3);
        if (it.place->hum >= 0) {
          char hb[16];
          snprintf(hb, sizeof(hb), "%d %%", it.place->hum);
          lv_obj_t* hl2 = lv_label_create(sh);
          lv_label_set_text(hl2, hb);
          lv_obj_set_style_text_color(hl2, lv_color_hex(humColour(it.place->hum_col)), 0);
          lv_obj_set_style_text_font(hl2, &lv_font_montserrat_ext_10, 0);
          lv_obj_align(hl2, LV_ALIGN_BOTTOM_RIGHT, 0, -3);
        }
      }
      addRow(card, *it.e, it.place, i + 1 == grp.second.size());
    }
  }

  // the header line: total, kinds, rolls (with spool / refill), over everything
  int grams = 0, kinds = 0, rolls = 0, refills = 0, empty = 0;
  for (const Entry& e : s_items) {
    grams += e.grams;
    kinds++;
    rolls += std::max(0, e.spools);
    refills += std::max(0, std::min(e.refills, e.spools));
    if (e.spools <= 0) empty++;
  }
  char kg[16], buf[120];
  fmtWeight(kg, sizeof(kg), grams);
  snprintf(buf, sizeof(buf), "#4caf50 %s# | %d Sorten | %d Rollen (%d mit Spule, %d Refill)",
           kg, kinds, rolls, rolls - refills, refills);
  lv_label_set_text(s_summary, buf);
  if (s_empty_btn) {
    snprintf(buf, sizeof(buf), "Leer (%d)", empty);
    lv_label_set_text(lv_obj_get_child(s_empty_btn, 0), buf);
  }
  if (groups.empty()) {
    lv_obj_t* l = lv_label_create(s_list);
    lv_label_set_text(l, "Alle Sorten sind aufgebraucht.");
    lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_CAPTION), 0);
  }
  lv_obj_scroll_to_y(s_list, 0, LV_ANIM_OFF);
  paintSortBar();
}

lv_obj_t* pill(int x, int y, int w, int h, const char* text, lv_event_cb_t cb, void* ud) {
  lv_obj_t* c = lv_btn_create(s_scr);
  lv_obj_set_size(c, w, h);
  lv_obj_set_pos(c, x, y);
  lv_obj_set_style_radius(c, h / 2, 0);
  lv_obj_set_style_border_width(c, 1, 0);
  lv_obj_set_style_shadow_width(c, 0, 0);
  lv_obj_set_style_pad_all(c, 0, 0);
  lv_obj_t* l = lv_label_create(c);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_ext_12, 0);
  lv_obj_center(l);
  lv_obj_add_event_cb(c, cb, LV_EVENT_CLICKED, ud);
  return c;
}

void build() {
  releaseScreen(&s_scr);
  s_scr = buildOverlayScreen();
  memset(s_sort_btn, 0, sizeof(s_sort_btn));
  s_empty_btn = nullptr;
  s_sort = prefsGetUChar(PREF_SORT, S_MATERIAL);
  if (s_sort >= S_COUNT) s_sort = S_MATERIAL;
  s_show_empty = prefsGetUChar(PREF_EMPTY, 0) == 1;

  buildSubHeader(s_scr, "Lager", [](lv_event_t*) {
    logSD("BTN: Lager -> Back");
    lagerScreenClose();
    showSettingsScreen();
  });

  s_summary = lv_label_create(s_scr);
  lv_label_set_recolor(s_summary, true);
  lv_label_set_text(s_summary, "");
  lv_obj_set_style_text_color(s_summary, lv_color_hex(UI_COL_INK_SOFT), 0);
  lv_obj_set_style_text_font(s_summary, &lv_font_montserrat_ext_12, 0);
  lv_obj_set_pos(s_summary, 10, 50);
  lv_label_set_long_mode(s_summary, LV_LABEL_LONG_DOT);
  lv_obj_set_width(s_summary, 380);

  int x = 8;
  for (int n = 0; n < S_COUNT; n++) {
    const int i = kSortOrder[n];
    s_sort_btn[i] = pill(x, 70, kSortW[i], 28, kSortName[i], [](lv_event_t* e) {
      s_sort = (int)(intptr_t)lv_event_get_user_data(e);
      prefsPutUChar(PREF_SORT, (uint8_t)s_sort);
      logSDf("BTN: Lager -> sort %s", kSortName[s_sort]);
      fillList();
      resetActivityTimer();
    }, (void*)(intptr_t)i);
    x += kSortW[i] + 6;
  }
  s_empty_btn = pill(392, 45, 80, 22, "Leer", [](lv_event_t*) {
    s_show_empty = !s_show_empty;
    prefsPutUChar(PREF_EMPTY, s_show_empty ? 1 : 0);
    logSDf("BTN: Lager -> empty %s", s_show_empty ? "shown" : "hidden");
    fillList();
    resetActivityTimer();
  }, nullptr);

  s_list = lv_obj_create(s_scr);
  lv_obj_set_size(s_list, 480, 216);
  lv_obj_set_pos(s_list, 0, 104);
  lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_list, 0, 0);
  lv_obj_set_style_pad_all(s_list, 0, 0);
  lv_obj_set_style_pad_hor(s_list, 8, 0);
  lv_obj_set_style_pad_row(s_list, 2, 0);
  lv_obj_set_style_pad_bottom(s_list, 10, 0);
  lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(s_list, LV_DIR_VER);

  std::string err;
  const int code = load(err);
  if (code != 200) {
    lv_obj_t* l = lv_label_create(s_list);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, 440);
    std::string text = err + "\n\nIst unter Filamentverwaltung \"Home Assistant\" gewählt, mit der "
                       "Adresse 127.0.0.1:7913, und die Brücke eingerichtet "
                       "(filamentlager-ha-bridge --check)?";
    lv_label_set_text(l, text.c_str());
    lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_BAD_TEXT), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_ext_14, 0);
    paintSortBar();
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
  s_empty_btn = nullptr;
  memset(s_sort_btn, 0, sizeof(s_sort_btn));
  s_items.clear();
  s_items.shrink_to_fit();
}

#endif  // SPOOLMANSCALE_PI
