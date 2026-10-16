#include "settings_screen.h"
#include "navigation.h"
#include "app/app_state.h"

#include <Arduino.h>
#include <lvgl.h>

#include "connection_screen.h"
#include "display_screen.h"
#include "hardware/sd_logger.h"
#include "lang.h"
#include "scale_menu.h"
#include "services/ota_state.h"
#include "system_screen.h"
#include "ui_common.h"
#include "update_badges.h"
#include "services/backend.h"
#include "services/user_options.h"
#include "theme.h"
#ifdef SPOOLMANSCALE_PI
#include "lager_screen.h"
#endif


void resetActivityTimer();

void buildSettingsScreen() {
  logSD("BUILD: SettingsScreen");
  if (sd_verbose) logSD("[verbose] buildSettingsScreen: start");
  releaseScreen(&scr_settings);
  scr_settings = buildOverlayScreen();

  lv_obj_t *title = lv_label_create(scr_settings);
  lv_label_set_text(title, T(STR_SETTINGS_TITLE));
  lv_obj_set_style_text_color(title, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_ext_18, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

  lv_obj_t *btn_x = lv_btn_create(scr_settings);
  lv_obj_set_size(btn_x, 44, 44);
  lv_obj_align(btn_x, LV_ALIGN_TOP_RIGHT, -4, 2);
  lv_obj_set_style_bg_color(btn_x, lv_color_hex(UI_COL_BAD_BG), 0);
  lv_obj_set_style_bg_color(btn_x, lv_color_hex(UI_COL_BAD_BG_PRESSED), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_x, 8, 0);
  lv_obj_set_style_shadow_width(btn_x, 0, 0);
  lv_obj_set_style_border_width(btn_x, 0, 0);
  lv_obj_t *lbl_x = lv_label_create(btn_x);
  lv_label_set_text(lbl_x, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_color(lbl_x, lv_color_hex(UI_COL_BAD_TEXT), 0);
  lv_obj_set_style_text_font(lbl_x, &lv_font_montserrat_ext_18, 0);
  lv_obj_center(lbl_x);
  lv_obj_add_event_cb(btn_x, [](lv_event_t *e){ logSD("BTN: Close -> Main"); showMainScreen(); }, LV_EVENT_CLICKED, NULL);

  // Names the active backend, so it is copied through backendText first.
  char conn_sub[40];
  backendText(T(STR_TILE_CONN_SUB), conn_sub, sizeof(conn_sub));
  struct { const char *icon; const char *label; const char *sub; uint32_t col; } tiles[] = {
    { LV_SYMBOL_WIFI,     T(STR_TILE_CONNECTION), conn_sub,                UI_COL_ROW },
    // The label stays "Scale" even with none fitted: this tile is where the
    // switch that turns it back on lives. Only the subtitle stops naming the
    // two rows that are no longer in there.
    { LV_SYMBOL_DRIVE,    T(STR_TILE_SCALE),
      T(g_scale_fitted ? STR_TILE_SCALE_SUB : STR_TILE_SCALE_SUB_OFF),         UI_COL_ROW },
    { LV_SYMBOL_IMAGE,    T(STR_TILE_DISPLAY),    T(STR_TILE_DISPLAY_SUB), UI_COL_ROW },
    { LV_SYMBOL_SETTINGS, T(STR_TILE_SYSTEM),     T(STR_TILE_SYSTEM_SUB),  UI_COL_ROW },
  };
  int tx[] = { 8, 242, 8, 242 };
  int ty[] = { 60, 60, 186, 186 };
  int tile_h = 118;
#ifdef SPOOLMANSCALE_PI
  // Home Assistant FilamentLager: a full width "Lager" bar under the tiles,
  // which move up and lose a little height to make room.
  const bool lager = backendIsHomeAssistant();
  if (lager) { ty[0] = ty[1] = 52; ty[2] = ty[3] = 150; tile_h = 92; }
#endif

  // Kept so the update dot can be anchored to it after the loop.
  lv_obj_t *tile_system = nullptr;

  for (int i = 0; i < 4; i++) {
    lv_obj_t *tile = lv_btn_create(scr_settings);
    if (i == 3) tile_system = tile;
    lv_obj_set_size(tile, 226, tile_h);
    lv_obj_set_pos(tile, tx[i], ty[i]);
    lv_obj_set_style_bg_color(tile, lv_color_hex(tiles[i].col), 0);
    lv_obj_set_style_bg_color(tile, lv_color_hex(uiShade(tiles[i].col, UI_SHADE_PRESSED)), LV_STATE_PRESSED);
    lv_obj_set_style_radius(tile, 10, 0);
    lv_obj_set_style_shadow_width(tile, 0, 0);
    lv_obj_set_style_border_width(tile, 1, 0);
    lv_obj_set_style_border_color(tile, lv_color_hex(uiShade(tiles[i].col, UI_SHADE_BORDER)), 0);

    lv_obj_t *ico = lv_label_create(tile);
    lv_label_set_text(ico, tiles[i].icon);
    lv_obj_set_style_text_color(ico, lv_color_hex(UI_COL_ACCENT), 0);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_ext_24, 0);
    lv_obj_align(ico, LV_ALIGN_TOP_LEFT, 10, 8);

    lv_obj_t *lbl = lv_label_create(tile);
    lv_label_set_text(lbl, tiles[i].label);
    lv_obj_set_style_text_color(lbl, lv_color_hex(UI_COL_INK), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_ext_18, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -8);

    lv_obj_t *sub = lv_label_create(tile);
    lv_label_set_text(sub, tiles[i].sub);
    lv_obj_set_style_text_color(sub, lv_color_hex(UI_COL_CAPTION), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_ext_12, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 16);

    lv_obj_add_event_cb(tile, [](lv_event_t *e) {
      intptr_t idx = (intptr_t)lv_event_get_user_data(e);
      switch (idx) {
        case 0:
          logSD("UI: Tile -> Connection");
          closeConnectionScreen();
          buildConnectionScreen();
          if (!scr_connection) buildConnectionScreen();
          hideAllOverlays();
          lv_obj_clear_flag(scr_connection, LV_OBJ_FLAG_HIDDEN);
          break;
        case 1:
          logSD("UI: Tile -> Scale");
          if (!scr_scale_sub) buildScaleSubScreen();
          if (!scr_scale_sub) buildScaleSubScreen();
          hideAllOverlays();
          lv_obj_clear_flag(scr_scale_sub, LV_OBJ_FLAG_HIDDEN);
          break;
        case 2:
          logSD("UI: Tile -> Display");
          if (!scr_display) buildDisplayScreen();
          hideAllOverlays();
          lv_obj_clear_flag(scr_display, LV_OBJ_FLAG_HIDDEN);
          break;
        case 3:
          logSD("UI: Tile -> System");
          if (!scr_system) buildSystemScreen();
          hideAllOverlays();
          lv_obj_clear_flag(scr_system, LV_OBJ_FLAG_HIDDEN);
          break;
      }
      resetActivityTimer();
    }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }

  lbl_system_badge = createUpdateBadge(scr_settings, tile_system);

#ifdef SPOOLMANSCALE_PI
  if (lager) {
    lv_obj_t *bar = lv_btn_create(scr_settings);
    lv_obj_set_size(bar, 460, 62);
    lv_obj_set_pos(bar, 8, 250);
    lv_obj_set_style_bg_color(bar, lv_color_hex(UI_COL_OK_BG), 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(UI_COL_OK_BG_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_radius(bar, 10, 0);
    lv_obj_set_style_shadow_width(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_t *l = lv_label_create(bar);
    lv_label_set_text(l, LV_SYMBOL_LIST "   Lager");
    lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_OK_TEXT), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_ext_20, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, -8);
    lv_obj_t *s = lv_label_create(bar);
    lv_label_set_text(s, "Alle Filamente aus Home Assistant");
    lv_obj_set_style_text_color(s, lv_color_hex(UI_COL_OK_TEXT), 0);
    lv_obj_set_style_text_font(s, &lv_font_montserrat_ext_12, 0);
    lv_obj_align(s, LV_ALIGN_CENTER, 0, 15);
    lv_obj_add_event_cb(bar, [](lv_event_t *e) {
      logSD("UI: Settings -> Lager");
      showLagerScreen();
    }, LV_EVENT_CLICKED, NULL);
  }
#endif

  if (sd_verbose) logSD("[verbose] buildSettingsScreen: done");
}
