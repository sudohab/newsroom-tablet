// Schriften mit deutschen Zeichen.
//
// Die in LVGL eingebauten Montserrat-Schriften enthalten nur ASCII – „ä", „ö",
// „ü" und „ß" fehlen und erscheinen als leere Kästchen. Diese vier Schriften
// sind deshalb selbst erzeugt und decken zusätzlich den kompletten
// Latin-1-Bereich ab (alle Umlaute, „ß", das Grad-Zeichen).
//
// Erzeugt mit (siehe scripts/build_fonts.sh):
//   npx lv_font_conv --font Montserrat-<Schnitt>.ttf \
//       -r 0x20-0x7F -r 0xA0-0xFF --size <px> --bpp 4 --format lvgl \
//       --no-compress -o ui_font_<px>.c
#pragma once

#include <lvgl.h>

LV_FONT_DECLARE(ui_font_18);   // Nebentexte, Quellen, Statuszeile
LV_FONT_DECLARE(ui_font_22);   // Fließtext: Termine, Schlagzeilen, Knöpfe
LV_FONT_DECLARE(ui_font_30);   // Überschriften, Weckzeit, Wetter
LV_FONT_DECLARE(ui_font_56);   // die Uhr
