#include <vector>

#include "fonts/ui_fonts.h"
#include "lvgl_port/lvgl_v8_port.h"
#include "tablet_data.h"
#include "ui_pages.h"
#include "ui_theme.h"

// Die Anzeigeseiten: Wetter, Termine, Nachrichten, Warnungen, Anrufe.
//
// Alle sind gleich gebaut: Beim Öffnen wird einmal geholt, danach nur noch
// nach einer Weile erneut. Geholt wird in `work()` – also aus der
// Hauptschleife ohne LVGL-Sperre –, gezeichnet unter der Sperre.

namespace ui_pages {
namespace {

using namespace ui_theme;

constexpr lv_coord_t kWidth = 752;
constexpr lv_coord_t kHeight = 300;
constexpr uint32_t kRefreshMs = 300000;   // fünf Minuten

// Gerüst einer Listenseite: Überschrift, Statuszeile, scrollbarer Bereich.
struct ListPage {
    lv_obj_t *page = nullptr;
    lv_obj_t *status = nullptr;
    lv_obj_t *area = nullptr;
    uint32_t lastFetchMs = 0;
    bool needsRefresh = true;

    lv_obj_t *build(lv_obj_t *parent, const char *title) {
        page = makeSection(parent, 0, 0, kWidth, kHeight);
        makeTitle(page, title);
        status = makeStatus(page);
        area = makeScrollArea(page, 0, 46, kWidth, kHeight - 46);
        return page;
    }

    // True, wenn jetzt geholt werden soll.
    bool due(uint32_t now) {
        if (!needsRefresh && now - lastFetchMs < kRefreshMs) return false;
        needsRefresh = false;
        lastFetchMs = now;
        return true;
    }

    void setStatus(const String &text) {
        lv_label_set_text(status, text.c_str());
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// Wetter
// ---------------------------------------------------------------------------
namespace weather {
namespace {
lv_obj_t *page = nullptr;
lv_obj_t *labelNow = nullptr;
lv_obj_t *labelCondition = nullptr;
lv_obj_t *labelRange = nullptr;
lv_obj_t *labelRain = nullptr;
lv_obj_t *labelWind = nullptr;
lv_obj_t *labelHumidity = nullptr;
lv_obj_t *status = nullptr;
uint32_t lastFetchMs = 0;
bool needsRefresh = true;

// Ein Wert mit Beschriftung darunter – vier davon nebeneinander.
lv_obj_t *makeValue(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, const char *caption) {
    makeLabel(parent, &ui_font_18, kTextMuted, LV_ALIGN_TOP_LEFT, x, y, caption);
    return makeLabel(parent, &ui_font_30, kText, LV_ALIGN_TOP_LEFT, x, y + 24, "–");
}
}  // namespace

lv_obj_t *create(lv_obj_t *parent) {
    page = makeSection(parent, 0, 0, kWidth, kHeight);
    makeTitle(page, "Wetter Bamberg");
    status = makeStatus(page);

    // Die aktuelle Temperatur groß, alles andere klein darunter.
    labelNow = makeLabel(page, &ui_font_56, kText, LV_ALIGN_TOP_LEFT, 0, 50, "–");
    labelCondition = makeLabel(page, &ui_font_30, kTextMuted, LV_ALIGN_TOP_LEFT, 0, 118, "");

    makeSeparator(page, 0, 170, kWidth, 1);

    labelRange = makeValue(page, 0, 190, "HEUTE");
    labelRain = makeValue(page, 200, 190, "REGEN");
    labelWind = makeValue(page, 400, 190, "WIND");
    labelHumidity = makeValue(page, 570, 190, "FEUCHTE");
    return page;
}

void activate() { needsRefresh = true; }

void work() {
    const uint32_t now = millis();
    if (!needsRefresh && now - lastFetchMs < kRefreshMs) return;
    needsRefresh = false;
    lastFetchMs = now;

    tablet_data::Weather weather;
    const String error = tablet_data::fetchWeather(weather);

    lvgl_port_lock(-1);
    if (!error.isEmpty() || !weather.valid) {
        lv_label_set_text(status, error.isEmpty() ? "Keine Daten" : error.c_str());
    } else {
        lv_label_set_text(status, "");
        lv_label_set_text(labelNow, (String(weather.temp, 1) + " °C").c_str());
        lv_label_set_text(labelCondition, weather.text.c_str());
        lv_label_set_text(labelRange, (String(weather.tempMin) + " bis "
                                       + String(weather.tempMax) + " °C").c_str());
        lv_label_set_text(labelRain, weather.rainProbability >= 0
                              ? (String(weather.rainProbability) + " %").c_str() : "–");
        lv_label_set_text(labelWind, weather.wind >= 0
                              ? (String(weather.wind) + " km/h").c_str() : "–");
        lv_label_set_text(labelHumidity, weather.humidity >= 0
                              ? (String(weather.humidity) + " %").c_str() : "–");
    }
    lvgl_port_unlock();
}
}  // namespace weather

// ---------------------------------------------------------------------------
// Termine: links der Monat, rechts die Liste
// ---------------------------------------------------------------------------
namespace calendar {
namespace {
constexpr lv_coord_t kMonthWidth = 300;

lv_obj_t *page = nullptr;
lv_obj_t *status = nullptr;
lv_obj_t *monthTitle = nullptr;
lv_obj_t *monthGrid = nullptr;
lv_obj_t *area = nullptr;
std::vector<tablet_data::Event> events;
tablet_data::Month month;
uint32_t lastFetchMs = 0;
bool needsRefresh = true;

// Zeichnet die Monatsübersicht: Tage mit Terminen bekommen einen Rahmen,
// der heutige Tag ist ausgefüllt.
void drawMonth() {
    lv_obj_clean(monthGrid);
    if (month.month == 0) return;

    static const char *namen[] = {"", "Januar", "Februar", "März", "April", "Mai", "Juni",
                                  "Juli", "August", "September", "Oktober", "November",
                                  "Dezember"};
    lv_label_set_text(monthTitle, (String(namen[month.month % 13]) + " "
                                   + String(month.year)).c_str());

    // Wie viele Tage hat der Monat, und auf welchen Wochentag fällt der Erste?
    // Beides hier ausrechnen ist billiger, als es zu übertragen.
    static const int tage[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int daysInMonth = tage[month.month % 13];
    const int y = month.year;
    if (month.month == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) daysInMonth = 29;

    // Wochentag des Ersten nach Zeller (0 = Montag)
    int m = month.month, year = y;
    if (m < 3) { m += 12; year -= 1; }
    const int k = year % 100, j = year / 100;
    const int h = (1 + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
    const int firstWeekday = (h + 5) % 7;   // 0 = Montag

    for (int i = 0; i < firstWeekday; ++i) {
        lv_obj_t *filler = lv_obj_create(monthGrid);
        lv_obj_set_size(filler, 36, 30);
        lv_obj_set_style_bg_opa(filler, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(filler, 0, 0);
        lv_obj_clear_flag(filler, LV_OBJ_FLAG_SCROLLABLE);
    }

    for (int day = 1; day <= daysInMonth; ++day) {
        const bool hasEvent = std::find(month.daysWithEvents.begin(),
                                        month.daysWithEvents.end(), day)
                              != month.daysWithEvents.end();
        const bool isToday = (day == month.today);

        lv_obj_t *cell = lv_obj_create(monthGrid);
        lv_obj_set_size(cell, 36, 30);
        lv_obj_set_style_pad_all(cell, 0, 0);
        lv_obj_set_style_radius(cell, 8, 0);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(cell, lv_color_hex(kAccent), 0);
        lv_obj_set_style_bg_opa(cell, isToday ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        // Tage mit Terminen bekommen einen Rahmen – wie gewünscht.
        lv_obj_set_style_border_width(cell, hasEvent && !isToday ? 2 : 0, 0);
        lv_obj_set_style_border_color(cell, lv_color_hex(kAccent), 0);

        lv_obj_t *label = lv_label_create(cell);
        lv_obj_set_style_text_font(label, &ui_font_18, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(isToday ? 0xffffff : kText), 0);
        lv_label_set_text(label, String(day).c_str());
        lv_obj_center(label);
    }
}

void drawEvents() {
    lv_obj_clean(area);
    if (events.empty()) {
        addLine(area, "Keine Termine", &ui_font_22, kTextMuted, 380);
        return;
    }
    for (const auto &event : events) {
        const String when = event.time.isEmpty() ? event.when
                                                 : event.when + "  " + event.time
                                                   + (event.end.isEmpty() ? "" : "–" + event.end);
        addLine(area, when, &ui_font_18, kAccent, 380);
        addLine(area, event.title, &ui_font_22, kText, 380);
        if (!event.location.isEmpty()) {
            addLine(area, event.location, &ui_font_18, kTextMuted, 380);
        }
    }
}
}  // namespace

lv_obj_t *create(lv_obj_t *parent) {
    page = makeSection(parent, 0, 0, kWidth, kHeight);
    makeTitle(page, "Termine");
    status = makeStatus(page);

    monthTitle = makeLabel(page, &ui_font_18, kTextMuted, LV_ALIGN_TOP_LEFT, 0, 46, "");
    monthGrid = makeSection(page, 0, 70, kMonthWidth, kHeight - 70);
    lv_obj_set_flex_flow(monthGrid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(monthGrid, 4, 0);
    lv_obj_set_style_pad_column(monthGrid, 4, 0);

    makeSeparator(page, kMonthWidth + 20, 46, 1, kHeight - 50);
    area = makeScrollArea(page, kMonthWidth + 44, 46, kWidth - kMonthWidth - 44, kHeight - 46);
    return page;
}

void activate() { needsRefresh = true; }

void work() {
    const uint32_t now = millis();
    if (!needsRefresh && now - lastFetchMs < kRefreshMs) return;
    needsRefresh = false;
    lastFetchMs = now;

    const String error = tablet_data::fetchCalendar(events, month);
    lvgl_port_lock(-1);
    lv_label_set_text(status, error.isEmpty() ? "" : error.c_str());
    drawMonth();
    drawEvents();
    lvgl_port_unlock();
}
}  // namespace calendar

// ---------------------------------------------------------------------------
// Nachrichten
// ---------------------------------------------------------------------------
namespace news {
namespace {
ListPage view;
std::vector<tablet_data::Headline> headlines;
}  // namespace

lv_obj_t *create(lv_obj_t *parent) { return view.build(parent, "Nachrichten"); }
void activate() { view.needsRefresh = true; }

void work() {
    if (!view.due(millis())) return;
    const String error = tablet_data::fetchNews(headlines);

    lvgl_port_lock(-1);
    view.setStatus(error.isEmpty() ? String(headlines.size()) + " Meldungen" : error);
    lv_obj_clean(view.area);
    if (headlines.empty()) {
        addLine(view.area, "Keine Nachrichten", &ui_font_22, kTextMuted, 700);
    }
    for (const auto &headline : headlines) {
        addLine(view.area, headline.source, &ui_font_18, kAccent, 700);
        addLine(view.area, headline.title, &ui_font_22, kText, 700);
    }
    lvgl_port_unlock();
}
}  // namespace news

// ---------------------------------------------------------------------------
// NINA-Warnungen
// ---------------------------------------------------------------------------
namespace warnings {
namespace {
ListPage view;
std::vector<tablet_data::Warning> list;
}  // namespace

lv_obj_t *create(lv_obj_t *parent) { return view.build(parent, "Warnungen"); }
void activate() { view.needsRefresh = true; }

void work() {
    if (!view.due(millis())) return;
    const String error = tablet_data::fetchWarnings(list);

    lvgl_port_lock(-1);
    view.setStatus(error.isEmpty() ? String(list.size()) + " aktiv" : error);
    lv_obj_clean(view.area);
    if (list.empty()) {
        addLine(view.area, "Alles ruhig – keine Warnungen", &ui_font_22, kTextMuted, 700);
    }
    for (const auto &warning : list) {
        // Ereignis und Schwere in einer Zeile, darunter der Text.
        String head = warning.event;
        if (!warning.severity.isEmpty()) head += "   " + warning.severity;
        addLine(view.area, head, &ui_font_18, kAlarm, 700);
        addLine(view.area, warning.headline, &ui_font_22, kText, 700);
    }
    lvgl_port_unlock();
}
}  // namespace warnings

// ---------------------------------------------------------------------------
// Verpasste Anrufe
// ---------------------------------------------------------------------------
namespace calls {
namespace {
ListPage view;
std::vector<tablet_data::Call> list;

// "2026-09-22T19:05:00" -> "22.09. 19:05"
String prettyTime(const String &iso) {
    if (iso.length() < 16) return iso;
    return iso.substring(8, 10) + "." + iso.substring(5, 7) + ".  "
         + iso.substring(11, 16);
}
}  // namespace

lv_obj_t *create(lv_obj_t *parent) { return view.build(parent, "Verpasste Anrufe"); }
void activate() { view.needsRefresh = true; }

void work() {
    if (!view.due(millis())) return;
    const String error = tablet_data::fetchCalls(list);

    lvgl_port_lock(-1);
    view.setStatus(error.isEmpty() ? String(list.size()) + " in 24 h" : error);
    lv_obj_clean(view.area);
    if (list.empty()) {
        addLine(view.area, "Keine verpassten Anrufe", &ui_font_22, kTextMuted, 700);
    }
    for (const auto &call : list) {
        addLine(view.area, prettyTime(call.time), &ui_font_18, kAccent, 700);
        addLine(view.area, call.name.isEmpty() ? call.number : call.name,
                &ui_font_22, kText, 700);
        if (!call.name.isEmpty() && !call.number.isEmpty()) {
            addLine(view.area, call.number, &ui_font_18, kTextMuted, 700);
        }
    }
    // Hinweis auf die noch fehlende Anrufbeantworter-Anzeige (eigene Etappe):
    // Sie braucht einen neuen Zugriff auf die Fritz!Box.
    addLine(view.area, "Anrufbeantworter: folgt", &ui_font_18, kTextMuted, 700);
    lvgl_port_unlock();
}
}  // namespace calls

}  // namespace ui_pages
