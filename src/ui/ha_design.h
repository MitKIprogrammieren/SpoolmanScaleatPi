#pragma once

#include <stdint.h>

// The look the Home Assistant FilamentLager card is set to - colour of the
// frames, frame width, compact list, colour names in both languages - kept
// on the scale so the palette has it at boot, before the network is up.
// The card keeps it in Home Assistant; the bridge passes it on
// (/api/v1/design) and the Design page writes it back, so a change on the
// phone reaches the scale and the other way round.
struct HaDesign {
  uint32_t accent = 0x4caf50;
  uint8_t  frame = 2;          // 1 thin, 2 normal, 3 strong
  bool     compact = false;
  bool     bilingual = true;
};

const HaDesign& haDesign();
// Stores what came from Home Assistant; true when the accent changed, which
// the palette only takes on at the next start.
bool haDesignStore(const HaDesign& d);
// The colour a palette started with, to tell whether a restart is due.
uint32_t haDesignBootAccent();
