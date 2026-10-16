// mDNS through avahi on the Pi: the hostname is announced by the OS, this
// class publishes the http service with avahi-publish-service when present.
#pragma once
#include "Arduino.h"
#include <string>
#include <vector>
class MDNSResponder {
 public:
  bool begin(const char* hostname);
  void end();
  void setInstanceName(const String& n) { instance_ = n.c_str(); }
  bool addService(const char* service, const char* proto, uint16_t port);
  bool addServiceTxt(const char* service, const char* proto, const char* key, const char* value);
  bool addServiceTxt(const char* s, const char* p, const String& k, const String& v) { return addServiceTxt(s, p, k.c_str(), v.c_str()); }
  void enableArduino(uint16_t = 3232, bool = false) {}
  void republish();
 private:
  std::string host_, instance_ = "SpoolmanScale", service_, proto_;
  uint16_t port_ = 0;
  std::vector<std::string> txt_;
  int pid_ = -1;
};
extern MDNSResponder MDNS;
