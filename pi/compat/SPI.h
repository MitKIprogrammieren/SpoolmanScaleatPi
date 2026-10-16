#pragma once
#include "Arduino.h"
#define HSPI 2
#define FSPI 1
#define VSPI 3
class SPIClass {
 public:
  explicit SPIClass(uint8_t bus = HSPI) { (void)bus; }
  void begin(int8_t sck = -1, int8_t miso = -1, int8_t mosi = -1, int8_t ss = -1) {
    (void)sck; (void)miso; (void)mosi; (void)ss;
  }
  void end() {}
};
extern SPIClass SPI;
