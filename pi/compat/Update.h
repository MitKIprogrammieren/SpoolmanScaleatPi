// Firmware updates do not exist on the Pi in this form (the program is
// updated with git pull + build). Every attempt fails cleanly.
#pragma once
#include "Arduino.h"
#define UPDATE_SIZE_UNKNOWN 0xFFFFFFFF
#define UPDATE_ERROR_ABORT 12
class UpdateClass {
 public:
  bool begin(size_t = UPDATE_SIZE_UNKNOWN, int = 0, int = -1, uint8_t = 0, const char* = nullptr) { return false; }
  size_t write(const uint8_t*, size_t) { return 0; }
  size_t write(uint8_t*, size_t) { return 0; }
  bool end(bool = false) { return false; }
  void abort() {}
  bool isRunning() { return false; }
  bool hasError() { return true; }
  uint8_t getError() { return UPDATE_ERROR_ABORT; }
  const char* errorString() { return "not available on the Raspberry Pi"; }
  void printError(Print&) {}
  size_t size() { return 0; }
  size_t progress() { return 0; }
  size_t remaining() { return 0; }
  bool canRollBack() { return false; }
  bool rollBack() { return false; }
};
extern UpdateClass Update;
