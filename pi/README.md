# SpoolmanScale auf dem Raspberry Pi

Diese Version ersetzt den ESP32 (WT32-SC01 Plus) und sein Display durch einen
**Raspberry Pi 4** mit dem **BIGTREETECH PI TFT50 V2** (5", 800×480, DSI,
kapazitiver Touch). Waage (NAU7802 + Wägezelle) und NFC-Reader (PN532) bleiben.

Die Firmware selbst ist dieselbe wie auf dem ESP: Der Ordner `src/` wird
unverändert unter Linux übersetzt. Darunter liegt in `pi/` eine kleine
Arduino-Schicht für Linux und Treiber für Display, Touch, Waage und NFC.

## Hardware

| Teil | Modell |
|---|---|
| Rechner | Raspberry Pi 4 (jede RAM-Größe) |
| Display | BIGTREETECH PI TFT50 V2, per DSI-Flachbandkabel |
| Waagen-Wandler | NAU7802 (Adafruit empfohlen) + YZC-133 Wägezelle |
| NFC-Reader | PN532-Modul, **im HSU-Modus (UART)** |

### Verdrahtung

Pin-Nummern sind die der 40-poligen Stiftleiste des Pi.

**NAU7802 → I²C**

| NAU7802 | Pi-Pin |
|---|---|
| VIN | Pin 1 (3,3V) |
| GND | Pin 9 (GND) |
| SDA | Pin 3 (GPIO2) |
| SCL | Pin 5 (GPIO3) |

Die Wägezelle bleibt am NAU7802 wie beim ESP-Aufbau.

**PN532 → UART**

DIP-Schalter am Modul auf **HSU**: SW1 OFF, SW2 OFF.

| PN532 | Pi-Pin |
|---|---|
| VCC | Pin 2 (5V) |
| GND | Pin 6 (GND) |
| TXD | Pin 10 (GPIO15 / RXD) |
| RXD | Pin 8 (GPIO14 / TXD) |

TX und RX werden über Kreuz verbunden. Die üblichen PN532-Module haben einen
eigenen 3,3V-Regler, ihre Datenleitungen sind also Pi-verträglich.

Warum nicht I²C wie beim ESP? Der I²C-Controller des Pi kommt mit dem
„Clock Stretching“ des PN532 schlecht zurecht, was zu sporadischen
Lesefehlern führt. Wer es trotzdem probieren will: PN532 an Pin 3/5 parallel
zum NAU7802, DIP-Schalter auf I²C und `SPOOLMANSCALE_PN532=i2c` setzen
(siehe Einstellungen).

**Display**

Das TFT50 wird nur mit dem DSI-Flachbandkabel an den DSI-Anschluss des Pi
angeschlossen. Die GPIO-Leiste bleibt frei.

## Raspberry Pi OS vorbereiten

1. **Raspberry Pi OS Lite (64-bit)** mit dem Raspberry Pi Imager auf die
   SD-Karte schreiben. Im Imager gleich WLAN, Benutzer und SSH einrichten.
   Eine Desktop-Version funktioniert nur, wenn der Pi in die Konsole statt in
   den Desktop startet, weil der Desktop sonst das Display belegt.
2. Display prüfen: Mit `display_auto_detect=1` (Standard in
   `/boot/firmware/config.txt`) wird das TFT50 in der Regel ohne Zutun
   erkannt. Bleibt es schwarz, diese Zeile in `config.txt` ergänzen und neu
   starten:
   ```
   dtoverlay=vc4-kms-dsi-7inch
   ```
   Reagiert der Touch nicht, berichten Nutzer, dass `dtoverlay=vc4-fkms-v3d`
   statt `vc4-kms-v3d` hilft.

## Installieren

Auf dem Pi:

```bash
sudo apt install -y git
git clone -b pi-port https://github.com/MitKIprogrammieren/SpoolmanScaleatPi.git
cd SpoolmanScaleatPi
sudo ./pi/install.sh
sudo reboot
```

Das Skript installiert die Pakete, schaltet I²C und die serielle
Schnittstelle ein (ohne Login-Konsole darauf), stellt den Start auf Konsole
statt Desktop, baut das Programm und richtet den Dienst `spoolmanscale` ein,
der beim Booten startet. Der erste Build dauert auf dem Pi 4 einige Minuten.

Nach dem Neustart erscheint die Waage auf dem Display. Die Einrichtung läuft
wie beim ESP: Sprache, dann Filamentverwaltung und Server. Das WLAN wird nicht
abgefragt, die Waage übernimmt das Netzwerk des Pi (WLAN oder Ethernet).

### Aktualisieren

```bash
cd SpoolmanScaleatPi
git pull
sudo ./pi/install.sh
```

Die Firmware-Update-Funktion des ESP (GitHub, Web-Flasher) ist auf dem Pi
ausgeblendet.

### Nützliche Befehle

```bash
systemctl status spoolmanscale       # läuft der Dienst?
journalctl -u spoolmanscale -f       # Log live
sudo systemctl restart spoolmanscale
i2cdetect -y 1                       # NAU7802 muss als 2a erscheinen
```

Einstellungen, Logs und der Bambu-Katalog liegen in `/var/lib/spoolmanscale`.
Ein Werksreset in der Waage löscht die Einstellungen dort.

## Einstellungen

In `/etc/default/spoolmanscale` (danach `sudo systemctl restart spoolmanscale`):

| Variable | Wirkung |
|---|---|
| `SPOOLMANSCALE_NATIVE=1` | Test: Vollbild 800×480, scharf gezeichnet (siehe unten) |
| `SPOOLMANSCALE_THEME=original` | ursprüngliches blaues Farbschema statt FilamentLager-Look |
| `SPOOLMANSCALE_ROTATE=180` | Bild und Touch um 180° drehen |
| `SPOOLMANSCALE_SCALE=nearest` | scharfe statt weiche Skalierung |
| `SPOOLMANSCALE_TOUCH_SWAP_XY=1` | Touch-Achsen tauschen |
| `SPOOLMANSCALE_TOUCH_INVERT_X=1` / `_Y=1` | Touch-Achse spiegeln |
| `SPOOLMANSCALE_TOUCH=/dev/input/eventN` | bestimmtes Touch-Gerät |
| `SPOOLMANSCALE_PN532=/dev/ttyUSB0` | PN532 an anderem Port (z. B. USB-Adapter) |
| `SPOOLMANSCALE_PN532=i2c` | PN532 am I²C-Bus |
| `SPOOLMANSCALE_FB=/dev/fb0` | anderer Framebuffer |
| `SPOOLMANSCALE_I2C=/dev/i2c-1` | anderer I²C-Bus |
| `SPOOLMANSCALE_BACKLIGHT=/sys/class/backlight/…` | Hintergrundbeleuchtung |
| `SPOOLMANSCALE_DATA=/pfad` | anderer Datenordner |

## Display: 480×320 auf 800×480

Die Oberfläche ist für 480×320 gebaut. Auf dem TFT50 wird sie um den Faktor
1,5 auf **720×480** vergrößert und mittig gezeigt, links und rechts bleibt je
ein schwarzer Rand von 40 Pixeln. Die Touch-Koordinaten werden zurückgerechnet.
Die Skalierung ist weich (bilinear), mit `SPOOLMANSCALE_SCALE=nearest` scharf.

### Vollbild, nativ 800×480 (Test)

Mit `SPOOLMANSCALE_NATIVE=1` zeichnet LVGL direkt in voller Auflösung über
den ganzen Bildschirm, ohne Ränder und ohne Hochskalieren, also gestochen
scharf. Die Bildschirme der Firmware sind weiter für 480×320 geschrieben;
`pi/native/native_scale.h` rechnet beim Übersetzen jede Position, Größe,
Ecke, Linie und Schrift um (Breite ×1,67, Höhe ×1,5, Schriften eine passende
größere Montserrat-Größe). Der Modus ist neu, einzelne Bildschirme können
noch kleine Unschönheiten haben. Ohne die Variable läuft alles wie bisher.

### Farben

Das dunkle Farbschema ist auf dem Pi an die FilamentLager-Karte in Home
Assistant angelehnt: fast schwarzer Hintergrund, dunkelgraue Karten mit
grünem Rahmen, grüne Schaltflächen, Orange/Rot/Grün für den Status. Die
hellen und die Spoolman/FilaMan-Schemata bleiben unverändert.

Helligkeit und Abschalten im Ruhezustand laufen über
`/sys/class/backlight`, falls das Display dort einen Regler anbietet,
sonst über das Abschalten des Bildschirms.

## Home Assistant FilamentLager als Filamentverwaltung

Unter Einstellungen → Verbindung → Filamentverwaltung gibt es auf dem Pi
neben Spoolman, FilaMan und BamBuddy einen vierten Knopf **Home Assistant**.
Die Waage arbeitet dann direkt mit der Liste der FilamentLager-Karte
(„Ernsthaft gelagert“, `todo.filament`) in Home Assistant.

Dazwischen sitzt eine kleine Brücke auf dem Pi
(`pi/ha_bridge/filamentlager_ha_bridge.py`, Dienst
`filamentlager-ha-bridge`). Sie spricht nach innen die Sprache von Spoolman
und liest und schreibt nach außen die Liste in Home Assistant.

Einmal einrichten:

1. In Home Assistant unten links auf das Profil → Reiter „Sicherheit“ →
   ganz unten „Token erstellen“, Namen vergeben, Token kopieren.
2. Auf dem Pi:
   ```bash
   sudo filamentlager-ha-bridge --setup
   ```
   Adresse (z. B. `http://homeassistant.local:8123`) und Token eingeben. Am
   Ende steht, wie viele Filamentsorten gefunden wurden.
3. An der Waage Filamentverwaltung → **Home Assistant** wählen. Die Adresse
   `127.0.0.1:7913` (die Brücke) ist schon eingetragen.

So passen die beiden Welten zusammen:

| Waage | Home Assistant |
|---|---|
| eine „Spule“ mit ID | ein Eintrag der Liste (eine Filamentsorte) |
| Restgewicht | der Rest der gerade gescannten Rolle aus `reste`, sonst eine volle Rolle |
| NFC-Tag verknüpfen | mehrere Tags pro Sorte möglich, einer pro Rolle |
| Gewicht updaten | ersetzt den Rest dieser Rolle; eine volle Rolle wird zu einem neuen Rest |
| leer (unter 5 g) | Rest fällt weg, `spulen` sinkt um 1 |
| Lagerort | zeigt „3 Rollen (1 offen)“ |

Alles andere im Eintrag (Codes, Bambu-ID, Temperaturen, Preis …) bleibt
unverändert. Was Home Assistant nicht speichert – welche Tags zu welcher
Sorte gehören, Leergewichte der Spulen, das Trocknungsdatum – liegt auf dem
Pi in `/var/lib/spoolmanscale/ha_bridge_state.json`. Neue Sorten legt man
weiter in der FilamentLager-Karte an.

**Lager auf der Waage:** Im Home-Assistant-Modus hat das Einstellungsmenü
(≡ unten rechts) unten den grünen Knopf **Lager**. Er zeigt alle Sorten mit
Farbe, Rollen, offenen Resten und Bestand, filterbar nach PLA, PETG, ABS,
ASA, TPU, Andere und Leer.

**Design:** Daneben öffnet **Design** die Einstellungen der FilamentLager-Karte:
Farbe der Umrandungen, Rahmenstärke, Ansicht der Liste, Farbnamen
(Deutsch + Englisch) und die Silica-Warnschwelle. Sie liegen in Home Assistant
und gelten überall – eine Änderung auf der Waage sieht man in der Karte und
umgekehrt. Dazu kommt der Schalter **Vollbild / Klassisch** für die Waage.
Farbe und Bildschirm gelten nach einem Neustart, der beim Verlassen angeboten
wird.

Prüfen: `filamentlager-ha-bridge --check`, Log:
`journalctl -u filamentlager-ha-bridge -f`.

## Backup auf die Synology

Einmal einrichten:

```bash
sudo filamentlager-backup --setup
```

Gefragt werden Adresse der Synology, freigegebener Ordner (`Backup`),
Unterordner (`raspberry`), Benutzer und Passwort. Danach legt der Pi jede
Nacht um 3:30 Uhr ein Archiv `filamentlager-<name>-<datum>.tar.gz` dort ab und
hebt die letzten 14 auf. Gesichert werden Einstellungen, Kalibrierung, Tags und
Logs der Waage, die Home-Assistant-Brücke und `/etc/default/spoolmanscale`.

Sofort sichern: `sudo filamentlager-backup`, Liste: `sudo filamentlager-backup --list`.
An der Synology muss SMB eingeschaltet sein (Systemsteuerung → Dateidienste).

Zurückspielen auf einem neu aufgesetzten Pi (nach `install.sh`):

```bash
sudo systemctl stop spoolmanscale filamentlager-ha-bridge
sudo tar -xzf filamentlager-....tar.gz -C /
sudo systemctl start filamentlager-ha-bridge spoolmanscale
```

## Was anders ist als beim ESP

| Funktion | Pi |
|---|---|
| Oberfläche, Backends, Tags, Waage, Web-Oberfläche | wie beim ESP |
| WLAN | vom Betriebssystem; der WLAN-Bildschirm nutzt NetworkManager (`nmcli`) |
| Einrichtungs-Hotspot, Improv, WLAN-Roaming | entfällt, macht das OS |
| Firmware-Update per GitHub/Browser | ausgeblendet, Update mit `git pull` |
| Deep Sleep | entfällt, das Display wird stattdessen abgeschaltet |
| Etikettendruck per Bluetooth | noch nicht umgesetzt |
| SD-Karten-Log | liegt in `/var/lib/spoolmanscale/sd` |
| mDNS | Hostname kommt vom Pi; der Webdienst wird per avahi angekündigt |

## Aufbau des Codes

```
pi/
  CMakeLists.txt     Build: src/ + LVGL 8.3.11 + ArduinoJson 7.4.3
  main_pi.cpp        main() ruft setup()/loop() der Firmware
  compat/            Arduino- und ESP-IDF-Schnittstellen auf Linux
                     (String, Serial, WiFi, HTTPClient über libcurl,
                      WebServer, Preferences als Dateien, Wire über i2c-dev,
                      FreeRTOS über pthreads, Flash-Partitionen als Dateien)
  hal/               Display + Touch, NAU7802, PN532
  override/          Pi-Fassungen der wenigen ESP-only-Dateien aus src/,
                     FilamentLager-Farben (pi_theme.cpp)
  native/            Vollbild-Modus: Umrechnung 480x320 -> 800x480
  systemd/           Dienst und Einstellungsdatei
  install.sh         Pakete, Build, Installation
```

In `src/` sind nur wenige, mit `#ifdef SPOOLMANSCALE_PI` markierte Stellen
geändert. Neue Versionen des Originalprojekts lassen sich dadurch meist
direkt übernehmen (`git merge` von `Niko11111/SpoolmanScale`).

## Ohne Hardware testen

Das Programm läuft auch auf einem normalen Linux-PC. Statt ins Display
schreibt es dann in eine Bilddatei, Touch kommt über eine Pipe:

```bash
cmake -S pi -B build && cmake --build build -j
SPOOLMANSCALE_FB=file:/tmp/fb.raw SPOOLMANSCALE_TOUCH=pipe:/tmp/touch \
SPOOLMANSCALE_DATA=/tmp/ssdata ./build/spoolmanscale &
echo "tap 400 428" > /tmp/touch     # Tippen bei x=400, y=428 (Display-Pixel)
# /tmp/fb.raw ist ein 800x480-Bild im Format XRGB8888
```
