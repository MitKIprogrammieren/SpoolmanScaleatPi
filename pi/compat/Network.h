#pragma once
#include "WiFi.h"
class NetworkClass {
 public:
  int hostByName(const char* host, IPAddress& out) { return WiFi.hostByName(host, out); }
};
extern NetworkClass Network;
