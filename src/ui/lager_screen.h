#pragma once

// Raspberry Pi with the Home Assistant FilamentLager: the whole stock on the
// scale, read from the bridge's /api/v1/lager. Opened from the settings menu.
void showLagerScreen();
// For hideAllOverlays(): drops the screen when the panel goes elsewhere.
void lagerScreenClose();
