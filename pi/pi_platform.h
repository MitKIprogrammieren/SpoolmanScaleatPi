// Raspberry Pi specifics the shared firmware code calls into.
#pragma once

// At boot: wait briefly for the OS network and, when no WiFi is configured in
// the scale's settings, take the network the Pi is on (its SSID, or
// "Ethernet") as the configured one.
void piAdoptOsNetwork();
