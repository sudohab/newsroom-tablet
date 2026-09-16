#!/usr/bin/env bash
#
# Sichert den kompletten 16-MB-Flash des ESP32-S3-Touch-LCD-7, bevor eigene
# Firmware daraufkommt. Damit lässt sich die Waveshare-Werksfirmware jederzeit
# wiederherstellen:
#
#     esptool --port <PORT> write_flash 0x0 backup/werksfirmware-<datum>.bin
#
# Aufruf:  scripts/backup_flash.sh [PORT]
#          PORT ist optional, Vorgabe /dev/ttyACM0
#
# Voraussetzung: esptool ab Version 4 (das Ubuntu-Paket 3.x kann den S3 nicht
# vollständig). Falls nicht vorhanden:
#     python3 -m venv ~/.venv/esptool && ~/.venv/esptool/bin/pip install esptool
#     ESPTOOL=~/.venv/esptool/bin/esptool scripts/backup_flash.sh

set -euo pipefail

PORT="${1:-/dev/ttyACM0}"
ESPTOOL="${ESPTOOL:-esptool}"
# Ordner relativ zum Projekt, nicht zum Arbeitsverzeichnis des Aufrufers
PROJEKT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ZIEL="$PROJEKT/backup"
DATEI="$ZIEL/werksfirmware-$(date +%Y-%m-%d).bin"

if [ ! -e "$PORT" ]; then
    echo "Serieller Port $PORT nicht gefunden. Display angesteckt?" >&2
    exit 1
fi

mkdir -p "$ZIEL"
echo "Lese 16 MB Flash von $PORT nach $DATEI (dauert ca. 8 Minuten) ..."

# read_flash liest nur, schreibt nichts auf das Gerät.
"$ESPTOOL" --port "$PORT" --baud 460800 read_flash 0x0 0x1000000 "$DATEI"

sha256sum "$DATEI" | tee "$DATEI.sha256"
echo "Fertig. Prüfsumme steht in $DATEI.sha256"
