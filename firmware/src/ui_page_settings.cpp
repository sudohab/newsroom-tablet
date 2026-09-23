#include "display_control.h"
#include "fonts/ui_fonts.h"
#include "lvgl_port/lvgl_v8_port.h"
#include "settings_store.h"
#include "tablet_config.h"
#include "ui_pages.h"
#include "ui_theme.h"

// Einstellungen des Geräts: Helligkeit, Nachtmodus, Bildschirm abschalten.
//
// Alles hier wirkt **sofort** und wird im Gerät gespeichert; es gibt bewusst
// keinen „Speichern"-Knopf. Wer die Helligkeit einstellt, will sie sehen.
//
// Zur Erinnerung (siehe display_control.h): Die Hintergrundbeleuchtung kann
// dieses Board nur an oder aus. „Helligkeit" dunkelt deshalb das Bild ab.

namespace ui_pages {
namespace settings {
namespace {

using namespace ui_theme;

constexpr lv_coord_t kWidth = 752;
constexpr lv_coord_t kHeight = 300;

lv_obj_t *page = nullptr;
lv_obj_t *sliderDay = nullptr;
lv_obj_t *sliderNight = nullptr;
lv_obj_t *labelDay = nullptr;
lv_obj_t *labelNight = nullptr;
lv_obj_t *switchNight = nullptr;
lv_obj_t *rollerStart = nullptr;
lv_obj_t *rollerEnd = nullptr;
lv_obj_t *rollerOff = nullptr;
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

void onDayChanged(lv_event_t *event) {
    const int value = lv_slider_get_value(lv_event_get_target(event));
    display_control::setDayLevel(static_cast<uint8_t>(value));
    lv_label_set_text(labelDay, (String(value) + " %").c_str());
}

void onNightChanged(lv_event_t *event) {
    const int value = lv_slider_get_value(lv_event_get_target(event));
    display_control::setNightLevel(static_cast<uint8_t>(value));
    lv_label_set_text(labelNight, (String(value) + " %").c_str());
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

void onOffChanged(lv_event_t *event) {
    const uint16_t index = lv_roller_get_selected(lv_event_get_target(event));
    if (index >= sizeof(kOffChoices) / sizeof(kOffChoices[0])) return;
    display_control::setOffAfterMinutes(kOffChoices[index]);
    lv_label_set_text(status, kOffChoices[index] == 0
                          ? "Bildschirm bleibt an"
                          : "Bildschirm geht aus, Berührung weckt ihn");
}

void onOffNow(lv_event_t *) {
    // Sofort abschalten. Die nächste Berührung weckt wieder auf und wird
    // dabei nicht als Bedienung gewertet.
    display_control::turnOff();
}

lv_obj_t *makeSlider(lv_obj_t *parent, lv_coord_t y, const char *caption,
                     lv_event_cb_t handler, lv_obj_t **valueLabel) {
    makeLabel(parent, &ui_font_18, kTextMuted, LV_ALIGN_TOP_LEFT, 0, y, caption);
    lv_obj_t *slider = lv_slider_create(parent);
    lv_obj_set_size(slider, 300, 14);
    lv_obj_set_pos(slider, 0, y + 28);
    lv_slider_set_range(slider, cfg::kMinBrightness, 100);
    lv_obj_set_style_bg_color(slider, lv_color_hex(kSurface), 0);
    lv_obj_set_style_bg_color(slider, lv_color_hex(kAccent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xffffff), LV_PART_KNOB);
    lv_obj_add_event_cb(slider, handler, LV_EVENT_VALUE_CHANGED, nullptr);
    *valueLabel = makeLabel(parent, &ui_font_22, kText, LV_ALIGN_TOP_LEFT, 320, y + 20, "–");
    return slider;
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

    // --- links: Helligkeit -------------------------------------------------
    sliderDay = makeSlider(page, 46, "HELLIGKEIT AM TAG", onDayChanged, &labelDay);
    sliderNight = makeSlider(page, 124, "HELLIGKEIT NACHTS", onNightChanged, &labelNight);

    makeLabel(page, &ui_font_18, kTextMuted, LV_ALIGN_TOP_LEFT, 0, 202,
              "Die Beleuchtung dieses Geräts kennt nur an und aus –");
    makeLabel(page, &ui_font_18, kTextMuted, LV_ALIGN_TOP_LEFT, 0, 224,
              "„dunkler“ heißt deshalb: das Bild wird abgedunkelt.");

    // --- rechts: Nachtmodus und Abschalten ---------------------------------
    makeSeparator(page, 420, 46, 1, kHeight - 50);

    makeLabel(page, &ui_font_18, kTextMuted, LV_ALIGN_TOP_LEFT, 450, 46, "NACHTMODUS");
    switchNight = lv_switch_create(page);
    lv_obj_set_size(switchNight, 56, 30);
    lv_obj_set_pos(switchNight, 640, 42);
    lv_obj_set_style_bg_color(switchNight, lv_color_hex(kSurface), 0);
    lv_obj_set_style_bg_color(switchNight, lv_color_hex(kAccent),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(switchNight, onNightSwitch, LV_EVENT_VALUE_CHANGED, nullptr);

    rollerStart = makeHourRoller(page, 450, 80, "VON");
    rollerEnd = makeHourRoller(page, 600, 80, "BIS");

    makeLabel(page, &ui_font_18, kTextMuted, LV_ALIGN_TOP_LEFT, 450, 180,
              "BILDSCHIRM AUS NACH");
    rollerOff = lv_roller_create(page);
    lv_roller_set_options(rollerOff, kOffText, LV_ROLLER_MODE_NORMAL);
    lv_obj_set_pos(rollerOff, 450, 204);
    lv_obj_set_width(rollerOff, 130);
    lv_roller_set_visible_row_count(rollerOff, 2);
    lv_obj_set_style_text_font(rollerOff, &ui_font_22, 0);
    lv_obj_set_style_bg_color(rollerOff, lv_color_hex(kSurface), 0);
    lv_obj_set_style_text_color(rollerOff, lv_color_hex(kText), 0);
    lv_obj_set_style_border_width(rollerOff, 0, 0);
    lv_obj_set_style_bg_color(rollerOff, lv_color_hex(kAccent), LV_PART_SELECTED);
    lv_obj_add_event_cb(rollerOff, onOffChanged, LV_EVENT_VALUE_CHANGED, nullptr);

    makeButton(page, "Jetzt aus", onOffNow, 130, 46, LV_ALIGN_TOP_LEFT, 600, 222);
    return page;
}

void activate() {
    // Die gespeicherten Werte in die Bedienelemente übernehmen.
    const uint8_t day = display_control::dayLevel();
    const uint8_t night = display_control::nightLevel();
    const uint8_t start = display_control::nightStart();
    const uint8_t end = display_control::nightEnd();

    lv_slider_set_value(sliderDay, day, LV_ANIM_OFF);
    lv_label_set_text(labelDay, (String(day) + " %").c_str());
    lv_slider_set_value(sliderNight, night, LV_ANIM_OFF);
    lv_label_set_text(labelNight, (String(night) + " %").c_str());

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

    const uint16_t minutes = display_control::offAfterMinutes();
    for (size_t i = 0; i < sizeof(kOffChoices) / sizeof(kOffChoices[0]); ++i) {
        if (kOffChoices[i] == minutes) {
            lv_roller_set_selected(rollerOff, i, LV_ANIM_OFF);
            break;
        }
    }
    lv_label_set_text(status, "");
}

void work() {
    // Nichts zu holen: Alle Werte stehen im Gerät.
}

}  // namespace settings
}  // namespace ui_pages
