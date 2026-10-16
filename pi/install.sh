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

echo "== Pakete"
apt-get update
apt-get install -y build-essential cmake git pkg-config libcurl4-openssl-dev i2c-tools avahi-utils

echo "== I2C und serielle Schnittstelle einschalten"
if command -v raspi-config >/dev/null; then
  raspi-config nonint do_i2c 0          # I2C an
  raspi-config nonint do_serial_hw 0    # UART an (GPIO14/15)
  raspi-config nonint do_serial_cons 1  # keine Login-Konsole auf dem UART
  raspi-config nonint do_boot_behaviour B1 || true   # Konsole statt Desktop
fi

echo "== Bauen (dauert beim ersten Mal einige Minuten)"
cmake -S "$REPO/pi" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" -j"$(nproc)"

echo "== Installieren"
systemctl stop spoolmanscale 2>/dev/null || true
install -m 0755 "$BUILD/spoolmanscale" /usr/local/bin/spoolmanscale
install -m 0644 "$REPO/pi/systemd/spoolmanscale.service" /etc/systemd/system/spoolmanscale.service
if [ ! -f /etc/default/spoolmanscale ]; then
  install -m 0644 "$REPO/pi/systemd/spoolmanscale.default" /etc/default/spoolmanscale
fi
# The login prompt on the screen would sit under the UI.
systemctl disable --now getty@tty1 2>/dev/null || true
systemctl daemon-reload
systemctl enable spoolmanscale
systemctl restart spoolmanscale

echo
echo "Fertig. Status:  systemctl status spoolmanscale"
echo "Log:             journalctl -u spoolmanscale -f"
echo "Nach dem ersten Einschalten von I2C/UART einmal neu starten: sudo reboot"
