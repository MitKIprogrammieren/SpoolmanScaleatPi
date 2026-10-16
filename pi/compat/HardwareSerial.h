#pragma once
#include "Stream.h"

// Serial goes to stdout (journald under systemd); read() takes stdin without blocking.
class HardwareSerial : public Stream {
 public:
  void begin(unsigned long baud = 115200, uint32_t cfg = 0, int8_t rx = -1, int8_t tx = -1) {
    (void)baud; (void)cfg; (void)rx; (void)tx;
  }
  void end() {}
  void setTxTimeoutMs(uint32_t) {}
  void setRxBufferSize(size_t) {}
  size_t write(uint8_t c) override;
  size_t write(const uint8_t* buf, size_t n) override;
  using Print::write;
  int available() override;
  int read() override;
  int peek() override;
  void flush() override;
  operator bool() const { return true; }
 private:
  int peeked_ = -1;
};

extern HardwareSerial Serial;
