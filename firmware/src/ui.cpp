#include "ui.h"

#include <lvgl.h>
#include <time.h>

#include "fonts/ui_fonts.h"
#include "lvgl_port/lvgl_v8_port.h"
#include "tablet_config.h"
#include "tablet_state.h"
#include "ui_pages.h"
#include "ui_popup.h"
#include "ui_theme.h"

// Der Rahmen der Oberfläche: Kopfzeile, Inhaltsbereich, Menüleiste.
// Die Seiten selbst liegen in ui_page_*.cpp (siehe ui_pages.h).

namespace ui {
namespace {

using namespace ui_theme;

// --- Maße -------------------------------------------------------------------
constexpr lv_coord_t kMargin = 24;
constexpr lv_coord_t kHeaderHeight = 92;
constexpr lv_coord_t kNavHeight = 56;
constexpr lv_coord_t kContentTop = kHeaderHeight + 10;
constexpr lv_coord_t kContentHeight = 480 - kContentTop - kNavHeight - 18;

lv_obj_t *screen = nullptr;
lv_obj_t *content = nullptr;
lv_obj_t *navBar = nullptr;

// Kopfzeile
lv_obj_t *labelClock = nullptr;
lv_obj_t *labelDate = nullptr;
lv_obj_t *labelAlarm = nullptr;
lv_obj_t *labelWeather = nullptr;
lv_obj_t *labelWeatherDetail = nullptr;

// Seiten: Container und Menüknöpfe, gleiche Reihenfolge wie ui_pages::all().
// Reichlich bemessen, damit eine neue Seite nicht stillschweigend hinten
// herausfällt; `begin()` prüft es zusätzlich.
constexpr int kMaxPages = 16;
lv_obj_t *pageObjects[kMaxPages] = {};
lv_obj_t *navButtons[kMaxPages] = {};
// -1 = es ist noch keine Seite offen. Daran erkennt `showPage`, dass es die
// erste ist – die soll ohne Laufbewegung in der Mitte stehen.
int activePage = -1;
// Seitenwechsel passieren im LVGL-Rückruf; die Seite darf aber erst aus der
// Hauptschleife heraus Daten holen. Deshalb nur vormerken.
volatile int pendingActivate = -1;

uint32_t lastClockMs = 0;

// --- Kopfzeile ---------------------------------------------------------------

void buildHeader() {
    labelClock = makeLabel(screen, &ui_font_56, kText, LV_ALIGN_TOP_LEFT, kMargin, 8, "--:--");
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

// --- Menüleiste --------------------------------------------------------------

void showPage(int index) {
    if (index < 0 || index >= ui_pages::count()) return;
    for (int i = 0; i < ui_pages::count(); ++i) {
        if (pageObjects[i] == nullptr) continue;
        if (i == index) {
            lv_obj_clear_flag(pageObjects[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(pageObjects[i], LV_OBJ_FLAG_HIDDEN);
        }
        // Der Knopf der offenen Seite ist eingefärbt – so sieht man auch beim
        // Scrollen der Leiste, wo man gerade ist.
        if (navButtons[i] != nullptr) {
            lv_obj_set_style_bg_color(navButtons[i],
                                      lv_color_hex(i == index ? kAccent : kSurface), 0);
        }
    }
    // Die alte Seite aufräumen lassen, bevor die neue kommt.
    if (activePage >= 0 && activePage != index) {
        const ui_pages::Page *old = &ui_pages::all()[activePage];
        if (old->deactivate != nullptr) old->deactivate();
    }

    const bool first = (activePage < 0);
    activePage = index;
    pendingActivate = index;

    // Den Knopf der offenen Seite mittig in die Leiste schieben. Das
    // Zentrieren macht LVGL von selbst, weil die Leiste auf
    // LV_SCROLL_SNAP_CENTER steht.
    if (navButtons[index] != nullptr) {
        lv_obj_scroll_to_view(navButtons[index], first ? LV_ANIM_OFF : LV_ANIM_ON);
    }
}

void onNavClicked(lv_event_t *event) {
    const int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
    showPage(index);
}

void buildNav() {
    // Waagerecht scrollbare Leiste. Sie rastet an den Knöpfen ein, damit nie
    // ein halber Knopf am Rand stehen bleibt.
    navBar = lv_obj_create(screen);
    lv_obj_set_size(navBar, 800, kNavHeight);
    lv_obj_align(navBar, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_opa(navBar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(navBar, 0, 0);
    lv_obj_set_style_pad_all(navBar, 0, 0);
    lv_obj_set_style_pad_left(navBar, kMargin, 0);
    lv_obj_set_style_pad_right(navBar, kMargin, 0);
    lv_obj_set_style_pad_column(navBar, 10, 0);
    lv_obj_set_flex_flow(navBar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(navBar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(navBar, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(navBar, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(navBar, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < min(ui_pages::count(), kMaxPages); ++i) {
        lv_obj_t *btn = lv_btn_create(navBar);
        lv_obj_set_size(btn, LV_SIZE_CONTENT, 44);
        lv_obj_set_style_bg_color(btn, lv_color_hex(kSurface), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(kSurfacePressed), LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_pad_hor(btn, 18, 0);
        lv_obj_add_event_cb(btn, onNavClicked, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<intptr_t>(i)));

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, ui_pages::all()[i].label);
        lv_obj_set_style_text_font(label, &ui_font_22, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(kText), 0);
        lv_obj_center(label);

        navButtons[i] = btn;
    }
}

}  // namespace

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

    buildNav();
    // Die Leiste muss ihre endgültigen Maße kennen, bevor mittig gescrollt
    // werden kann – sonst stehen alle Knöpfe noch übereinander auf Null.
    lv_obj_update_layout(screen);
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

    if (tablet_state::consumeChanged()) {
        lvgl_port_lock(-1);
        updateHeaderState();
        // Anruf und abgelaufener Timer blenden über jeder Seite auf.
        ui_popup::update();
        lvgl_port_unlock();
    }

    // Ohne Sperre: Das Abstellen eines Timers geht über das Netz.
    ui_popup::work();

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
