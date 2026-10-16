#pragma once
#include "Arduino.h"
#include "IPAddress.h"
enum class DNSReplyCode { NoError = 0, ServerFailure = 2, NonExistentDomain = 3 };
class DNSServer {
 public:
  bool start(uint16_t, const String&, const IPAddress&) { return false; }
  void stop() {}
  void processNextRequest() {}
  void setErrorReplyCode(DNSReplyCode) {}
  void setTTL(uint32_t) {}
};
