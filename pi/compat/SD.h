// The "SD card" of the Pi build is the directory <data>/sd.
#pragma once
#include "FS.h"
#include "SPI.h"

typedef enum { CARD_NONE, CARD_MMC, CARD_SD, CARD_SDHC, CARD_UNKNOWN } sdcard_type_t;

class SDFS : public fs::FS {
 public:
  SDFS() : FS("sd") {}
  bool begin(uint8_t ss = 0, SPIClass& spi = SPI, uint32_t freq = 4000000, const char* mount = "/sd",
             uint8_t max_files = 5, bool format_if_empty = false);
  void end() {}
  sdcard_type_t cardType() { return CARD_SDHC; }
  uint64_t cardSize();
  uint64_t totalBytes() { return cardSize(); }
  uint64_t usedBytes();
};
extern SDFS SD;
