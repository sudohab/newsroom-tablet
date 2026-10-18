// Symbole für die Kacheln der Startseite.
//
// Die Symbole stammen aus der freien Symbolschrift Lucide (Lizenz ISC) und
// liegen als eigene LVGL-Schrift `ui_icons_32` vor (fonts/ui_icons_32.c,
// erzeugt von scripts/build_fonts.sh). Ein Symbol ist dort ein Zeichen aus
// dem „privaten“ Unicode-Bereich (U+E000 …); hier steht es als UTF-8-Folge,
// so wie LVGL Texte erwartet.
//
// Ein neues Symbol braucht zwei Schritte: die Nummer in build_fonts.sh
// (Variable SYMBOLE) aufnehmen und die Schrift neu erzeugen, dann hier
// eintragen. Fehlt ein Zeichen in der Schrift, zeichnet LVGL ein Kästchen.
#pragma once

namespace ui_icons {

constexpr const char *kNews = "\xEE\x8D\x88";       // U+E348 newspaper
constexpr const char *kWeather = "\xEE\x88\x96";    // U+E216 cloud-sun
constexpr const char *kWarning = "\xEE\x86\x93";    // U+E193 triangle-alert
constexpr const char *kCalendar = "\xEE\x81\xA3";   // U+E063 calendar
constexpr const char *kSignal = "\xEE\x84\x96";     // U+E116 message-circle
constexpr const char *kCalls = "\xEE\x84\xB3";      // U+E133 phone
constexpr const char *kClocks = "\xEE\x82\x87";     // U+E087 clock
constexpr const char *kTimer = "\xEE\x87\xA0";      // U+E1E0 timer
constexpr const char *kCamera = "\xEE\x81\xA4";     // U+E064 camera
constexpr const char *kPrinter = "\xEE\x85\x81";    // U+E141 printer
constexpr const char *kRadio = "\xEE\x85\x82";      // U+E142 radio
constexpr const char *kPodcast = "\xEE\x87\xBA";    // U+E1FA podcast
constexpr const char *kAlarm = "\xEE\x80\xBA";      // U+E03A alarm-clock
constexpr const char *kWifi = "\xEE\x86\xAE";       // U+E1AE wifi
constexpr const char *kSettings = "\xEE\x85\x94";   // U+E154 settings

}  // namespace ui_icons
