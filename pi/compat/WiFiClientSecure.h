// TLS is done by libcurl inside HTTPClient. This class only carries the trust
// settings the firmware chose (insecure or the system CA store).
#pragma once
#include "WiFiClient.h"

class WiFiClientSecure : public WiFiClient {
 public:
  void setInsecure() { insecure_ = true; }
  bool insecure() const { return insecure_; }
  void setCACert(const char*) { insecure_ = false; }
  void setCACertBundle(const uint8_t*, size_t = 0) { insecure_ = false; }
  void setHandshakeTimeout(unsigned long) {}
  void setTimeout(unsigned long ms) { Stream::setTimeout(ms); }
 protected:
  bool insecure_ = false;
};
