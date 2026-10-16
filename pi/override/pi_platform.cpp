#include "pi_platform.h"

#include <Arduino.h>
#include <WiFi.h>

#include "app/app_state.h"
#include "services/prefs_store.h"

#define PI_NET_WAIT_MS 20000

void piAdoptOsNetwork() {
  const unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < PI_NET_WAIT_MS) delay(250);
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Pi: no network yet - the scale starts offline");
    return;
  }
  if (strlen(cfg_wifi_ssid) > 0) return;
  String ssid = WiFi.SSID();
  if (ssid.length() == 0) ssid = "Ethernet";
  strncpy(cfg_wifi_ssid, ssid.c_str(), sizeof(cfg_wifi_ssid) - 1);
  cfg_wifi_ssid[sizeof(cfg_wifi_ssid) - 1] = '\0';
  // Shown once on the first boot, then the normal connect path runs, the
  // same way as for WiFi handed over by the web flasher.
  if (cfg_first_boot) cfg_setup_resume = true;
  Serial.printf("Pi: using the OS network (%s)\n", cfg_wifi_ssid);
}

const char* piDefaultDeviceLabel() {
  static char name[64] = "";
  if (!name[0]) {
    if (gethostname(name, sizeof(name) - 1) != 0 || !name[0]) snprintf(name, sizeof(name), "filamentlager");
    for (char* c = name; *c; c++) {
      *c = (char)tolower((unsigned char)*c);
      if (*c == '.') { *c = 0; break; }
    }
  }
  return name;
}
