#pragma once
#include <cstdint>

class EspClass {
 public:
  uint32_t getFreeHeap();
  uint32_t getMinFreeHeap() { return getFreeHeap(); }
  uint32_t getHeapSize();
  uint32_t getMaxAllocHeap() { return getFreeHeap(); }
  uint32_t getFreePsram() { return 0; }
  uint32_t getPsramSize() { return 0; }
  // The app slot of partitions.csv (6 MB), so the firmware's partition
  // checks find the "current" layout and never ask for a USB re-flash.
  uint32_t getFreeSketchSpace() { return 0x600000; }
  uint32_t getSketchSize();
  uint64_t getEfuseMac();
  uint32_t getCpuFreqMHz() { return 1500; }
  const char* getChipModel() { return "Raspberry Pi"; }
  [[noreturn]] void restart();
};

extern EspClass ESP;
