// Improv (WiFi provisioning over the USB serial port) belongs to the ESP web
// flasher. The Pi gets its network from the OS.
#include "services/improv_serial.h"

void improvSerialTick() {}
bool improvSerialBusy() { return false; }
bool improvSerialTakeProvisioned() { return false; }
