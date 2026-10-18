#include "ui.h"

#include <lvgl.h>
#include <time.h>

#include "fonts/ui_fonts.h"
#include "lvgl_port/lvgl_v8_port.h"
#include "settings_store.h"
#include "tablet_config.h"
#include "tablet_state.h"
#include "ui_pages.h"
#include "ui_popup.h"
#include "ui_theme.h"

// Der Rahmen der Oberfläche: Kopfzeile, Inhaltsbereich und – auf allen
// Seiten außer der Startseite – der Knopf „‹ Start“ unten links.
// Die Startseite ist ein Kachelraster (ui_page_home.cpp); eine Menüleiste
// gibt es nicht mehr.
// Die Seiten selbst liegen in ui_page_*.cpp (siehe ui_pages.h).

namespace ui {
namespace {

using namespace ui_theme;

// --- Maße -------------------------------------------------------------------
constexpr lv_coord_t kMargin = 24;
constexpr lv_coord_t kHeaderHeight = 92;
constexpr lv_coord_t kContentTop = kHeaderHeight + 10;
// Der Inhaltsbereich reicht bis fast an den unteren Rand: Das Kachelraster
// der Startseite nutzt die ganze Höhe. Alle anderen Seiten sind 300 hoch
// (wie zu Zeiten der Menüleiste) – darunter liegt der Knopf „‹ Start“.
constexpr lv_coord_t kContentHeight = 480 - kContentTop - 12;
constexpr lv_coord_t kBackWidth = 170;
constexpr lv_coord_t kBackHeight = 50;

lv_obj_t *screen = nullptr;
lv_obj_t *content = nullptr;
lv_obj_t *backButton = nullptr;

// Kopfzeile
lv_obj_t *labelClock = nullptr;
lv_obj_t *labelDate = nullptr;
lv_obj_t *labelAlarm = nullptr;
lv_obj_t *labelWeather = nullptr;
lv_obj_t *labelWeatherDetail = nullptr;

// Seiten-Container, gleiche Reihenfolge wie ui_pages::all().
// Reichlich bemessen, damit eine neue Seite nicht stillschweigend hinten
// herausfällt; `begin()` prüft es zusätzlich.
constexpr int kMaxPages = 16;
lv_obj_t *pageObjects[kMaxPages] = {};
// -1 = es ist noch keine Seite offen.
int activePage = -1;
// Seitenwechsel passieren im LVGL-Rückruf; die Seite darf aber erst aus der
// Hauptschleife heraus Daten holen. Deshalb nur vormerken.
volatile int pendingActivate = -1;

uint32_t lastClockMs = 0;

// --- Kopfzeile ---------------------------------------------------------------

void showPage(int index);

// Ein Tipp auf die Uhrzeit führt ebenfalls zur Startseite – die Uhr ist
// das, was man auf dem Gerät als Erstes sieht und am leichtesten trifft.
void onClockClicked(lv_event_t *) { showPage(ui_pages::homeIndex()); }

void buildHeader() {
    labelClock = makeLabel(screen, &ui_font_56, kText, LV_ALIGN_TOP_LEFT, kMargin, 8, "--:--");
    lv_obj_add_flag(labelClock, LV_OBJ_FLAG_CLICKABLE);
    // Größere Trefferfläche als die Ziffern selbst (Finger statt Mauszeiger).
    lv_obj_set_ext_click_area(labelClock, 12);
    lv_obj_add_event_cb(labelClock, onClockClicked, LV_EVENT_CLICKED, nullptr);
    labelDate = makeLabel(screen, &ui_font_22, kTextMuted, LV_ALIGN_TOP_LEFT, 232, 14,
                          cfg::kDeviceName);
    labelAlarm = makeLabel(screen, &ui_font_22, kText, LV_ALIGN_TOP_LEFT, 232, 46, "Weckruf –");

    // Wetter rechts: große Zahl mit Symbol, darunter Tagesspanne und Regen
    labelWeather = makeLabel(screen, &ui_font_30, kText, LV_ALIGN_TOP_RIGHT, -kMargin, 10);
    labelWeatherDetail = makeLabel(screen, &ui_font_18, kTextMuted,
                                   LV_ALIGN_TOP_RIGHT, -kMargin, 50);

    makeSeparator(screen, kMargin, kHeaderHeight, 800 - 2 * kMargin, 1);
}

void updateClock() {
    struct tm now;
    if (!getLocalTime(&now, 0)) {
        lv_label_set_text(labelClock, "--:--");
        lv_label_set_text(labelDate, "Uhrzeit noch nicht gesetzt");
        return;
    }
    char timeBuf[6];
    strftime(timeBuf, sizeof(timeBuf), "%H:%M", &now);
    lv_label_set_text(labelClock, timeBuf);

    // Wochentag auf Deutsch: Die Sprachunterstützung des ESP kennt nur
    // Englisch, also wird der Name selbst gesetzt.
    static const char *wochentage[] = {"Sonntag", "Montag", "Dienstag", "Mittwoch",
                                       "Donnerstag", "Freitag", "Samstag"};
    char dateBuf[48];
    snprintf(dateBuf, sizeof(dateBuf), "%s, %d.%d.%d",
             wochentage[now.tm_wday % 7], now.tm_mday, now.tm_mon + 1, now.tm_year + 1900);
    lv_label_set_text(labelDate, dateBuf);
}

void updateHeaderState() {
    const tablet_state::Snapshot &state = tablet_state::current();

    if (state.nextAlarm.isEmpty()) {
        lv_label_set_text(labelAlarm, "Kein Weckruf");
    } else {
        const String text = "Weckruf " + state.nextAlarm
                          + (state.alarmSnoozed ? "  (schlummert)" : "");
        lv_label_set_text(labelAlarm, text.c_str());
    }
    lv_obj_set_style_text_color(labelAlarm,
                                lv_color_hex(state.alarmActive ? kAlarm : kText), 0);

    if (state.hasWeather) {
        const String head = String(ui_pages::weatherSymbol(state.weatherWmo)) + "  "
                          + String(state.temperature, 0) + " °C";
        lv_label_set_text(labelWeather, head.c_str());
        String detail = String(state.tempMin) + " bis " + String(state.tempMax) + " °C";
        if (state.rainProbability >= 0) {
            detail += "   Regen " + String(state.rainProbability) + " %";
        }
        lv_label_set_text(labelWeatherDetail, detail.c_str());
    } else if (!state.online) {
        lv_label_set_text(labelWeather, "");
        lv_label_set_text(labelWeatherDetail, state.error.c_str());
    } else {
        lv_label_set_text(labelWeather, "");
        lv_label_set_text(labelWeatherDetail, "");
    }
}

// --- Seitenwechsel -----------------------------------------------------------

void showPage(int index) {
    if (index < 0 || index >= ui_pages::count()) return;
    for (int i = 0; i < ui_pages::count(); ++i) {
        if (pageObjects[i] == nullptr) continue;
        if (i == index) {
            lv_obj_clear_flag(pageObjects[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(pageObjects[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    // „‹ Start“ nur auf Unterseiten – auf der Startseite gibt es kein Zurück.
    if (backButton != nullptr) {
        if (index == ui_pages::homeIndex()) {
            lv_obj_add_flag(backButton, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(backButton, LV_OBJ_FLAG_HIDDEN);
        }
    }
    // Die alte Seite aufräumen lassen, bevor die neue kommt.
    if (activePage >= 0 && activePage != index) {
        const ui_pages::Page *old = &ui_pages::all()[activePage];
        if (old->deactivate != nullptr) old->deactivate();
    }

    activePage = index;
    pendingActivate = index;
}

void onBackClicked(lv_event_t *) { showPage(ui_pages::homeIndex()); }

void buildBackButton() {
    // Unten links, wo vorher die Menüleiste war: Dort liegt der Daumen, und
    // die Seiten (300 hoch) reichen nicht bis hierher.
    backButton = makeButton(screen, "\xE2\x80\xB9  Start", onBackClicked, kBackWidth, kBackHeight,
                            LV_ALIGN_BOTTOM_LEFT, kMargin, -10);
}

// Nach einer Weile ohne Berührung zurück zur Startseite. Die Zeit stellt
// man auf der Seite „Gerät“ ein (0 = nie). Gezählt wird mit LVGLs eigener
// Ruhezeit – dieselbe, mit der auch der Bildschirm abschaltet.
void returnHomeWhenIdle() {
    const uint16_t minutes = settings_store::homeReturnMinutes();
    if (minutes == 0 || activePage < 0 || activePage == ui_pages::homeIndex()) return;
    if (lv_disp_get_inactive_time(nullptr) < static_cast<uint32_t>(minutes) * 60000UL) return;
    // Ein Aufblendfenster (Anruf, Timer, Waschmaschine …) liegt über allem;
    // die Seite darunter zu wechseln stört nicht, es bleibt sichtbar.
    showPage(ui_pages::homeIndex());
}

}  // namespace

void openPage(int index) { showPage(index); }

void begin() {
    lvgl_port_lock(-1);

    screen = lv_obj_create(nullptr);
    applyBackground(screen);

    buildHeader();

    // Inhaltsbereich: Hier liegen alle Seiten übereinander, sichtbar ist eine.
    content = makeSection(screen, kMargin, kContentTop, 800 - 2 * kMargin, kContentHeight);

    // Lieber hier laut sein als eine Seite stillschweigend verlieren.
    const int pages = min(ui_pages::count(), kMaxPages);
    if (ui_pages::count() > kMaxPages) {
        Serial.printf("[ui] %d Seiten, aber nur %d Plaetze - kMaxPages erhoehen!\n",
                      ui_pages::count(), kMaxPages);
    }
    for (int i = 0; i < pages; ++i) {
        pageObjects[i] = ui_pages::all()[i].create(content);
        if (pageObjects[i] != nullptr) lv_obj_add_flag(pageObjects[i], LV_OBJ_FLAG_HIDDEN);
    }

    buildBackButton();
    showPage(ui_pages::homeIndex());
    updateClock();
    updateHeaderState();

    // Zuletzt, damit es über allem liegt.
    ui_popup::begin(screen);

    lv_scr_load(screen);
    lvgl_port_unlock();
}

void tick() {
    const uint32_t now = millis();

    if (now - lastClockMs > 1000) {
        lastClockMs = now;
        lvgl_port_lock(-1);
        updateClock();
        lvgl_port_unlock();
    }

    // Zurück zur Startseite nach der eingestellten Ruhezeit
    lvgl_port_lock(-1);
    returnHomeWhenIdle();
    lvgl_port_unlock();

    if (tablet_state::consumeChanged()) {
        lvgl_port_lock(-1);
        updateHeaderState();
        // Anruf und abgelaufener Timer blenden über jeder Seite auf.
        ui_popup::update();
        lvgl_port_unlock();
    }

    // Ohne Sperre: Das Abstellen eines Timers geht über das Netz.
    ui_popup::work();
    // Stream der Kamera stoppen, wenn die Seite verlassen wurde.
    ui_pages::camera::background();

    // Ein Seitenwechsel wurde im Rückruf nur vorgemerkt – hier darf die Seite
    // nachladen, ohne die Anzeige zu blockieren.
    const int toActivate = pendingActivate;
    if (toActivate >= 0) {
        pendingActivate = -1;
        if (ui_pages::all()[toActivate].activate != nullptr) {
            ui_pages::all()[toActivate].activate();
        }
    }

    // Die offene Seite arbeiten lassen (Daten holen, Knopfdrücke ausführen).
    const ui_pages::Page *page = &ui_pages::all()[activePage];
    if (page->work != nullptr) page->work();
}

}  // namespace ui
