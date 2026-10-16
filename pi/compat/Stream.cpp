#include "Stream.h"

#include <cctype>
#include <thread>

int Stream::timedRead() {
  const unsigned long t0 = millis();
  do {
    int c = read();
    if (c >= 0) return c;
    std::this_thread::sleep_for(std::chrono::microseconds(200));
  } while (millis() - t0 < timeout_);
  return -1;
}

int Stream::timedPeek() {
  const unsigned long t0 = millis();
  do {
    int c = peek();
    if (c >= 0) return c;
    std::this_thread::sleep_for(std::chrono::microseconds(200));
  } while (millis() - t0 < timeout_);
  return -1;
}

size_t Stream::readBytes(char* buf, size_t len) {
  size_t n = 0;
  while (n < len) {
    int c = timedRead();
    if (c < 0) break;
    buf[n++] = (char)c;
  }
  return n;
}

size_t Stream::readBytesUntil(char term, char* buf, size_t len) {
  size_t n = 0;
  while (n < len) {
    int c = timedRead();
    if (c < 0 || c == term) break;
    buf[n++] = (char)c;
  }
  return n;
}

String Stream::readString() {
  String s;
  int c;
  while ((c = timedRead()) >= 0) s.concat((char)c);
  return s;
}

String Stream::readStringUntil(char term) {
  String s;
  int c;
  while ((c = timedRead()) >= 0 && c != term) s.concat((char)c);
  return s;
}

bool Stream::find(const char* target) {
  const size_t len = strlen(target);
  if (!len) return true;
  size_t m = 0;
  int c;
  while ((c = timedRead()) >= 0) {
    if (c == target[m]) { if (++m == len) return true; }
    else m = (c == target[0]) ? 1 : 0;
  }
  return false;
}

long Stream::parseInt() {
  int c;
  while ((c = timedPeek()) >= 0 && !(isdigit(c) || c == '-')) read();
  bool neg = false;
  long v = 0;
  if (c == '-') { neg = true; read(); }
  while ((c = timedPeek()) >= 0 && isdigit(c)) { v = v * 10 + (c - '0'); read(); }
  return neg ? -v : v;
}

size_t Print::vprintf(const char* fmt, va_list ap) {
  char stackbuf[256];
  va_list ap2;
  va_copy(ap2, ap);
  int n = vsnprintf(stackbuf, sizeof(stackbuf), fmt, ap);
  if (n < 0) { va_end(ap2); return 0; }
  size_t w;
  if ((size_t)n < sizeof(stackbuf)) {
    w = write((const uint8_t*)stackbuf, (size_t)n);
  } else {
    std::string big((size_t)n + 1, '\0');
    vsnprintf(&big[0], big.size(), fmt, ap2);
    w = write((const uint8_t*)big.data(), (size_t)n);
  }
  va_end(ap2);
  return w;
}

size_t Print::printf(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  size_t n = vprintf(fmt, ap);
  va_end(ap);
  return n;
}
