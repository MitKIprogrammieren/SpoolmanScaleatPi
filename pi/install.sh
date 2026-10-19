#!/bin/bash
# SpoolmanScale for Raspberry Pi - install or update.
#
#   git clone -b pi-port https://github.com/MitKIprogrammieren/SpoolmanScaleatPi.git
#   cd SpoolmanScaleatPi
#   sudo ./pi/install.sh
#
# Run it again after "git pull" to update.
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
  echo "Bitte mit sudo starten: sudo ./pi/install.sh" >&2
  exit 1
fi

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$REPO/build-pi"

PKGS="build-essential cmake git pkg-config libcurl4-openssl-dev i2c-tools avahi-utils python3 cifs-utils"
# Only when something is missing: apt-get update alone takes a minute.
if dpkg -s $PKGS >/dev/null 2>&1; then
  echo "== Pakete: alles da"
else
  echo "== Pakete"
  apt-get update
  apt-get install -y $PKGS
fi

echo "== I2C und serielle Schnittstelle einschalten"
if command -v raspi-config >/dev/null; then
  raspi-config nonint do_i2c 0          # I2C an
  raspi-config nonint do_serial_hw 0    # UART an (GPIO14/15)
  raspi-config nonint do_serial_cons 1  # keine Login-Konsole auf dem UART
  raspi-config nonint do_boot_behaviour B1 || true   # Konsole statt Desktop
fi
# Zweiter NFC-Leser: UART3 auf GPIO4 (Pin 7) / GPIO5 (Pin 29)
CFG=/boot/firmware/config.txt
[ -f "$CFG" ] || CFG=/boot/config.txt
if [ -f "$CFG" ] && ! grep -q '^dtoverlay=uart3' "$CFG"; then
  printf '\n[all]\n# FilamentLager: zweiter PN532\ndtoverlay=uart3\n' >> "$CFG"
  echo "UART3 für den zweiten NFC-Leser eingeschaltet (wirkt nach dem Neustart)"
fi

echo "== Bauen (dauert beim ersten Mal einige Minuten)"
cmake -S "$REPO/pi" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" -j"$(nproc)"

echo "== Installieren"
systemctl stop spoolmanscale 2>/dev/null || true
install -m 0755 "$BUILD/spoolmanscale" /usr/local/bin/spoolmanscale
install -m 0644 "$REPO/pi/systemd/spoolmanscale.service" /etc/systemd/system/spoolmanscale.service
# Home Assistant FilamentLager bridge (python3, standard library only)
install -m 0755 "$REPO/pi/ha_bridge/filamentlager_ha_bridge.py" /usr/local/bin/filamentlager-ha-bridge
install -m 0644 "$REPO/pi/systemd/filamentlager-ha-bridge.service" /etc/systemd/system/filamentlager-ha-bridge.service
# Daily backup to the Synology (configured with filamentlager-backup --setup)
install -m 0755 "$REPO/pi/backup/filamentlager-backup" /usr/local/bin/filamentlager-backup
install -m 0644 "$REPO/pi/systemd/filamentlager-backup.service" /etc/systemd/system/filamentlager-backup.service
install -m 0644 "$REPO/pi/systemd/filamentlager-backup.timer" /etc/systemd/system/filamentlager-backup.timer
if [ ! -f /etc/default/spoolmanscale ]; then
  install -m 0644 "$REPO/pi/systemd/spoolmanscale.default" /etc/default/spoolmanscale
fi
# The login prompt on the screen would sit under the UI.
systemctl disable --now getty@tty1 2>/dev/null || true
systemctl daemon-reload
systemctl enable spoolmanscale filamentlager-ha-bridge filamentlager-backup.timer
systemctl start filamentlager-backup.timer
systemctl restart filamentlager-ha-bridge
systemctl restart spoolmanscale

echo
echo "Fertig. Status:  systemctl status spoolmanscale"
echo "Log:             journalctl -u spoolmanscale -f"
if [ ! -f /etc/filamentlager/backup.conf ]; then
  echo "Backup auf die Synology einrichten:      sudo filamentlager-backup --setup"
fi
if [ ! -f /etc/filamentlager/ha.json ]; then
  echo "Home Assistant FilamentLager einrichten:  sudo filamentlager-ha-bridge --setup"
fi
echo "Nach dem ersten Einschalten von I2C/UART einmal neu starten: sudo reboot"
