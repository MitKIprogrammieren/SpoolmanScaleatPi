#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "WString.h"

#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t c) = 0;
  virtual size_t write(const uint8_t* buf, size_t n) {
    size_t w = 0;
    while (n--) { if (!write(*buf++)) break; w++; }
    return w;
  }
  size_t write(const char* s) { return s ? write((const uint8_t*)s, strlen(s)) : 0; }
  size_t write(const char* buf, size_t n) { return write((const uint8_t*)buf, n); }
  virtual void flush() {}
  virtual int availableForWrite() { return 0; }

  size_t printf(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
  size_t vprintf(const char* fmt, va_list ap);

  size_t print(const String& s) { return write(s.c_str(), s.length()); }
  size_t print(const char* s) { return write(s); }
  size_t print(const __FlashStringHelper* s) { return write(reinterpret_cast<const char*>(s)); }
  size_t print(char c) { return write((uint8_t)c); }
  size_t print(unsigned char v, int base = DEC) { return print(String(v, (unsigned char)base)); }
  size_t print(int v, int base = DEC) { return print(String(v, (unsigned char)base)); }
  size_t print(unsigned int v, int base = DEC) { return print(String(v, (unsigned char)base)); }
  size_t print(long v, int base = DEC) { return print(String(v, (unsigned char)base)); }
  size_t print(unsigned long v, int base = DEC) { return print(String(v, (unsigned char)base)); }
  size_t print(long long v, int base = DEC) { return print(String(v, (unsigned char)base)); }
  size_t print(unsigned long long v, int base = DEC) { return print(String(v, (unsigned char)base)); }
  size_t print(double v, int digits = 2) { return print(String(v, (unsigned int)digits)); }

  size_t println() { return write("\n"); }
  template <typename T> size_t println(const T& v) { size_t n = print(v); return n + println(); }
  template <typename T> size_t println(const T& v, int f) { size_t n = print(v, f); return n + println(); }
};
