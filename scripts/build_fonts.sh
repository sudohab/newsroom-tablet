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

# Zeichenumfang. Fehlt ein Zeichen, zeichnet LVGL ein hochkantes Rechteck –
# so erschienen bis 30.09.2026 „…", „–" und die deutschen Anführungszeichen
# (in Nachrichten und in der Oberfläche selbst).
#   0x20-0x7F      ASCII
#   0xA0-0xFF      Latin-1: Umlaute, ß, °, «», ·
#   0x100-0x17F    Latin Extended-A: Namen in Nachrichten (Łódź, Škoda, Erdoğan, Œ)
#   0x2010-0x2027  Striche – —, Anführungszeichen „ " ‚ ' ' ", • …
#   0x2030,0x2039-0x203A  ‰ ‹ ›
#   0x20AC         €
#   0x2122,0x2212  ™ −
VOLL="-r 0x20-0x7F -r 0xA0-0xFF -r 0x100-0x17F -r 0x2010-0x2027 -r 0x2030 -r 0x2039-0x203A -r 0x20AC -r 0x2122 -r 0x2212"
# Die große Schrift (Uhrzeit, Aufblendfenster) nur mit dem Nötigsten – jedes
# Zeichen kostet in 56 px rund 1,5 KB.
KLEIN="-r 0x20-0x7F -r 0xA0-0xFF -r 0x2013 -r 0x2018-0x201E -r 0x2026"

# Schnitt:Größe:Name:Umfang
for spec in Medium:18:ui_font_18:VOLL Medium:22:ui_font_22:VOLL \
            SemiBold:30:ui_font_30:VOLL SemiBold:56:ui_font_56:KLEIN; do
  IFS=: read -r variant size name umfang <<< "$spec"
  # shellcheck disable=SC2086  # die Bereiche sollen als einzelne Wörter ankommen
  npx --yes lv_font_conv@1.5.2 \
    --font "$ARBEIT/Montserrat-$variant.ttf" \
    ${!umfang} \
    --size "$size" --bpp 4 --format lvgl --no-compress \
    --lv-include lvgl.h -o "$ZIEL/$name.c"
  echo "$name.c erzeugt"
done
