#pragma once
#include <cstdint>
#include "WString.h"
#include "Print.h"

class IPAddress {
 public:
  IPAddress() : v_(0) {}
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
      : v_((uint32_t)a | ((uint32_t)b << 8) | ((uint32_t)c << 16) | ((uint32_t)d << 24)) {}
  IPAddress(uint32_t v) : v_(v) {}   // network byte order, like the ESP core
  operator uint32_t() const { return v_; }
  uint8_t operator[](int i) const { return (uint8_t)(v_ >> (8 * i)); }
  bool operator==(const IPAddress& o) const { return v_ == o.v_; }
  bool operator!=(const IPAddress& o) const { return v_ != o.v_; }
  bool fromString(const char* s);
  bool fromString(const String& s) { return fromString(s.c_str()); }
  String toString() const;
  size_t printTo(Print& p) const { return p.print(toString()); }
 private:
  uint32_t v_;
};

extern const IPAddress INADDR_NONE_IP;
