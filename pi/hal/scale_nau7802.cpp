// NAU7802 load cell ADC on the Pi's I2C bus (GPIO2 SDA / GPIO3 SCL).
// Register-level replacement for the Adafruit library, with the same setup
// the ESP build used: LDO 3.0 V, gain 128, 10 samples/s, internal calibration.

#include "hardware/scale.h"

#include "app_config.h"

namespace {
constexpr uint8_t REG_PU_CTRL = 0x00;
constexpr uint8_t REG_CTRL1 = 0x01;
constexpr uint8_t REG_CTRL2 = 0x02;
constexpr uint8_t REG_ADCO_B2 = 0x12;
constexpr uint8_t REG_ADC = 0x15;
constexpr uint8_t REG_PGA = 0x1B;
constexpr uint8_t REG_POWER = 0x1C;
constexpr uint8_t REG_REVISION = 0x1F;

constexpr uint8_t PU_RR = 1 << 0, PU_PUD = 1 << 1, PU_PUA = 1 << 2, PU_PUR = 1 << 3;
constexpr uint8_t PU_CS = 1 << 4, PU_CR = 1 << 5, PU_AVDDS = 1 << 7;
constexpr uint8_t CTRL2_CALS = 1 << 2, CTRL2_CAL_ERR = 1 << 3;

constexpr uint8_t LDO_3V0 = 5;
constexpr uint8_t GAIN_128 = 7;
constexpr uint8_t RATE_10SPS = 0;

#define CAL_MAX_ATTEMPTS 30
#define CAL_RETRY_DELAY_MS 100
#define CAL_ATTEMPT_MS 1500
// Like the ESP build: a broken load cell must not hold up the boot for long.
#define CAL_TOTAL_MS 6000

TwoWire* s_wire = nullptr;

bool writeReg(uint8_t reg, uint8_t val) {
  s_wire->beginTransmission(I2C_ADDR_NAU7802);
  s_wire->write(reg);
  s_wire->write(val);
  return s_wire->endTransmission() == 0;
}

bool readRegs(uint8_t reg, uint8_t* out, size_t n) {
  s_wire->beginTransmission(I2C_ADDR_NAU7802);
  s_wire->write(reg);
  if (s_wire->endTransmission(false) != 0) return false;
  if (s_wire->requestFrom((uint8_t)I2C_ADDR_NAU7802, n) != n) return false;
  for (size_t i = 0; i < n; i++) out[i] = (uint8_t)s_wire->read();
  return true;
}

bool readReg(uint8_t reg, uint8_t* val) { return readRegs(reg, val, 1); }

bool updateBits(uint8_t reg, uint8_t mask, uint8_t value) {
  uint8_t v;
  if (!readReg(reg, &v)) return false;
  v = (uint8_t)((v & ~mask) | (value & mask));
  return writeReg(reg, v);
}

bool powerUp() {
  // Reset, then power up the digital and analog parts and wait for "ready".
  if (!updateBits(REG_PU_CTRL, PU_RR, PU_RR)) return false;
  delay(10);
  if (!updateBits(REG_PU_CTRL, PU_RR | PU_PUD, PU_PUD)) return false;
  delay(1);
  uint8_t v = 0;
  for (int i = 0; i < 100; i++) {
    if (readReg(REG_PU_CTRL, &v) && (v & PU_PUR)) break;
    delay(2);
  }
  if (!(v & PU_PUR)) return false;
  if (!updateBits(REG_PU_CTRL, PU_PUD | PU_PUA, PU_PUD | PU_PUA)) return false;
  delay(600);   // the analog part needs this long after PUA
  if (!updateBits(REG_PU_CTRL, PU_CS, PU_CS)) return false;
  return readReg(REG_PU_CTRL, &v) && (v & PU_PUR);
}

bool configure() {
  uint8_t rev = 0;
  if (!readReg(REG_REVISION, &rev) || (rev & 0x0F) != 0x0F) return false;
  // Internal LDO at 3.0 V.
  if (!updateBits(REG_CTRL1, 0x38, LDO_3V0 << 3)) return false;
  if (!updateBits(REG_PU_CTRL, PU_AVDDS, PU_AVDDS)) return false;
  if (!updateBits(REG_CTRL1, 0x07, GAIN_128)) return false;
  if (!updateBits(REG_CTRL2, 0x70, RATE_10SPS << 4)) return false;
  if (!updateBits(REG_ADC, 0x30, 0x30)) return false;     // ADC chopper clock off
  if (!updateBits(REG_PGA, 0x40, 0x00)) return false;     // low ESR caps
  if (!updateBits(REG_POWER, 0x80, 0x80)) return false;   // PGA output cap
  return true;
}

bool calibrateInternal() {
  if (!updateBits(REG_CTRL2, 0x03, 0x00)) return false;   // CALMOD = internal offset
  if (!updateBits(REG_CTRL2, CTRL2_CALS, CTRL2_CALS)) return false;
  // CALS clears when the calibration is done (a few conversions at 10 SPS).
  uint8_t v = CTRL2_CALS;
  const unsigned long t0 = millis();
  while ((v & CTRL2_CALS) && millis() - t0 < CAL_ATTEMPT_MS) {
    delay(10);
    if (!readReg(REG_CTRL2, &v)) return false;
  }
  return !(v & CTRL2_CALS) && !(v & CTRL2_CAL_ERR);
}
}  // namespace

bool scaleHardwarePresent() {
  if (!s_wire) return false;
  s_wire->beginTransmission(I2C_ADDR_NAU7802);
  return s_wire->endTransmission() == 0;
}

bool scaleHardwareBegin(TwoWire* wire, void (*calibration_wait_cb)()) {
  s_wire = wire;
  if (!scaleHardwarePresent()) {
    Serial.printf("NAU7802 does not acknowledge 0x%02X\n", I2C_ADDR_NAU7802);
    return false;
  }
  if (!powerUp() || !configure()) {
    Serial.println("NAU7802 answered but would not start");
    return false;
  }
  delay(300);
  const unsigned long t0 = millis();
  for (int attempt = 0; attempt < CAL_MAX_ATTEMPTS && millis() - t0 < CAL_TOTAL_MS; attempt++) {
    if (calibrateInternal()) return true;
    if (calibration_wait_cb) calibration_wait_cb();
    else delay(CAL_RETRY_DELAY_MS);
  }
  Serial.println("NAU7802 calibration timed out");
  return false;
}

bool scaleHardwareAvailable() {
  if (!scaleHardwarePresent()) return false;
  uint8_t v = 0;
  return readReg(REG_PU_CTRL, &v) && (v & PU_CR);
}

int32_t scaleHardwareReadRaw() {
  uint8_t b[3] = {0, 0, 0};
  if (!readRegs(REG_ADCO_B2, b, 3)) return -1;
  int32_t v = ((int32_t)b[0] << 16) | ((int32_t)b[1] << 8) | b[2];
  if (v & 0x800000) v |= (int32_t)0xFF000000;
  return v;
}
