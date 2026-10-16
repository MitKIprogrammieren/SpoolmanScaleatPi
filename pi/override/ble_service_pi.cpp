// Bluetooth (label printers) is not wired up on the Raspberry Pi build yet.
// The functions report "no stack", which the UI already handles: the label
// printing menu says Bluetooth is not available.
#include "services/ble_service.h"

bool bleEnabled() { return false; }
bool bleStackAvailable() { return false; }
void bleSetEnabled(bool) {}
int bleScan(BleDevice*, int, uint32_t, BleProgressFn) { return 0; }
BleWriteResult bleWriteBlocks(const char*, uint16_t, uint16_t, const BleBlock*, int, BleProgressFn,
                              const BleDone*) {
  return BLE_WRITE_INIT_FAILED;
}
BleSessionPhase bleSessionPhase() { return BLE_PHASE_IDLE; }
void bleSessionBytes(size_t* sent, size_t* total) {
  if (sent) *sent = 0;
  if (total) *total = 0;
}
bool bleStackStuck() { return false; }
