#include "ui_pages.h"

#include "fonts/ui_fonts.h"
#include "ui_icons.h"
#include "ui_theme.h"

namespace ui_pages {

using namespace ui_theme;

// Reihenfolge der Seiten = Reihenfolge der Kacheln auf der Startseite
// (5 Spalten × 3 Zeilen, zeilenweise von links oben).
//
//   Zeile 1  Informieren:  News · Wetter · Warnungen · Termine · Signal
//   Zeile 2  Kontakt/Haus: Anrufe · Uhren · Timer · Kamera · Drucker
//   Zeile 3  Hören/Gerät:  Multimedia · Smart Home · Wecker · Gerät
//
// Seit 0.9.0 haben Radio, Podcast und WLAN keine eigene Kachel mehr: Radio
// und Podcast liegen unter „Multimedia“, WLAN unter „Gerät“ (Knopf neben der
// Überschrift). Sie stehen am Ende der Liste, ohne Symbol (= keine Kachel),
// und nennen ihre übergeordnete Seite – dorthin führt der Knopf unten links.
//
// Die Startseite selbst steht vorn und hat keine Kachel (icon = nullptr).
// Wer hier etwas einfügt, muss nichts weiter tun: `homeIndex()` sucht die
// Startseite selbst, und die Startseite legt für jeden Eintrag mit Symbol
// eine Kachel an. Mehr als 15 Kacheln passen aber nicht ins Raster.
const Page kPages[] = {
    {"Start",      nullptr,               kAccent,  home::create,       home::activate,       home::work},
    {"News",       ui_icons::kNews,       kAccent,  news::create,       news::activate,       news::work},
    {"Wetter",     ui_icons::kWeather,    0xffd60a, weather::create,    weather::activate,    weather::work},
    {"Warnungen",  ui_icons::kWarning,    0xff9f0a, warnings::create,   warnings::activate,   warnings::work},
    {"Termine",    ui_icons::kCalendar,   kAccent,  calendar::create,   calendar::activate,   calendar::work},
    {"Signal",     ui_icons::kSignal,     kAccent,  signal::create,     signal::activate,     signal::work},
    {"Anrufe",     ui_icons::kCalls,      kCall,    calls::create,      calls::activate,      calls::work},
    {"Uhren",      ui_icons::kClocks,     kAccent,  clocks::create,     clocks::activate,     clocks::work, clocks::deactivate},
    {"Timer",      ui_icons::kTimer,      kAccent,  timers::create,     timers::activate,     timers::work},
    {"Kamera",     ui_icons::kCamera,     kAccent,  camera::create,     camera::activate,     camera::work, camera::deactivate},
    {"Drucker",    ui_icons::kPrinter,    kAccent,  printers::create,   printers::activate,   printers::work},
    {"Multimedia", ui_icons::kMultimedia, kAccent,  multimedia::create, multimedia::activate, multimedia::work},
    {"Smart Home", ui_icons::kSmartHome,  0xffd60a, smarthome::create,  smarthome::activate,  smarthome::work},
    {"Wecker",     ui_icons::kAlarm,      kAccent,  alarms::create,     alarms::activate,     alarms::work},
    {"Gerät",      ui_icons::kSettings,   kTextMuted, settings::create, settings::activate,   settings::work},
    // --- ohne Kachel, erreichbar über die übergeordnete Seite -------------
    {"Radio",      nullptr, kAccent,    radio::create,    radio::activate,    radio::work,    nullptr, multimedia::create},
    {"Podcast",    nullptr, kAccent,    podcasts::create, podcasts::activate, podcasts::work, nullptr, multimedia::create},
    {"WLAN",       nullptr, kTextMuted, wifi::create,     wifi::activate,     wifi::work,     nullptr, settings::create},
};

const Page *all() { return kPages; }
int count() { return sizeof(kPages) / sizeof(kPages[0]); }

int indexOf(lv_obj_t *(*create)(lv_obj_t *parent)) {
    for (int i = 0; i < count(); ++i) {
        if (kPages[i].create == create) return i;
    }
    return -1;
}

int homeIndex() {
    const int index = indexOf(home::create);
    return index < 0 ? 0 : index;
}

lv_obj_t *makeTitle(lv_obj_t *parent, const char *text) {
    return makeLabel(parent, &ui_font_30, kText, LV_ALIGN_TOP_LEFT, 0, 0, text);
}

lv_obj_t *makeStatus(lv_obj_t *parent) {
    return makeLabel(parent, &ui_font_18, kTextMuted, LV_ALIGN_TOP_RIGHT, 0, 8, "");
}

lv_obj_t *addLine(lv_obj_t *area, const String &text, const lv_font_t *font,
                  uint32_t color, lv_coord_t width) {
    lv_obj_t *label = lv_label_create(area);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_width(label, width);
    // Lange Titel umbrechen statt abschneiden – Schlagzeilen sind selten kurz.
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, text.c_str());
    return label;
}

const char *weatherSymbol(int wmo) {
    // WMO-Wettercodes (open-meteo) zu einem kurzen Wort.
    //
    // Warum Wörter und keine Bildzeichen? Die Schriften dieses Projekts
    // enthalten ASCII und Latin-1 – also Umlaute, aber keine Wettersymbole.
    // LVGLs eingebaute Zeichen kennen zwar WLAN und Pfeile, aber weder Sonne
    // noch Wolke. Echte Symbole bräuchten eine erweiterte Schrift (siehe
    // scripts/build_fonts.sh) oder gezeichnete Grafiken; ein Wort ist sofort
    // lesbar und kostet nichts.
    if (wmo < 0) return "";
    if (wmo == 0) return "klar";
    if (wmo <= 2) return "heiter";
    if (wmo == 3) return "bewoelkt";
    if (wmo <= 48) return "Nebel";
    if (wmo <= 57) return "Niesel";
    if (wmo <= 67) return "Regen";
    if (wmo <= 77) return "Schnee";
    if (wmo <= 82) return "Schauer";
    if (wmo <= 86) return "Schneeschauer";
    return "Gewitter";
}

}  // namespace ui_pages
