#!/bin/bash
# Neue Tablet-Firmware für Updates über das Netz bereitlegen – nur auf dem PC.
#
#   1. firmware/include/firmware_info.h: FIRMWARE_BUILD erhöhen, FIRMWARE_VERSION anpassen
#   2. scripts/release.sh
#   3. in newsroom21 committen und pushen, auf dem Pi „Update anwenden“
#   4. Weboberfläche, Tab Updates: beim Tablet „Installieren“
#
# Signiert wird mit ~/.config/newsroom21-firmware/signing-key.pem
# (newsroom21/scripts/firmware/publish.py).
set -euo pipefail
cd "$(dirname "$0")/.."
NEWSROOM="${NEWSROOM21_REPO:-../newsroom21}"
PIO="${PIO:-$HOME/.platformio/penv/bin/pio}"
info="firmware/include/firmware_info.h"
env="waveshare_s3_touch_lcd_7"

build="$(sed -n 's/^#define FIRMWARE_BUILD \([0-9]\{10\}\)LL$/\1/p' "$info")"
label="$(sed -n 's/^#define FIRMWARE_VERSION "\([A-Za-z0-9._-]*\)"$/\1/p' "$info")"
model="$(sed -n 's/^#define FIRMWARE_MODEL "\([a-z0-9-]*\)"$/\1/p' "$info")"
[[ -n "$build" && -n "$label" && -n "$model" ]] || { echo "firmware_info.h nicht lesbar" >&2; exit 1; }

scripts/make_ota_pubkey.sh >/dev/null   # eingebauter Schlüssel passt zum privaten
(cd firmware && "$PIO" run -e "$env")
python3 "$NEWSROOM/scripts/firmware/publish.py" --model "$model" --build "$build" \
  --label "$label" --bin "firmware/.pio/build/$env/firmware.bin"
