#include "ui_pages.h"

#include "fonts/ui_fonts.h"
#include "ui_theme.h"

namespace ui_pages {

using namespace ui_theme;

// Reihenfolge der Seiten in der Menüleiste. Start zuerst, danach die
// Anzeigeseiten, zuletzt das Einstellbare – so liegt das Alltägliche vorn.
const Page kPages[] = {
    {"Start",    home::create,     home::activate,     home::work},
    {"Wetter",   weather::create,  weather::activate,  weather::work},
    {"Termine",  calendar::create, calendar::activate, calendar::work},
    {"News",     news::create,     news::activate,     news::work},
    {"Warnungen", warnings::create, warnings::activate, warnings::work},
    {"Anrufe",   calls::create,    calls::activate,    calls::work},
    {"Radio",    radio::create,    radio::activate,    radio::work},
    {"Podcast",  podcasts::create, podcasts::activate, podcasts::work},
    {"Wecker",   alarms::create,   alarms::activate,   alarms::work},
    {"Timer",    timers::create,   timers::activate,   timers::work},
    {"WLAN",     wifi::create,     wifi::activate,     wifi::work},
    {"Geraet",   settings::create, settings::activate, settings::work},
};

const Page *all() { return kPages; }
int count() { return sizeof(kPages) / sizeof(kPages[0]); }

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
