// Preferences (NVS) as one small text file per namespace in the data dir.
#pragma once
#include "Arduino.h"

class Preferences {
 public:
  bool begin(const char* ns, bool read_only = false, const char* partition = nullptr);
  void end();
  bool clear();
  bool remove(const char* key);
  bool isKey(const char* key);
  size_t freeEntries() { return 500; }

  size_t putChar(const char* k, int8_t v) { return putI(k, v, 1); }
  size_t putUChar(const char* k, uint8_t v) { return putI(k, v, 1); }
  size_t putShort(const char* k, int16_t v) { return putI(k, v, 2); }
  size_t putUShort(const char* k, uint16_t v) { return putI(k, v, 2); }
  size_t putInt(const char* k, int32_t v) { return putI(k, v, 4); }
  size_t putUInt(const char* k, uint32_t v) { return putI(k, v, 4); }
  size_t putLong(const char* k, int32_t v) { return putI(k, v, 4); }
  size_t putULong(const char* k, uint32_t v) { return putI(k, v, 4); }
  size_t putLong64(const char* k, int64_t v) { return putI(k, v, 8); }
  size_t putULong64(const char* k, uint64_t v) { return putI(k, (int64_t)v, 8); }
  size_t putBool(const char* k, bool v) { return putI(k, v ? 1 : 0, 1); }
  size_t putFloat(const char* k, float v);
  size_t putDouble(const char* k, double v);
  size_t putString(const char* k, const char* v);
  size_t putString(const char* k, const String& v) { return putString(k, v.c_str()); }
  size_t putBytes(const char* k, const void* v, size_t len);

  int8_t getChar(const char* k, int8_t d = 0) { return (int8_t)getI(k, d); }
  uint8_t getUChar(const char* k, uint8_t d = 0) { return (uint8_t)getI(k, d); }
  int16_t getShort(const char* k, int16_t d = 0) { return (int16_t)getI(k, d); }
  uint16_t getUShort(const char* k, uint16_t d = 0) { return (uint16_t)getI(k, d); }
  int32_t getInt(const char* k, int32_t d = 0) { return (int32_t)getI(k, d); }
  uint32_t getUInt(const char* k, uint32_t d = 0) { return (uint32_t)getI(k, d); }
  int32_t getLong(const char* k, int32_t d = 0) { return (int32_t)getI(k, d); }
  uint32_t getULong(const char* k, uint32_t d = 0) { return (uint32_t)getI(k, d); }
  int64_t getLong64(const char* k, int64_t d = 0) { return getI(k, d); }
  uint64_t getULong64(const char* k, uint64_t d = 0) { return (uint64_t)getI(k, (int64_t)d); }
  bool getBool(const char* k, bool d = false) { return getI(k, d ? 1 : 0) != 0; }
  float getFloat(const char* k, float d = NAN);
  double getDouble(const char* k, double d = NAN);
  String getString(const char* k, const String& d = String());
  size_t getString(const char* k, char* out, size_t max);
  size_t getBytesLength(const char* k);
  size_t getBytes(const char* k, void* out, size_t max);

 private:
  size_t putI(const char* k, int64_t v, size_t width);
  int64_t getI(const char* k, int64_t d);
  std::string ns_;
  bool open_ = false;
  bool ro_ = false;
};

// Wipes every namespace (the firmware's factory reset).
void piPreferencesEraseAll();
