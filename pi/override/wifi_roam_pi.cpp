// On the Pi, NetworkManager picks the access point; there is nothing to roam.
#include "services/wifi_roam.h"

void wifiRoamTick(bool, const char*, const char*) {}
bool wifiRoamSwitching() { return false; }
