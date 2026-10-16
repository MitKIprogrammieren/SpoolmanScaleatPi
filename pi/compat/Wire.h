// TwoWire on Linux i2c-dev (/dev/i2c-1 by default, SPOOLMANSCALE_I2C overrides).
#pragma once
#include <vector>
#include "Arduino.h"

class TwoWire : public Stream {
 public:
  explicit TwoWire(uint8_t bus_num = 0) : bus_num_(bus_num) {}
  bool begin(int sda = -1, int scl = -1, uint32_t freq = 0);
  bool begin(uint8_t addr, int sda, int scl, uint32_t freq) { (void)addr; return begin(sda, scl, freq); }
  bool end();
  bool setClock(uint32_t) { return true; }
  void setTimeOut(uint16_t ms) { timeout_ms_ = ms; }
  void setTimeout(uint16_t ms) { timeout_ms_ = ms; }
  uint16_t getTimeOut() { return timeout_ms_; }

  void beginTransmission(uint8_t addr);
  void beginTransmission(int addr) { beginTransmission((uint8_t)addr); }
  uint8_t endTransmission(bool send_stop = true);
  size_t requestFrom(uint8_t addr, size_t len, bool send_stop = true);
  uint8_t requestFrom(int addr, int len) { return (uint8_t)requestFrom((uint8_t)addr, (size_t)len, true); }
  uint8_t requestFrom(uint8_t addr, uint8_t len) { return (uint8_t)requestFrom(addr, (size_t)len, true); }

  size_t write(uint8_t c) override;
  size_t write(const uint8_t* buf, size_t n) override;
  using Print::write;
  int available() override { return (int)(rx_.size() - rx_pos_); }
  int read() override { return rx_pos_ < rx_.size() ? rx_[rx_pos_++] : -1; }
  int peek() override { return rx_pos_ < rx_.size() ? rx_[rx_pos_] : -1; }
  void flush() override {}
  const char* device() const { return dev_.c_str(); }

 private:
  uint8_t bus_num_;
  int fd_ = -1;
  std::string dev_;
  uint16_t timeout_ms_ = 50;
  uint8_t tx_addr_ = 0;
  std::vector<uint8_t> tx_;
  bool pending_write_ = false;        // endTransmission(false): kept for a repeated start
  std::vector<uint8_t> pending_;
  uint8_t pending_addr_ = 0;
  std::vector<uint8_t> rx_;
  size_t rx_pos_ = 0;
};

extern TwoWire Wire;
extern TwoWire Wire1;
