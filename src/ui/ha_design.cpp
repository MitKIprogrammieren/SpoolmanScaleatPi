#include "ha_design.h"

#include <Arduino.h>

#include "services/prefs_store.h"

namespace {
HaDesign s_d;
bool s_loaded = false;
uint32_t s_boot_accent = 0;

void load() {
  if (s_loaded) return;
  s_loaded = true;
  String a = prefsGetString("ha_farbe", "");
  if (a.length() == 7 && a[0] == '#') s_d.accent = (uint32_t)strtoul(a.c_str() + 1, nullptr, 16);
  s_d.frame = prefsGetUChar("ha_rand", 2);
  if (s_d.frame < 1 || s_d.frame > 3) s_d.frame = 2;
  s_d.compact = prefsGetUChar("ha_kompakt", 0) == 1;
  s_d.bilingual = prefsGetUChar("ha_zwei", 1) == 1;
  s_boot_accent = s_d.accent;
}
}  // namespace

const HaDesign& haDesign() {
  load();
  return s_d;
}

uint32_t haDesignBootAccent() {
  load();
  return s_boot_accent;
}

bool haDesignStore(const HaDesign& d) {
  load();
  const bool accent_changed = d.accent != s_d.accent;
  if (accent_changed) {
    char hex[8];
    snprintf(hex, sizeof(hex), "#%06lx", (unsigned long)(d.accent & 0xFFFFFF));
    prefsPutString("ha_farbe", hex);
  }
  if (d.frame != s_d.frame) prefsPutUChar("ha_rand", d.frame);
  if (d.compact != s_d.compact) prefsPutUChar("ha_kompakt", d.compact ? 1 : 0);
  if (d.bilingual != s_d.bilingual) prefsPutUChar("ha_zwei", d.bilingual ? 1 : 0);
  s_d = d;
  return accent_changed;
}
