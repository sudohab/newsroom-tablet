#include <vector>

#include "fonts/ui_fonts.h"
#include "lvgl_port/lvgl_v8_port.h"
#include "tablet_data.h"
#include "tablet_state.h"
#include "ui_pages.h"
#include "ui_theme.h"

// Startseite.
//
//   links oben   nächster Termin (nur einer)
//   links unten  eine Schlagzeile, Wechsel alle zwei Minuten
//   rechts       drei Felder: verpasste Anrufe · laufender Sender · Warnungen
//
// Die Schlagzeilen wechseln **reihum über die Quellen** – der Pi liefert sie
// bereits in dieser Reihenfolge (siehe /api/tablet/news), das Gerät geht die
// Liste nur der Reihe nach durch.

namespace ui_pages {
namespace home {
namespace {

using namespace ui_theme;

constexpr lv_coord_t kWidth = 752;      // Inhaltsbreite (800 - 2 x 24)
constexpr lv_coord_t kLeftWidth = 470;
constexpr lv_coord_t kTileLeft = 500;
constexpr lv_coord_t kTileWidth = kWidth - kTileLeft;
constexpr uint32_t kHeadlineIntervalMs = 120000;   // zwei Minuten
constexpr uint32_t kNewsRefreshMs = 600000;        // Liste alle zehn Minuten neu

lv_obj_t *page = nullptr;
lv_obj_t *labelEventWhen = nullptr;
lv_obj_t *labelEventTitle = nullptr;
lv_obj_t *labelEventPlace = nullptr;
lv_obj_t *labelNewsSource = nullptr;
lv_obj_t *labelNewsTitle = nullptr;
lv_obj_t *labelCalls = nullptr;
lv_obj_t *labelRadio = nullptr;
lv_obj_t *labelWarning = nullptr;

std::vector<tablet_data::Event> events;
std::vector<tablet_data::Headline> headlines;
size_t headlineIndex = 0;
uint32_t lastHeadlineMs = 0;
uint32_t lastNewsFetchMs = 0;
uint32_t lastCalendarFetchMs = 0;
bool needsRefresh = true;

// Ein Feld auf der rechten Seite: Überschrift klein, Inhalt groß.
lv_obj_t *makeTile(lv_obj_t *parent, lv_coord_t y, const char *caption, const char *initial) {
    makeLabel(parent, &ui_font_18, kTextMuted, LV_ALIGN_TOP_LEFT, kTileLeft, y, caption);
    lv_obj_t *value = lv_label_create(parent);
    lv_obj_set_style_text_font(value, &ui_font_22, 0);
    lv_obj_set_style_text_color(value, lv_color_hex(kText), 0);
    lv_obj_set_pos(value, kTileLeft, y + 24);
    lv_obj_set_width(value, kTileWidth);
    lv_label_set_long_mode(value, LV_LABEL_LONG_WRAP);
    lv_label_set_text(value, initial);
    return value;
}

void showHeadline() {
    if (headlines.empty()) {
        lv_label_set_text(labelNewsSource, "");
        lv_label_set_text(labelNewsTitle, "Keine Nachrichten");
        return;
    }
    if (headlineIndex >= headlines.size()) headlineIndex = 0;
    lv_label_set_text(labelNewsSource, headlines[headlineIndex].source.c_str());
    lv_label_set_text(labelNewsTitle, headlines[headlineIndex].title.c_str());
}

void showNextEvent() {
    if (events.empty()) {
        lv_label_set_text(labelEventWhen, "");
        lv_label_set_text(labelEventTitle, "Keine Termine");
        lv_label_set_text(labelEventPlace, "");
        return;
    }
    const tablet_data::Event &event = events.front();
    const String when = event.time.isEmpty() ? event.when : event.when + "  " + event.time;
    lv_label_set_text(labelEventWhen, when.c_str());
    lv_label_set_text(labelEventTitle, event.title.c_str());
    lv_label_set_text(labelEventPlace, event.location.c_str());
}

void showTiles() {
    const tablet_state::Snapshot &state = tablet_state::current();

    if (state.missedCallCount == 0) {
        lv_label_set_text(labelCalls, "keine");
    } else if (state.missedCallCount == 1) {
        lv_label_set_text(labelCalls, "1 Anruf");
    } else {
        lv_label_set_text(labelCalls, (String(state.missedCallCount) + " Anrufe").c_str());
    }
    lv_obj_set_style_text_color(labelCalls,
                                lv_color_hex(state.missedCallCount > 0 ? kAccent : kTextMuted), 0);

    const bool radioPlaying = state.mediaState == "playing" && state.mediaKind == "radio";
    lv_label_set_text(labelRadio, radioPlaying && !state.mediaTitle.isEmpty()
                                      ? state.mediaTitle.c_str()
                                      : "kein Radiosender");
    lv_obj_set_style_text_color(labelRadio,
                                lv_color_hex(radioPlaying ? kText : kTextMuted), 0);

    if (state.warningCount == 0) {
        lv_label_set_text(labelWarning, "alles ruhig");
        lv_obj_set_style_text_color(labelWarning, lv_color_hex(kTextMuted), 0);
    } else {
        lv_label_set_text(labelWarning, state.warningHeadline.isEmpty()
                                            ? (String(state.warningCount) + " Warnungen").c_str()
                                            : state.warningHeadline.c_str());
        lv_obj_set_style_text_color(labelWarning, lv_color_hex(kAlarm), 0);
    }
}

}  // namespace

lv_obj_t *create(lv_obj_t *parent) {
    page = makeSection(parent, 0, 0, kWidth, 300);

    // --- links: nächster Termin ------------------------------------------
    makeLabel(page, &ui_font_18, kTextMuted, LV_ALIGN_TOP_LEFT, 0, 0, "NÄCHSTER TERMIN");
    labelEventWhen = makeLabel(page, &ui_font_18, kAccent, LV_ALIGN_TOP_LEFT, 0, 26, "");
    labelEventTitle = lv_label_create(page);
    lv_obj_set_style_text_font(labelEventTitle, &ui_font_30, 0);
    lv_obj_set_style_text_color(labelEventTitle, lv_color_hex(kText), 0);
    lv_obj_set_pos(labelEventTitle, 0, 50);
    lv_obj_set_width(labelEventTitle, kLeftWidth);
    lv_label_set_long_mode(labelEventTitle, LV_LABEL_LONG_WRAP);
    lv_label_set_text(labelEventTitle, "…");
    labelEventPlace = makeLabel(page, &ui_font_18, kTextMuted, LV_ALIGN_TOP_LEFT, 0, 88, "");

    makeSeparator(page, 0, 124, kLeftWidth, 1);

    // --- links: wechselnde Schlagzeile ------------------------------------
    makeLabel(page, &ui_font_18, kTextMuted, LV_ALIGN_TOP_LEFT, 0, 140, "NACHRICHTEN");
    labelNewsSource = makeLabel(page, &ui_font_18, kAccent, LV_ALIGN_TOP_LEFT, 0, 166, "");
    labelNewsTitle = lv_label_create(page);
    lv_obj_set_style_text_font(labelNewsTitle, &ui_font_22, 0);
    lv_obj_set_style_text_color(labelNewsTitle, lv_color_hex(kText), 0);
    lv_obj_set_pos(labelNewsTitle, 0, 190);
    lv_obj_set_width(labelNewsTitle, kLeftWidth);
    lv_label_set_long_mode(labelNewsTitle, LV_LABEL_LONG_WRAP);
    lv_label_set_text(labelNewsTitle, "…");

    // --- rechts: drei Felder ----------------------------------------------
    // Senkrechte Trennlinie zwischen linker Spalte und den Feldern
    makeSeparator(page, kTileLeft - 26, 0, 1, 280);

    labelCalls = makeTile(page, 0, "VERPASSTE ANRUFE", "…");
    makeSeparator(page, kTileLeft, 86, kTileWidth, 1);
    labelRadio = makeTile(page, 100, "RADIO", "…");
    makeSeparator(page, kTileLeft, 186, kTileWidth, 1);
    labelWarning = makeTile(page, 200, "WARNUNGEN", "…");

    return page;
}

void activate() {
    // Beim Öffnen sofort auffrischen; geholt wird erst in work().
    needsRefresh = true;
    showTiles();
}

void work() {
    const uint32_t now = millis();

    // Termine und Nachrichten selten holen – sie ändern sich langsam.
    const bool fetchNews = needsRefresh || headlines.empty()
                        || now - lastNewsFetchMs > kNewsRefreshMs;
    const bool fetchCalendar = needsRefresh || now - lastCalendarFetchMs > kNewsRefreshMs;
    needsRefresh = false;

    if (fetchCalendar) {
        lastCalendarFetchMs = now;
        tablet_data::Month month;
        tablet_data::fetchCalendar(events, month);
        lvgl_port_lock(-1);
        showNextEvent();
        lvgl_port_unlock();
    }

    if (fetchNews) {
        lastNewsFetchMs = now;
        const String error = tablet_data::fetchNews(headlines);
        if (!error.isEmpty()) headlines.clear();
        if (headlineIndex >= headlines.size()) headlineIndex = 0;
        lvgl_port_lock(-1);
        showHeadline();
        lvgl_port_unlock();
        lastHeadlineMs = now;
    }

    // Alle zwei Minuten die nächste Meldung zeigen.
    if (now - lastHeadlineMs > kHeadlineIntervalMs && !headlines.empty()) {
        lastHeadlineMs = now;
        headlineIndex = (headlineIndex + 1) % headlines.size();
        lvgl_port_lock(-1);
        showHeadline();
        lvgl_port_unlock();
    }

    // Die drei Felder hängen am laufenden Zustand und sind billig.
    lvgl_port_lock(-1);
    showTiles();
    lvgl_port_unlock();
}

}  // namespace home
}  // namespace ui_pages
