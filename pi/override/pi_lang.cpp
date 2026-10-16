// Texts that differ on the Raspberry Pi: the diagnostics explain the Pi's
// wiring (PN532 on the UART, NAU7802 on GPIO2/3) instead of the ESP board's
// 8-pin I/O connector.
#include "lang.h"

#include <cstring>
#include <map>
#include <mutex>
#include <string>

#include "product_name.h"

namespace {
struct PiText {
  int id;
  const char* text[3];   // DE, EN, FR
};

const PiText kTexts[] = {
  {STR_DIAG_BUS_EMPTY_BANNER,
   {"NFC-Reader und Waage fehlen", "NFC reader and scale missing", "Lecteur NFC et balance absents"}},
  {STR_DIAG_BUS_EMPTY_TITLE,
   {"NFC-Reader und Waage fehlen", "NFC reader and scale missing", "Lecteur NFC et balance absents"}},
  {STR_DIAG_BUS_EMPTY_TEXT,
   {"Weder der NFC-Reader (PN532) noch der Waagen-Wandler (NAU7802) antworten.\n\n"
    "Prüfe die Stromversorgung beider Module: PN532 an 5V (Pin 2) und GND (Pin 6), "
    "NAU7802 an 3,3V (Pin 1) und GND (Pin 9).\n\n"
    "Prüfe außerdem, ob I2C und die serielle Schnittstelle in raspi-config aktiviert sind "
    "(siehe pi/README.md).",
    "Neither the NFC reader (PN532) nor the scale converter (NAU7802) answers.\n\n"
    "Check the power of both modules: PN532 on 5V (pin 2) and GND (pin 6), "
    "NAU7802 on 3.3V (pin 1) and GND (pin 9).\n\n"
    "Also check that I2C and the serial port are enabled in raspi-config (see pi/README.md).",
    "Ni le lecteur NFC (PN532) ni le convertisseur de la balance (NAU7802) ne répondent.\n\n"
    "Vérifiez l'alimentation des deux modules : PN532 sur 5V (broche 2) et GND (broche 6), "
    "NAU7802 sur 3,3V (broche 1) et GND (broche 9).\n\n"
    "Vérifiez aussi que l'I2C et le port série sont activés dans raspi-config (voir pi/README.md)."}},
  {STR_DIAG_NAU_MISSING_TEXT,
   {"Der Waagen-Wandler (NAU7802) antwortet nicht auf 0x2A.\n\n"
    "Er hängt am I2C-Bus des Pi: SDA an GPIO2 (Pin 3), SCL an GPIO3 (Pin 5), VIN an 3,3V (Pin 1), "
    "GND an Pin 9.\n\nMit 'i2cdetect -y 1' lässt sich prüfen, ob 2a erscheint.",
    "The scale converter (NAU7802) does not answer at 0x2A.\n\n"
    "It sits on the Pi's I2C bus: SDA to GPIO2 (pin 3), SCL to GPIO3 (pin 5), VIN to 3.3V (pin 1), "
    "GND to pin 9.\n\n'i2cdetect -y 1' shows whether 2a appears.",
    "Le convertisseur de la balance (NAU7802) ne répond pas à l'adresse 0x2A.\n\n"
    "Il est sur le bus I2C du Pi : SDA sur GPIO2 (broche 3), SCL sur GPIO3 (broche 5), VIN sur 3,3V "
    "(broche 1), GND sur la broche 9.\n\n'i2cdetect -y 1' indique si 2a apparaît."}},
  {STR_DIAG_PN532_MISSING_TEXT,
   {"Der NFC-Reader (PN532) antwortet nicht an der seriellen Schnittstelle.\n\n"
    "Die DIP-Schalter am Modul müssen auf HSU stehen: SW1 OFF, SW2 OFF.\n\n"
    "Verkabelung: TXD des PN532 an GPIO15/RXD (Pin 10), RXD an GPIO14/TXD (Pin 8), "
    "VCC an 5V (Pin 2), GND an Pin 6.\n\n"
    "In raspi-config muss die serielle Schnittstelle an sein, die Login-Konsole darauf aus.",
    "The NFC reader (PN532) does not answer on the serial port.\n\n"
    "The module's DIP switches have to be on HSU: SW1 OFF, SW2 OFF.\n\n"
    "Wiring: PN532 TXD to GPIO15/RXD (pin 10), RXD to GPIO14/TXD (pin 8), "
    "VCC to 5V (pin 2), GND to pin 6.\n\n"
    "In raspi-config the serial port has to be on and the login console on it off.",
    "Le lecteur NFC (PN532) ne répond pas sur le port série.\n\n"
    "Les interrupteurs DIP du module doivent être sur HSU : SW1 OFF, SW2 OFF.\n\n"
    "Câblage : TXD du PN532 sur GPIO15/RXD (broche 10), RXD sur GPIO14/TXD (broche 8), "
    "VCC sur 5V (broche 2), GND sur la broche 6.\n\n"
    "Dans raspi-config, le port série doit être activé et la console de connexion désactivée."}},
};
}  // namespace

namespace {
// Every other text that names the product gets the Pi build's name instead,
// worked out once per text and language and kept.
const char* withProductName(int id, int lang) {
  const char* orig = STRINGS[id][lang] ? STRINGS[id][lang] : STRINGS[id][LANG_EN];
  if (!orig || !strstr(orig, "SpoolmanScale")) return nullptr;
  static std::mutex mu;
  static std::map<int, std::string> cache;
  std::lock_guard<std::mutex> lk(mu);
  const int key = id * 4 + lang;
  auto it = cache.find(key);
  if (it != cache.end()) return it->second.c_str();
  std::string out = orig;
  size_t p;
  while ((p = out.find("SpoolmanScale")) != std::string::npos) out.replace(p, 13, FW_PRODUCT_NAME);
  return cache.emplace(key, out).first->second.c_str();
}
}  // namespace

const char* piLangText(int id, int lang) {
  if (lang < 0 || lang > 2) lang = 1;
  for (const PiText& t : kTexts)
    if (t.id == id) return t.text[lang];
  return withProductName(id, lang);
}
