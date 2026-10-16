// FilamentLager look for the Pi build: the dark theme is recoloured to match
// the FilamentLager card in Home Assistant - near-black ground, dark grey
// cards with green frames, filled green buttons, orange/red/green status.
// Light and backend themes stay as they are. SPOOLMANSCALE_THEME=original
// keeps the original blue dark theme.
#include <cstdlib>
#include <cstring>

#include "ui/theme.h"
#include "ui/ha_design.h"

namespace {
uint32_t mix(uint32_t a, uint32_t b, float t) {   // t = share of b
  uint32_t out = 0;
  for (int sh = 0; sh <= 16; sh += 8) {
    const float x = ((a >> sh) & 0xFF) * (1 - t) + ((b >> sh) & 0xFF) * t;
    out |= (uint32_t)(x + 0.5f) << sh;
  }
  return out;
}
int luma(uint32_t c) {
  return (int)(0.299f * ((c >> 16) & 0xFF) + 0.587f * ((c >> 8) & 0xFF) + 0.114f * (c & 0xFF));
}
}  // namespace

void piThemeOverlay(int id) {
  if (id != UI_THEME_DARK) return;
  const char* e = getenv("SPOOLMANSCALE_THEME");
  if (e && strcmp(e, "original") == 0) return;

  // surfaces
  UI_COL_GROUND = 0x111111;
  UI_COL_SURFACE = 0x1c1c1c;
  UI_COL_ROW = 0x232323;
  UI_COL_ROW_PRESSED = 0x2e3a26;
  UI_COL_LINE = 0x33492a;          // frames and the fill of unchosen buttons: dark enough for light text
  UI_COL_LINE_SOFT = 0x2c2c2c;
  UI_COL_PRESS_FILL = 0x3a6420;
  UI_COL_ROW_PRESS_FILL = 0x2e3a26;
  UI_COL_DIVIDER = 0x2a2a2a;
  UI_COL_POPUP_BORDER = 0x5e9e30;
  UI_COL_EMPTY = 0x262626;
  UI_COL_CHIP = 0x262626;
  UI_COL_ACCENT_CHIP = 0x2e3a26;
  // text
  UI_COL_INK = 0xf0f0f0;
  UI_COL_INK_2 = 0xdddddd;
  UI_COL_INK_SOFT = 0xa8a8a8;
  UI_COL_CAPTION = 0x9a9a9a;
  UI_COL_RULE = 0x3a3a3a;
  UI_COL_INK_FAINT = 0x3a3a3a;
  UI_COL_VALUE_BLUE = 0xcfcfcf;
  UI_COL_ON_ACCENT = 0x111111;     // text on a bright green ACCENT fill
  UI_COL_OFF_TEXT = 0x9a9a9a;
  UI_COL_ID_TEXT = 0x8a8a8a;
  UI_COL_STATUS_BLUE = 0x6db33f;
  UI_COL_HDR_OFF = 0x606060;
  UI_COL_DISABLED_BG = 0x1a1a1a;
  UI_COL_DISABLED_TEXT = 0x444444;
  UI_COL_UNAVAILABLE = 0x7a7a7a;
  // accents and buttons
  UI_COL_ACCENT = 0x6db33f;
  UI_COL_GOOD = 0x4caf50;
  UI_COL_OK_BG = 0x4f8a28;
  UI_COL_OK_BG_PRESSED = 0x3a6a1c;
  UI_COL_OK_TEXT = 0xffffff;
  UI_COL_OK_TEXT_2 = 0xd8f5c0;
  UI_COL_WARN = 0xff9800;
  UI_COL_BAD = 0xe53935;
  UI_COL_GO_BG = 0x4f8a28;
  UI_COL_GO_BG_PRESSED = 0x3a6a1c;
  UI_COL_QUIET_BG_PRESSED = 0x2e2e2e;
  UI_COL_CHOICE_BG = 0x232323;
  UI_COL_CHOICE_BG_PRESSED = 0x2e3a26;
  UI_COL_PICKED_BG = 0x2e4a1e;
  UI_COL_ALT_TEXT = 0x8fd860;
  UI_COL_KEY_DEL = 0x2a1818;
  UI_COL_MATCH_BG_PRESSED = 0x3a6a1c;
  UI_COL_NFC_FRAME = 0x5e9e30;
  UI_COL_WEIGHT_BG = 0x1e2a18;
  UI_COL_WEIGHT_BG_PRESSED = 0x2e4a1e;
  UI_COL_WEIGHT_TEXT = 0x8fd860;
  UI_COL_WEIGHT_AUTO = 0x6db33f;
  UI_COL_FILAMENT_BG = 0x1e2a18;
  UI_COL_LV_PRIMARY = 0x5e9e30;
  UI_COL_LV_SECONDARY = 0xff9800;

  // The colour of the frames as set in the card (or on the scale's Design
  // page): every green above that stands for "the accent" follows it.
  const uint32_t a = haDesign().accent;
  const bool light = luma(a) > 170;
  UI_COL_ACCENT = light ? a : mix(a, 0xffffff, 0.12f);
  UI_COL_STATUS_BLUE = UI_COL_ACCENT;
  UI_COL_WEIGHT_AUTO = UI_COL_ACCENT;
  UI_COL_ON_ACCENT = light ? 0x111111 : (luma(UI_COL_ACCENT) > 140 ? 0x111111 : 0xffffff);
  UI_COL_LINE = mix(a, 0x111111, 0.72f);
  UI_COL_POPUP_BORDER = a;
  UI_COL_NFC_FRAME = a;
  UI_COL_LV_PRIMARY = a;
  UI_COL_OK_BG = UI_COL_GO_BG = mix(a, 0x000000, 0.18f);
  UI_COL_OK_BG_PRESSED = UI_COL_GO_BG_PRESSED = UI_COL_MATCH_BG_PRESSED = mix(a, 0x000000, 0.4f);
  UI_COL_OK_TEXT = luma(UI_COL_OK_BG) > 150 ? 0x111111 : 0xffffff;
  UI_COL_OK_TEXT_2 = luma(UI_COL_OK_BG) > 150 ? 0x222222 : mix(a, 0xffffff, 0.75f);
  UI_COL_PRESS_FILL = mix(a, 0x111111, 0.5f);
  UI_COL_ROW_PRESSED = UI_COL_ROW_PRESS_FILL = UI_COL_CHOICE_BG_PRESSED = UI_COL_ACCENT_CHIP =
      mix(a, 0x1c1c1c, 0.8f);
  UI_COL_PICKED_BG = mix(a, 0x111111, 0.68f);
  UI_COL_ALT_TEXT = UI_COL_WEIGHT_TEXT = mix(a, 0xffffff, 0.4f);
  UI_COL_WEIGHT_BG = UI_COL_FILAMENT_BG = mix(a, 0x111111, 0.85f);
  UI_COL_WEIGHT_BG_PRESSED = mix(a, 0x111111, 0.7f);
}
