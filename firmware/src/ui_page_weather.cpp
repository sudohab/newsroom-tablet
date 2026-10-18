#include <cmath>
#include <cstring>

#include "fonts/ui_fonts.h"
#include "lvgl_port/lvgl_v8_port.h"
#include "tablet_data.h"
#include "ui_icons.h"
#include "ui_pages.h"
#include "ui_theme.h"

// Seite „Wetter“ – zwei Ansichten auf derselben Seite.
//
// Übersicht (beim Öffnen):
//
//     ┌──────────────┬──────────────────────────────────────────────┐
//     │ ☁ 11°        │ 07  08  09  10  11  12 …   (nächste 12 Std.)  │
//     │ Bewölkt      │ ☁   ☁   🌧  …                                 │
//     │ gefühlt 9° … │ 10° 11° 12° …                                 │
//     │              │ 10% 20% 40% …                                 │
//     ├──────────────┴──────────────────────────────────────────────┤
//     │ [Heute ›] [Di] [Mi] [Do] [Fr] [Sa] [So]   (7 Tage)           │
//     └──────────────────────────────────────────────────────────────┘
//
// Verlauf (Tipp auf „Heute ›“): links Details (Spanne, gefühlt, Wind, Böen,
// Sonne), rechts die Temperaturkurve der nächsten 24 Stunden mit Höchst- und
// Tiefstwert und darunter Balken für die Regenwahrscheinlichkeit.
// „‹ 7 Tage“ führt zurück zur Übersicht, beim erneuten Öffnen der Seite steht
// immer die Übersicht.
//
// Gezeichnet wird flach (keine Verläufe, keine Durchsichtigkeit) – der
// ESP32-S3 zeichnet jedes Pixel selbst, siehe ui_theme.h. Die Kurve ist eine
// lv_line mit 24 Punkten, die Balken sind einfache Flächen.

namespace ui_pages {
namespace weather {
namespace {

using namespace ui_theme;

constexpr lv_coord_t kWidth = 752;
constexpr lv_coord_t kHeight = 300;
constexpr uint32_t kRefreshMs = 300000;   // fünf Minuten

constexpr int kHourColumns = 12;          // Übersicht: nächste 12 Stunden
constexpr int kCurvePoints = 24;          // Verlauf: nächste 24 Stunden
constexpr int kDays = 7;
constexpr uint32_t kRainColor = 0x64a8ff;   // Regen-Text (gut lesbar auf Schwarz)
constexpr uint32_t kRainBar = 0x2f6fd6;     // Regen-Balken
constexpr uint32_t kMaxColor = 0xff9f0a;    // Höchstwert
constexpr uint32_t kDayBox = 0x0e0e10;      // Tageskästchen (außer heute)

// --- Maße der Übersicht ------------------------------------------------------
constexpr lv_coord_t kLeftW = 200;
constexpr lv_coord_t kHoursX = 226;
constexpr lv_coord_t kHourW = (kWidth - kHoursX) / kHourColumns;   // 43
constexpr lv_coord_t kBandH = 150;                                  // oberes Band
constexpr lv_coord_t kDaysY = 166;
constexpr lv_coord_t kDayGap = 8;
constexpr lv_coord_t kDayW = (kWidth - (kDays - 1) * kDayGap) / kDays;   // 100
constexpr lv_coord_t kDayH = kHeight - kDaysY - 4;                       // 130

// --- Maße des Verlaufs -------------------------------------------------------
constexpr lv_coord_t kFactsW = 220;
constexpr lv_coord_t kPlotX = 262;
constexpr lv_coord_t kPlotW = kWidth - kPlotX - 8;   // 482
constexpr lv_coord_t kPlotY = 62;                     // oberste Temperaturlinie
constexpr lv_coord_t kPlotH = 116;
constexpr lv_coord_t kRainBottom = 238;
constexpr lv_coord_t kRainMaxH = 40;
constexpr lv_coord_t kAxisY = 244;

lv_obj_t *page = nullptr;
lv_obj_t *viewOverview = nullptr;
lv_obj_t *viewCurve = nullptr;
uint32_t lastFetchMs = 0;
bool needsRefresh = true;

// Übersicht
struct NowBlock {           // Symbol, Temperatur, Wetterlage – in beiden Ansichten
    lv_obj_t *icon = nullptr;
    lv_obj_t *temp = nullptr;
    lv_obj_t *condition = nullptr;
};
NowBlock nowOverview;
NowBlock nowCurve;
lv_obj_t *labelDetail = nullptr;
struct HourColumn { lv_obj_t *hour, *icon, *temp, *rain; };
HourColumn hourColumns[kHourColumns];
struct DayBox { lv_obj_t *box, *name, *icon, *temps, *rain; };
DayBox dayBoxes[kDays];

// Verlauf
lv_obj_t *factValues[5] = {};
lv_obj_t *curveLine = nullptr;
lv_point_t curvePoints[kCurvePoints];   // muss bestehen bleiben, solange die Linie lebt
lv_obj_t *maxDot = nullptr;
lv_obj_t *maxLabel = nullptr;
lv_obj_t *minDot = nullptr;
lv_obj_t *minLabel = nullptr;
lv_obj_t *rainBars[kCurvePoints] = {};
lv_obj_t *axisLabels[kCurvePoints / 3] = {};
lv_obj_t *curveEmpty = nullptr;

// --- kleine Helfer -----------------------------------------------------------

void setText(lv_obj_t *label, const char *text) {
    if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

// Beschriftung mit fester Größe (sonst wächst sie mit dem Text, siehe
// Merksatz im BAUPLAN) und wählbarer Ausrichtung.
lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, lv_coord_t x, lv_coord_t y,
                lv_coord_t w, lv_coord_t h, lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(l, align, 0);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_size(l, w, h);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_label_set_text(l, "");
    return l;
}

// Eine einfache Fläche (Balken, Punkt, Kästchen) – nicht anklickbar, nicht scrollbar.
lv_obj_t *box(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
              uint32_t color, lv_coord_t radius) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t *view(lv_obj_t *parent) {
    lv_obj_t *v = makeSection(parent, 0, 0, kWidth, kHeight);
    lv_obj_clear_flag(v, LV_OBJ_FLAG_SCROLLABLE);
    return v;
}

// „11°“, oder „–“ wenn unbekannt. (Nicht „degrees“: so heißt ein Makro in Arduino.h.)
String tempText(int value) {
    return value == tablet_data::kNoTemp ? String("–") : String(value) + "°";
}

void showCurve(bool curve) {
    if (curve) {
        lv_obj_add_flag(viewOverview, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(viewCurve, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(viewCurve, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(viewOverview, LV_OBJ_FLAG_HIDDEN);
    }
}

void onTodayClicked(lv_event_t *) { showCurve(true); }
void onBackClicked(lv_event_t *) { showCurve(false); }

// --- Aufbau ------------------------------------------------------------------

NowBlock buildNow(lv_obj_t *parent, lv_coord_t width) {
    NowBlock now;
    now.icon = label(parent, &ui_icons_32, 0xc7c7cc, 0, 14, 40, 36);
    now.temp = label(parent, &ui_font_56, kText, 46, 0, width - 46, 64);
    now.condition = label(parent, &ui_font_22, kText, 0, 68, width, 28);
    return now;
}

void buildOverview() {
    viewOverview = view(page);

    nowOverview = buildNow(viewOverview, kLeftW);
    labelDetail = label(viewOverview, &ui_font_18, kTextMuted, 0, 100, kLeftW, 46);
    lv_label_set_long_mode(labelDetail, LV_LABEL_LONG_WRAP);

    makeSeparator(viewOverview, kLeftW + 12, 0, 1, kBandH);

    for (int i = 0; i < kHourColumns; ++i) {
        const lv_coord_t x = kHoursX + i * kHourW;
        HourColumn &c = hourColumns[i];
        c.hour = label(viewOverview, &ui_font_18, kTextMuted, x, 4, kHourW, 22, LV_TEXT_ALIGN_CENTER);
        c.icon = label(viewOverview, &ui_icons_22, 0xc7c7cc, x, 30, kHourW, 26, LV_TEXT_ALIGN_CENTER);
        c.temp = label(viewOverview, &ui_font_22, kText, x, 60, kHourW, 28, LV_TEXT_ALIGN_CENTER);
        c.rain = label(viewOverview, &ui_font_18, kRainColor, x, 92, kHourW, 22, LV_TEXT_ALIGN_CENTER);
    }

    makeSeparator(viewOverview, 0, kBandH + 6, kWidth, 1);

    for (int i = 0; i < kDays; ++i) {
        const lv_coord_t x = i * (kDayW + kDayGap);
        DayBox &d = dayBoxes[i];
        if (i == 0) {
            // „Heute“ ist ein Knopf: Ein Tipp öffnet den Verlauf.
            d.box = lv_btn_create(viewOverview);
            lv_obj_set_pos(d.box, x, kDaysY);
            lv_obj_set_size(d.box, kDayW, kDayH);
            lv_obj_set_style_bg_color(d.box, lv_color_hex(kSurface), 0);
            lv_obj_set_style_bg_opa(d.box, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(d.box, lv_color_hex(kSurfacePressed), LV_STATE_PRESSED);
            lv_obj_set_style_radius(d.box, 12, 0);
            lv_obj_set_style_border_width(d.box, 0, 0);
            lv_obj_set_style_shadow_width(d.box, 0, 0);
            lv_obj_set_style_pad_all(d.box, 0, 0);
            lv_obj_clear_flag(d.box, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_event_cb(d.box, onTodayClicked, LV_EVENT_CLICKED, nullptr);
        } else {
            d.box = box(viewOverview, x, kDaysY, kDayW, kDayH, kDayBox, 12);
        }
        d.name = label(d.box, &ui_font_18, i == 0 ? kAccent : kText, 0, 8, kDayW, 22, LV_TEXT_ALIGN_CENTER);
        d.icon = label(d.box, &ui_icons_32, 0xc7c7cc, 0, 34, kDayW, 36, LV_TEXT_ALIGN_CENTER);
        d.temps = label(d.box, &ui_font_22, kText, 0, 74, kDayW, 28, LV_TEXT_ALIGN_CENTER);
        lv_label_set_recolor(d.temps, true);   // Höchstwert weiß, Tiefstwert grau
        d.rain = label(d.box, &ui_font_18, kRainColor, 0, 102, kDayW, 22, LV_TEXT_ALIGN_CENTER);
    }
}

void buildCurve() {
    viewCurve = view(page);

    nowCurve = buildNow(viewCurve, kFactsW);
    static const char *keys[5] = {"Heute", "Gefühlt", "Wind", "Böen", "Sonne"};
    for (int i = 0; i < 5; ++i) {
        const lv_coord_t y = 104 + i * 38;
        makeSeparator(viewCurve, 0, y, kFactsW, 1);
        lv_obj_t *key = label(viewCurve, &ui_font_18, kTextMuted, 0, y + 8, 90, 24);
        lv_label_set_text(key, keys[i]);
        factValues[i] = label(viewCurve, &ui_font_18, kText, 90, y + 8, kFactsW - 90, 24,
                              LV_TEXT_ALIGN_RIGHT);
    }

    makeSeparator(viewCurve, kFactsW + 16, 0, 1, kHeight - 10);

    lv_obj_t *title = label(viewCurve, &ui_font_18, kTextMuted, kPlotX, 12, 260, 24);
    lv_label_set_text(title, "NÄCHSTE 24 STUNDEN");
    makeButton(viewCurve, "\xE2\x80\xB9  7 Tage", onBackClicked, 140, 44, LV_ALIGN_TOP_RIGHT, 0, 0);

    // Hilfslinien oben, Mitte, unten des Temperaturbereichs
    for (int i = 0; i < 3; ++i) {
        box(viewCurve, kPlotX, kPlotY + i * (kPlotH / 2), kPlotW, 1, kSurface, 0);
    }
    for (int i = 0; i < kCurvePoints; ++i) {
        rainBars[i] = box(viewCurve, 0, kRainBottom, 1, 1, kRainBar, 2);
        lv_obj_add_flag(rainBars[i], LV_OBJ_FLAG_HIDDEN);
    }

    curveLine = lv_line_create(viewCurve);
    lv_obj_set_pos(curveLine, 0, 0);
    lv_obj_set_style_line_width(curveLine, 3, 0);
    lv_obj_set_style_line_color(curveLine, lv_color_hex(kText), 0);
    lv_obj_set_style_line_rounded(curveLine, true, 0);

    maxDot = box(viewCurve, 0, 0, 10, 10, kMaxColor, LV_RADIUS_CIRCLE);
    maxLabel = label(viewCurve, &ui_font_22, kMaxColor, 0, 0, 60, 28, LV_TEXT_ALIGN_CENTER);
    minDot = box(viewCurve, 0, 0, 10, 10, kRainColor, LV_RADIUS_CIRCLE);
    minLabel = label(viewCurve, &ui_font_22, kRainColor, 0, 0, 60, 28, LV_TEXT_ALIGN_CENTER);

    for (int i = 0; i < kCurvePoints / 3; ++i) {
        axisLabels[i] = label(viewCurve, &ui_font_18, kTextMuted, 0, kAxisY, 40, 22, LV_TEXT_ALIGN_CENTER);
    }
    curveEmpty = label(viewCurve, &ui_font_22, kTextMuted, kPlotX, 120, kPlotW, 28, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(curveEmpty, "Noch keine Stundenwerte vom Pi");
    lv_obj_add_flag(curveEmpty, LV_OBJ_FLAG_HIDDEN);
}

// --- Daten eintragen ----------------------------------------------------------

void fillNow(const NowBlock &now, const tablet_data::Weather &w) {
    const ui_icons::WeatherIcon icon = ui_icons::weatherIcon(w.wmo);
    setText(now.icon, icon.glyph);
    lv_obj_set_style_text_color(now.icon, lv_color_hex(icon.color), 0);
    setText(now.temp, (String(static_cast<int>(lroundf(w.temp))) + "°").c_str());
    setText(now.condition, w.text.c_str());
}

void fillOverview(const tablet_data::Weather &w) {
    fillNow(nowOverview, w);
    String detail;
    if (w.feels != tablet_data::kNoTemp) detail = "gefühlt " + tempText(w.feels);
    if (w.wind >= 0) {
        if (!detail.isEmpty()) detail += " · ";
        detail += "Wind " + String(w.wind) + " km/h";
        if (!w.windDir.isEmpty()) detail += " " + w.windDir;
    }
    setText(labelDetail, detail.c_str());

    for (int i = 0; i < kHourColumns; ++i) {
        HourColumn &c = hourColumns[i];
        if (i >= static_cast<int>(w.hours.size())) {
            setText(c.hour, ""); setText(c.icon, ""); setText(c.temp, ""); setText(c.rain, "");
            continue;
        }
        const tablet_data::WeatherHour &h = w.hours[i];
        const ui_icons::WeatherIcon icon = ui_icons::weatherIcon(h.wmo);
        setText(c.hour, h.hour.c_str());
        setText(c.icon, icon.glyph);
        lv_obj_set_style_text_color(c.icon, lv_color_hex(icon.color), 0);
        setText(c.temp, tempText(h.temp).c_str());
        // 0 % wird nicht angezeigt – sonst stünde die Zeile voller Nullen.
        setText(c.rain, h.rain > 0 ? (String(h.rain) + "%").c_str() : "");
    }

    for (int i = 0; i < kDays; ++i) {
        DayBox &d = dayBoxes[i];
        if (i >= static_cast<int>(w.days.size())) {
            setText(d.name, i == 0 ? "Heute \xE2\x80\xBA" : "");
            setText(d.icon, ""); setText(d.temps, ""); setText(d.rain, "");
            continue;
        }
        const tablet_data::WeatherDay &day = w.days[i];
        const ui_icons::WeatherIcon icon = ui_icons::weatherIcon(day.wmo);
        // „Heute ›“: der Pfeil zeigt, dass sich hier etwas öffnen lässt.
        setText(d.name, i == 0 ? (day.name + " \xE2\x80\xBA").c_str() : day.name.c_str());
        setText(d.icon, icon.glyph);
        lv_obj_set_style_text_color(d.icon, lv_color_hex(icon.color), 0);
        // Recolor-Syntax von LVGL: "#rrggbb Text#" färbt den Abschnitt.
        const String temps = "#ffffff " + tempText(day.max) + "#  #8e8e93 " + tempText(day.min) + "#";
        setText(d.temps, temps.c_str());
        setText(d.rain, day.rain > 0 ? (String(day.rain) + " %").c_str() : "");
    }
}

void fillCurve(const tablet_data::Weather &w) {
    fillNow(nowCurve, w);
    setText(factValues[0], (tempText(w.tempMin) + " bis " + tempText(w.tempMax)).c_str());
    setText(factValues[1], tempText(w.feels).c_str());
    setText(factValues[2], w.wind >= 0 ? (String(w.wind) + " km/h " + w.windDir).c_str() : "–");
    setText(factValues[3], w.gusts >= 0 ? (String(w.gusts) + " km/h").c_str() : "–");
    setText(factValues[4], w.sunrise.isEmpty() ? "–" : (w.sunrise + " – " + w.sunset).c_str());

    const int available = static_cast<int>(w.hours.size());
    const int n = available < kCurvePoints ? available : kCurvePoints;
    const bool enough = n >= 2;
    lv_obj_t *curveParts[] = {curveLine, maxDot, maxLabel, minDot, minLabel};
    for (lv_obj_t *part : curveParts) {
        if (enough) lv_obj_clear_flag(part, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(part, LV_OBJ_FLAG_HIDDEN);
    }
    if (enough) lv_obj_add_flag(curveEmpty, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(curveEmpty, LV_OBJ_FLAG_HIDDEN);

    // Temperaturbereich bestimmen; mindestens 4 Grad Spanne, damit ein
    // ruhiger Tag nicht wie eine Achterbahn aussieht.
    int lo = 100, hi = -100, iLo = 0, iHi = 0;
    for (int i = 0; i < n; ++i) {
        if (w.hours[i].temp < lo) { lo = w.hours[i].temp; iLo = i; }
        if (w.hours[i].temp > hi) { hi = w.hours[i].temp; iHi = i; }
    }
    int top = hi, bottom = lo;
    if (top - bottom < 4) { const int mid = (top + bottom) / 2; top = mid + 2; bottom = mid - 2; }

    const lv_coord_t step = kPlotW / (kCurvePoints - 1);
    auto px = [&](int i) { return static_cast<lv_coord_t>(kPlotX + i * kPlotW / (kCurvePoints - 1)); };
    auto py = [&](int t) { return static_cast<lv_coord_t>(kPlotY + (top - t) * kPlotH / (top - bottom)); };

    if (enough) {
        for (int i = 0; i < n; ++i) {
            curvePoints[i].x = px(i);
            curvePoints[i].y = py(w.hours[i].temp);
        }
        lv_line_set_points(curveLine, curvePoints, n);
        lv_obj_set_pos(maxDot, px(iHi) - 5, py(hi) - 5);
        lv_obj_set_pos(maxLabel, px(iHi) - 30, py(hi) - 34);
        setText(maxLabel, tempText(hi).c_str());
        lv_obj_set_pos(minDot, px(iLo) - 5, py(lo) - 5);
        lv_obj_set_pos(minLabel, px(iLo) - 30, py(lo) + 8);
        setText(minLabel, tempText(lo).c_str());
    }

    // Regenbalken: Höhe = Wahrscheinlichkeit (100 % = 40 px)
    for (int i = 0; i < kCurvePoints; ++i) {
        const int rain = i < n ? w.hours[i].rain : -1;
        if (rain <= 0) { lv_obj_add_flag(rainBars[i], LV_OBJ_FLAG_HIDDEN); continue; }
        const lv_coord_t scaled = static_cast<lv_coord_t>(rain * kRainMaxH / 100);
        const lv_coord_t h = scaled < 3 ? 3 : scaled;   // auch 5 % soll sichtbar sein
        lv_obj_set_pos(rainBars[i], px(i) - step / 2 + 1, kRainBottom - h);
        lv_obj_set_size(rainBars[i], step - 2, h);
        lv_obj_clear_flag(rainBars[i], LV_OBJ_FLAG_HIDDEN);
    }

    // Uhrzeiten alle drei Stunden unter der Kurve
    for (int k = 0; k < kCurvePoints / 3; ++k) {
        const int i = k * 3;
        lv_obj_set_pos(axisLabels[k], px(i) - 20, kAxisY);
        setText(axisLabels[k], i < n ? w.hours[i].hour.c_str() : "");
    }
}

}  // namespace

lv_obj_t *create(lv_obj_t *parent) {
    page = makeSection(parent, 0, 0, kWidth, kHeight);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    buildOverview();
    buildCurve();
    showCurve(false);
    return page;
}

void activate() {
    // Beim Öffnen immer die Übersicht; geholt wird erst in work().
    showCurve(false);
    needsRefresh = true;
}

void work() {
    const uint32_t now = millis();
    if (!needsRefresh && now - lastFetchMs < kRefreshMs) return;
    needsRefresh = false;
    lastFetchMs = now;

    // Netzabfrage ohne LVGL-Sperre (bis zu acht Sekunden)
    tablet_data::Weather weather;
    const String error = tablet_data::fetchWeather(weather);

    lvgl_port_lock(-1);
    if (!error.isEmpty() || !weather.valid) {
        // Fehler dort zeigen, wo sonst die Wetterlage steht – die alten
        // Werte bleiben stehen, bis neue kommen.
        const char *text = error.isEmpty() ? "Keine Daten" : error.c_str();
        setText(nowOverview.condition, text);
        setText(nowCurve.condition, text);
    } else {
        fillOverview(weather);
        fillCurve(weather);
    }
    lvgl_port_unlock();
}

}  // namespace weather
}  // namespace ui_pages
