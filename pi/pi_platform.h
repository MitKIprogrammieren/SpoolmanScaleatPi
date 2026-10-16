// Raspberry Pi specifics the shared firmware code calls into.
#pragma once

// At boot: wait briefly for the OS network and, when no WiFi is configured in
// the scale's settings, take the network the Pi is on (its SSID, or
// "Ethernet") as the configured one.
void piAdoptOsNetwork();

// The Pi's host name in lower case, as the default device name (it is what
// avahi announces as <name>.local).
const char* piDefaultDeviceLabel();
