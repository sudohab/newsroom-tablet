#include "display_control.h"
#include "fonts/ui_fonts.h"
#include "lvgl_port/lvgl_v8_port.h"
#include "settings_store.h"
#include "tablet_config.h"
#include "ui_pages.h"
#include "ui_theme.h"

// Einstellungen des Geräts: Nachtmodus und Abschaltzeiten.
//
// Alles hier wirkt **sofort** und wird im Gerät gespeichert; es gibt bewusst
// keinen „Speichern"-Knopf.
//
// Eine Helligkeitsregelung gibt es nicht, und das ist Absicht: Die
// Hintergrundbeleuchtung dieses Boards ist ein reiner Schalter (siehe
// display_control.h). Ein Regler, der nur zwischen „an" und „an" wählt, wäre
// irreführend. Geregelt wird stattdessen, **wann** der Bildschirm dunkel ist –
// getrennt für Tag und Nacht.

namespace ui_pages {
namespace settings {
namespace {

using namespace ui_theme;

constexpr lv_coord_t kWidth = 752;
constexpr lv_coord_t kHeight = 300;

lv_obj_t *page = nullptr;
lv_obj_t *switchNight = nullptr;
lv_obj_t *rollerStart = nullptr;
lv_obj_t *rollerEnd = nullptr;
lv_obj_t *rollerDayOff = nullptr;
lv_obj_t *rollerNightOff = nullptr;
lv_obj_t *status = nullptr;

// Abschaltzeiten zur Auswahl – in Minuten, gleiche Reihenfolge wie im Text.
constexpr uint16_t kOffChoices[] = {0, 1, 2, 5, 10, 30, 60};
constexpr const char *kOffText = "nie\n1 Min\n2 Min\n5 Min\n10 Min\n30 Min\n60 Min";

String hourOptions() {
    String text;
    for (int hour = 0; hour < 24; ++hour) {
        text += (hour < 10 ? "0" : "") + String(hour) + " Uhr";
        if (hour < 23) text += "\n";
    }
    return text;
}

// Nachtmodus aus = Beginn und Ende gleich setzen (so merkt es sich das Gerät,
// ohne einen zusätzlichen Schalter zu speichern).
void applyNightHours() {
    const bool on = lv_obj_has_state(switchNight, LV_STATE_CHECKED);
    const uint8_t start = static_cast<uint8_t>(lv_roller_get_selected(rollerStart));
    const uint8_t end = static_cast<uint8_t>(lv_roller_get_selected(rollerEnd));
    display_control::setNightHours(on ? start : end, end);
    lv_label_set_text(status, on ? "Nachtmodus an" : "Nachtmodus aus");
}

void onNightSwitch(lv_event_t *) { applyNightHours(); }
void onHourChanged(lv_event_t *) { applyNightHours(); }

void onDayOffChanged(lv_event_t *event) {
    const uint16_t index = lv_roller_get_selected(lv_event_get_target(event));
    if (index >= sizeof(kOffChoices) / sizeof(kOffChoices[0])) return;
    display_control::setDayOffMinutes(kOffChoices[index]);
    lv_label_set_text(status, "Gespeichert");
}

void onNightOffChanged(lv_event_t *event) {
    const uint16_t index = lv_roller_get_selected(lv_event_get_target(event));
    if (index >= sizeof(kOffChoices) / sizeof(kOffChoices[0])) return;
    display_control::setNightOffMinutes(kOffChoices[index]);
    lv_label_set_text(status, "Gespeichert");
}

void onOffNow(lv_event_t *) {
    // Sofort abschalten. Die nächste Berührung weckt wieder auf und wird
    // dabei nicht als Bedienung gewertet.
    display_control::turnOff();
}

// Eine Auswahlwalze für die Abschaltzeit.
lv_obj_t *makeOffRoller(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                        const char *caption, lv_event_cb_t handler) {
    makeLabel(parent, &ui_font_18, kTextMuted, LV_ALIGN_TOP_LEFT, x, y, caption);
    lv_obj_t *roller = lv_roller_create(parent);
    lv_roller_set_options(roller, kOffText, LV_ROLLER_MODE_NORMAL);
    lv_obj_set_pos(roller, x, y + 24);
    lv_obj_set_width(roller, 150);
    lv_roller_set_visible_row_count(roller, 3);
    lv_obj_set_style_text_font(roller, &ui_font_22, 0);
    lv_obj_set_style_bg_color(roller, lv_color_hex(kSurface), 0);
    lv_obj_set_style_text_color(roller, lv_color_hex(kText), 0);
    lv_obj_set_style_border_width(roller, 0, 0);
    lv_obj_set_style_bg_color(roller, lv_color_hex(kAccent), LV_PART_SELECTED);
    lv_obj_add_event_cb(roller, handler, LV_EVENT_VALUE_CHANGED, nullptr);
    return roller;
}

lv_obj_t *makeHourRoller(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, const char *caption) {
    makeLabel(parent, &ui_font_18, kTextMuted, LV_ALIGN_TOP_LEFT, x, y, caption);
    lv_obj_t *roller = lv_roller_create(parent);
    lv_roller_set_options(roller, hourOptions().c_str(), LV_ROLLER_MODE_NORMAL);
    lv_obj_set_pos(roller, x, y + 24);
    lv_obj_set_width(roller, 130);
    lv_roller_set_visible_row_count(roller, 2);
    lv_obj_set_style_text_font(roller, &ui_font_22, 0);
    lv_obj_set_style_bg_color(roller, lv_color_hex(kSurface), 0);
    lv_obj_set_style_text_color(roller, lv_color_hex(kText), 0);
    lv_obj_set_style_border_width(roller, 0, 0);
    lv_obj_set_style_bg_color(roller, lv_color_hex(kAccent), LV_PART_SELECTED);
    lv_obj_add_event_cb(roller, onHourChanged, LV_EVENT_VALUE_CHANGED, nullptr);
    return roller;
}

}  // namespace

lv_obj_t *create(lv_obj_t *parent) {
    page = makeSection(parent, 0, 0, kWidth, kHeight);
    makeTitle(page, "Einstellungen");
    status = makeStatus(page);

    // --- links: wann geht der Bildschirm aus? -----------------------------
    rollerDayOff = makeOffRoller(page, 0, 46, "BILDSCHIRM AUS AM TAG", onDayOffChanged);
    rollerNightOff = makeOffRoller(page, 190, 46, "IN DER NACHT", onNightOffChanged);

    makeButton(page, "Jetzt aus", onOffNow, 150, 46, LV_ALIGN_TOP_LEFT, 0, 190, kAccent);

    // Feste Breite: Ohne sie waere die Zeile so breit wie ihr Text und liefe
    // ueber die Trennlinie in die rechte Spalte.
    makeWrappedLabel(page, &ui_font_18, kTextMuted, 0, 244, 360,
                     "Eine Berührung weckt den Bildschirm wieder auf.");

    // --- rechts: Nachtmodus ------------------------------------------------
    makeSeparator(page, 400, 46, 1, kHeight - 50);

    makeLabel(page, &ui_font_18, kTextMuted, LV_ALIGN_TOP_LEFT, 430, 46, "NACHTMODUS");
    switchNight = lv_switch_create(page);
    lv_obj_set_size(switchNight, 56, 30);
    lv_obj_set_pos(switchNight, 640, 42);
    lv_obj_set_style_bg_color(switchNight, lv_color_hex(kSurface), 0);
    lv_obj_set_style_bg_color(switchNight, lv_color_hex(kAccent),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(switchNight, onNightSwitch, LV_EVENT_VALUE_CHANGED, nullptr);

    rollerStart = makeHourRoller(page, 430, 90, "VON");
    rollerEnd = makeHourRoller(page, 590, 90, "BIS");

    makeWrappedLabel(page, &ui_font_18, kTextMuted, 430, 196, 300,
                     "In dieser Zeit gilt die Nacht-Abschaltzeit.");
    makeWrappedLabel(page, &ui_font_18, kTextMuted, 430, 226, 300,
                     "Eine Helligkeitsregelung hat dieses Gerät nicht: "
                     "Die Beleuchtung kennt nur an und aus.");
    return page;
}

void activate() {
    // Die gespeicherten Werte in die Bedienelemente übernehmen.
    const uint8_t start = display_control::nightStart();
    const uint8_t end = display_control::nightEnd();

    if (start == end) {
        lv_obj_clear_state(switchNight, LV_STATE_CHECKED);
        // Bei ausgeschaltetem Nachtmodus die Voreinstellung anzeigen, damit
        // beim Einschalten etwas Sinnvolles dasteht.
        lv_roller_set_selected(rollerStart, cfg::kDefaultNightStart, LV_ANIM_OFF);
        lv_roller_set_selected(rollerEnd, cfg::kDefaultNightEnd, LV_ANIM_OFF);
    } else {
        lv_obj_add_state(switchNight, LV_STATE_CHECKED);
        lv_roller_set_selected(rollerStart, start, LV_ANIM_OFF);
        lv_roller_set_selected(rollerEnd, end, LV_ANIM_OFF);
    }

    const uint16_t day = display_control::dayOffMinutes();
    const uint16_t night = display_control::nightOffMinutes();
    for (size_t i = 0; i < sizeof(kOffChoices) / sizeof(kOffChoices[0]); ++i) {
        if (kOffChoices[i] == day) lv_roller_set_selected(rollerDayOff, i, LV_ANIM_OFF);
        if (kOffChoices[i] == night) lv_roller_set_selected(rollerNightOff, i, LV_ANIM_OFF);
    }
    lv_label_set_text(status, "");
}

void work() {
    // Nichts zu holen: Alle Werte stehen im Gerät.
}

}  // namespace settings
}  // namespace ui_pages
