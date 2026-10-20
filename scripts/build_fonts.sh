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

# ---------------------------------------------------------------------------
# Symbolschrift für die Kacheln der Startseite (ui_icons_32)
#
# Quelle: Lucide (https://lucide.dev), Lizenz ISC – frei nutzbar, auch in
# Firmware. Version und Prüfsumme sind fest: Ein verändertes oder anderes
# Paket bricht den Lauf ab, statt unbemerkt fremde Daten einzubauen.
# Neue Version: Nummer ändern, Paket laden, sha256sum prüfen, hier eintragen.
LUCIDE_VERSION="1.48.0"
LUCIDE_SHA256="3c2ecda3d25f6a9692d83f8036d9a526f7da584a51af74cd16eda4498c5c33d8"
curl -sfL -o "$ARBEIT/lucide.tgz" \
  "https://registry.npmjs.org/lucide-static/-/lucide-static-$LUCIDE_VERSION.tgz"
echo "$LUCIDE_SHA256  $ARBEIT/lucide.tgz" | sha256sum -c --quiet -
tar -xzf "$ARBEIT/lucide.tgz" -C "$ARBEIT" package/font/lucide.ttf

# Nur die Symbole, die wirklich gebraucht werden (je ~0,5 KB). Die Nummern
# stehen in package/font/codepoints.json des Pakets; die Zuordnung zu den
# Seiten steht in firmware/src/ui_icons.h – beides zusammen ändern.
#   e348 newspaper  e216 cloud-sun  e193 triangle-alert  e063 calendar
#   e116 message-circle  e133 phone  e087 clock  e1e0 timer  e064 camera
#   e141 printer  e142 radio  e1fa podcast  e03a alarm-clock  e1ae wifi
#   e154 settings  e122 music (Multimedia, ab 0.9.0)  e5f0 house-plug (Smart Home)
# Wetter (Seite „Wetter“, ab 0.8.0) – auch in 22 px für die Stundenleiste:
#   e178 sun  e216 cloud-sun  e088 cloud  e214 cloud-fog  e08a cloud-drizzle
#   e08e cloud-rain  e090 cloud-snow  e08c cloud-lightning
SYMBOLE="0xe348,0xe216,0xe193,0xe063,0xe116,0xe133,0xe087,0xe1e0,0xe064,0xe141,0xe142,0xe1fa,0xe03a,0xe1ae,0xe154,0xe122,0xe5f0"
WETTER="0xe178,0xe216,0xe088,0xe214,0xe08a,0xe08e,0xe090,0xe08c"
npx --yes lv_font_conv@1.5.2 \
  --font "$ARBEIT/package/font/lucide.ttf" -r "$SYMBOLE,$WETTER" \
  --size 32 --bpp 4 --format lvgl --no-compress \
  --lv-include lvgl.h -o "$ZIEL/ui_icons_32.c"
echo "ui_icons_32.c erzeugt"
npx --yes lv_font_conv@1.5.2 \
  --font "$ARBEIT/package/font/lucide.ttf" -r "$WETTER" \
  --size 22 --bpp 4 --format lvgl --no-compress \
  --lv-include lvgl.h -o "$ZIEL/ui_icons_22.c"
echo "ui_icons_22.c erzeugt"
