#include "Wire.h"

#include <cerrno>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <unistd.h>

TwoWire Wire(0);
TwoWire Wire1(1);

bool TwoWire::begin(int, int, uint32_t) {
  if (fd_ >= 0) return true;
  const char* env = getenv("SPOOLMANSCALE_I2C");
  dev_ = env && *env ? env : "/dev/i2c-1";
  fd_ = open(dev_.c_str(), O_RDWR | O_CLOEXEC);
  if (fd_ < 0) {
    fprintf(stderr, "I2C: cannot open %s (%s). Enable I2C with raspi-config.\n", dev_.c_str(), strerror(errno));
    return false;
  }
  return true;
}

bool TwoWire::end() {
  if (fd_ >= 0) close(fd_);
  fd_ = -1;
  return true;
}

void TwoWire::beginTransmission(uint8_t addr) {
  tx_addr_ = addr;
  tx_.clear();
}

size_t TwoWire::write(uint8_t c) {
  tx_.push_back(c);
  return 1;
}
size_t TwoWire::write(const uint8_t* buf, size_t n) {
  tx_.insert(tx_.end(), buf, buf + n);
  return n;
}

// Arduino codes: 0 ok, 2 address NACK, 3 data NACK, 4 other, 5 timeout.
uint8_t TwoWire::endTransmission(bool send_stop) {
  if (fd_ < 0) return 4;
  if (!send_stop) {
    // The read that follows goes out as one transaction with a repeated start.
    pending_write_ = true;
    pending_ = tx_;
    pending_addr_ = tx_addr_;
    return 0;
  }
  pending_write_ = false;
  if (tx_.empty()) {
    // An address probe: SMBus quick write, as i2cdetect does.
    if (ioctl(fd_, I2C_SLAVE, tx_addr_) < 0) return 4;
    struct i2c_smbus_ioctl_data d = {I2C_SMBUS_WRITE, 0, I2C_SMBUS_QUICK, nullptr};
    return ioctl(fd_, I2C_SMBUS, &d) < 0 ? 2 : 0;
  }
  struct i2c_msg m = {tx_addr_, 0, (uint16_t)tx_.size(), tx_.data()};
  struct i2c_rdwr_ioctl_data d = {&m, 1};
  if (ioctl(fd_, I2C_RDWR, &d) < 0) return errno == ETIMEDOUT ? 5 : 2;
  return 0;
}

size_t TwoWire::requestFrom(uint8_t addr, size_t len, bool) {
  rx_.assign(len, 0);
  rx_pos_ = 0;
  if (fd_ < 0 || !len) { rx_.clear(); return 0; }
  struct i2c_msg msgs[2];
  int n = 0;
  if (pending_write_ && pending_addr_ == addr && !pending_.empty()) {
    msgs[n++] = {addr, 0, (uint16_t)pending_.size(), pending_.data()};
  }
  msgs[n++] = {addr, I2C_M_RD, (uint16_t)len, rx_.data()};
  struct i2c_rdwr_ioctl_data d = {msgs, (uint32_t)n};
  pending_write_ = false;
  if (ioctl(fd_, I2C_RDWR, &d) < 0) {
    rx_.clear();
    return 0;
  }
  return len;
}
