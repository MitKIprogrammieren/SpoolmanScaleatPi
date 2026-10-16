#pragma once

#include "Print.h"

extern "C" unsigned long millis(void);

class Stream : public Print {
 public:
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;

  void setTimeout(unsigned long ms) { timeout_ = ms; }
  unsigned long getTimeout() const { return timeout_; }

  virtual size_t readBytes(char* buf, size_t len);
  size_t readBytes(uint8_t* buf, size_t len) { return readBytes((char*)buf, len); }
  size_t readBytesUntil(char term, char* buf, size_t len);
  String readString();
  String readStringUntil(char term);
  bool find(const char* target);
  bool find(char c) { char t[2] = {c, 0}; return find(t); }
  long parseInt();

 protected:
  int timedRead();
  int timedPeek();
  unsigned long timeout_ = 1000;
};
