#!/usr/bin/env bash
#
# Erzeugt die Schriften mit deutschen Zeichen neu (firmware/src/fonts/).
#
# Nötig ist das nur, wenn Größen oder Zeichenumfang geändert werden sollen –
# die erzeugten .c-Dateien liegen im Git und werden normal mitgebaut.
#
# Voraussetzung: Node.js (für lv_font_conv, wird per npx geholt).

set -euo pipefail

PROJEKT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ZIEL="$PROJEKT/firmware/src/fonts"
ARBEIT="$(mktemp -d)"
trap 'rm -rf "$ARBEIT"' EXIT

# Montserrat aus dem LVGL-Projekt bzw. vom Hersteller
curl -sL -o "$ARBEIT/Montserrat-Medium.ttf" \
  "https://raw.githubusercontent.com/lvgl/lvgl/v8.4.0/scripts/built_in_font/Montserrat-Medium.ttf"
curl -sL -o "$ARBEIT/Montserrat-SemiBold.ttf" \
  "https://github.com/JulietaUla/Montserrat/raw/master/fonts/ttf/Montserrat-SemiBold.ttf"

# Schnitt:Größe:Name — 0x20-0x7F ist ASCII, 0xA0-0xFF bringt die Umlaute mit
for spec in Medium:18:ui_font_18 Medium:22:ui_font_22 \
            SemiBold:30:ui_font_30 SemiBold:56:ui_font_56; do
  IFS=: read -r variant size name <<< "$spec"
  npx --yes lv_font_conv@1.5.2 \
    --font "$ARBEIT/Montserrat-$variant.ttf" \
    -r 0x20-0x7F -r 0xA0-0xFF \
    --size "$size" --bpp 4 --format lvgl --no-compress \
    --lv-include lvgl.h -o "$ZIEL/$name.c"
  echo "$name.c erzeugt"
done
