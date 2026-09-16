#include "ui.h"

#include <lvgl.h>
#include <time.h>

#include <vector>

#include "api_client.h"
#include "lvgl_port/lvgl_v8_port.h"
#include "settings_store.h"
#include "tablet_config.h"
#include "wifi_manager.h"

namespace ui {
namespace {

// --- Seiten -----------------------------------------------------------------
lv_obj_t *screenHome = nullptr;
lv_obj_t *screenWifi = nullptr;

// Startseite
lv_obj_t *labelClock = nullptr;
lv_obj_t *labelDate = nullptr;
lv_obj_t *labelStatus = nullptr;
lv_obj_t *labelResult = nullptr;

// WLAN-Seite
lv_obj_t *listNetworks = nullptr;
lv_obj_t *labelSelected = nullptr;
lv_obj_t *inputPassword = nullptr;
lv_obj_t *keyboard = nullptr;
lv_obj_t *labelWifiHint = nullptr;
String selectedSsid;

// Wünsche aus der Oberfläche, die außerhalb der LVGL-Sperre erledigt werden.
enum class Pending { None, Ping, Scan, Connect };
volatile Pending pending = Pending::None;
String pendingPassword;

uint32_t lastClockUpdateMs = 0;
uint32_t lastStatusUpdateMs = 0;

// --- Hilfsfunktionen --------------------------------------------------------

// Setzt einen Text sicher: LVGL kopiert den Text, der String darf danach weg.
void setText(lv_obj_t *label, const String &text) {
    if (label) lv_label_set_text(label, text.c_str());
}

// Einheitlich gestaltete Schaltfläche mit Beschriftung.
lv_obj_t *makeButton(lv_obj_t *parent, const char *text, lv_event_cb_t handler,
                     lv_coord_t width = 220, lv_coord_t height = 64) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, width, height);
    lv_obj_add_event_cb(btn, handler, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_center(label);
    return btn;
}

// --- Ereignisse -------------------------------------------------------------

void onPingClicked(lv_event_t *) {
    setText(labelResult, "Teste Verbindung ...");
    pending = Pending::Ping;
}

void onWifiPageClicked(lv_event_t *) {
    lv_scr_load(screenWifi);
    setText(labelWifiHint, "Suche nach Netzen ...");
    lv_obj_clean(listNetworks);
    pending = Pending::Scan;
}

void onBackClicked(lv_event_t *) {
    lv_scr_load(screenHome);
}

void onScanClicked(lv_event_t *) {
    setText(labelWifiHint, "Suche nach Netzen ...");
    lv_obj_clean(listNetworks);
    pending = Pending::Scan;
}

// Ein Netz aus der Liste wurde angetippt.
void onNetworkClicked(lv_event_t *event) {
    lv_obj_t *btn = lv_event_get_target(event);
    const char *text = lv_list_get_btn_text(listNetworks, btn);
    if (text == nullptr) return;
    selectedSsid = text;
    setText(labelSelected, "Netz: " + selectedSsid);
    lv_obj_clear_state(inputPassword, LV_STATE_DISABLED);
    // Tastatur zeigen und mit dem Passwortfeld verbinden
    lv_keyboard_set_textarea(keyboard, inputPassword);
    lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}

void onConnectClicked(lv_event_t *) {
    if (selectedSsid.isEmpty()) {
        setText(labelWifiHint, "Bitte zuerst ein Netz auswaehlen");
        return;
    }
    pendingPassword = lv_textarea_get_text(inputPassword);
    setText(labelWifiHint, "Verbinde mit " + selectedSsid + " ...");
    // Passwortfeld sofort leeren: Es soll nicht sichtbar stehen bleiben.
    lv_textarea_set_text(inputPassword, "");
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    pending = Pending::Connect;
}

// --- Seitenaufbau -----------------------------------------------------------

void buildHomeScreen() {
    screenHome = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screenHome, lv_color_hex(0x10131a), 0);

    labelClock = lv_label_create(screenHome);
    lv_obj_set_style_text_font(labelClock, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(labelClock, lv_color_hex(0xffffff), 0);
    lv_label_set_text(labelClock, "--:--");
    lv_obj_align(labelClock, LV_ALIGN_TOP_MID, 0, 40);

    labelDate = lv_label_create(screenHome);
    lv_obj_set_style_text_font(labelDate, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(labelDate, lv_color_hex(0x9aa4b2), 0);
    lv_label_set_text(labelDate, cfg::kDeviceName);
    lv_obj_align(labelDate, LV_ALIGN_TOP_MID, 0, 110);

    labelStatus = lv_label_create(screenHome);
    lv_obj_set_style_text_font(labelStatus, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(labelStatus, lv_color_hex(0x9aa4b2), 0);
    lv_label_set_text(labelStatus, "WLAN ...");
    lv_obj_align(labelStatus, LV_ALIGN_TOP_MID, 0, 150);

    labelResult = lv_label_create(screenHome);
    lv_obj_set_style_text_font(labelResult, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(labelResult, lv_color_hex(0x9aa4b2), 0);
    lv_label_set_text(labelResult, "");
    lv_obj_align(labelResult, LV_ALIGN_BOTTOM_MID, 0, -30);

    lv_obj_t *btnPing = makeButton(screenHome, "Verbindung testen", onPingClicked, 280, 64);
    lv_obj_align(btnPing, LV_ALIGN_CENTER, -150, 40);

    lv_obj_t *btnWifi = makeButton(screenHome, "WLAN einrichten", onWifiPageClicked, 280, 64);
    lv_obj_align(btnWifi, LV_ALIGN_CENTER, 150, 40);
}

void buildWifiScreen() {
    screenWifi = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screenWifi, lv_color_hex(0x10131a), 0);

    lv_obj_t *title = lv_label_create(screenWifi);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_label_set_text(title, "WLAN einrichten");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 20, 16);

    listNetworks = lv_list_create(screenWifi);
    lv_obj_set_size(listNetworks, 380, 300);
    lv_obj_align(listNetworks, LV_ALIGN_TOP_LEFT, 20, 60);

    labelSelected = lv_label_create(screenWifi);
    lv_obj_set_style_text_font(labelSelected, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(labelSelected, lv_color_hex(0xffffff), 0);
    lv_label_set_text(labelSelected, "Netz: -");
    lv_obj_align(labelSelected, LV_ALIGN_TOP_LEFT, 420, 60);

    inputPassword = lv_textarea_create(screenWifi);
    lv_obj_set_size(inputPassword, 340, 56);
    lv_obj_align(inputPassword, LV_ALIGN_TOP_LEFT, 420, 100);
    lv_textarea_set_one_line(inputPassword, true);
    // Eingabe als Punkte anzeigen: Das WLAN-Passwort soll nicht offen auf
    // einem Bildschirm im Wohnzimmer stehen.
    lv_textarea_set_password_mode(inputPassword, true);
    lv_textarea_set_max_length(inputPassword, settings_store::kMaxPassword);
    lv_textarea_set_placeholder_text(inputPassword, "WLAN-Passwort");

    labelWifiHint = lv_label_create(screenWifi);
    lv_obj_set_style_text_font(labelWifiHint, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(labelWifiHint, lv_color_hex(0x9aa4b2), 0);
    lv_label_set_text(labelWifiHint, "");
    lv_obj_align(labelWifiHint, LV_ALIGN_TOP_LEFT, 420, 170);

    lv_obj_t *btnSuchen = makeButton(screenWifi, "Suchen", onScanClicked, 160, 56);
    lv_obj_align(btnSuchen, LV_ALIGN_TOP_LEFT, 420, 220);
    lv_obj_t *btnVerbinden = makeButton(screenWifi, "Verbinden", onConnectClicked, 160, 56);
    lv_obj_align(btnVerbinden, LV_ALIGN_TOP_LEFT, 600, 220);
    lv_obj_t *btnZurueck = makeButton(screenWifi, "Zurueck", onBackClicked, 160, 56);
    lv_obj_align(btnZurueck, LV_ALIGN_TOP_LEFT, 420, 290);

    keyboard = lv_keyboard_create(screenWifi);
    lv_obj_set_size(keyboard, 800, 230);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}

// --- Aktualisierung ---------------------------------------------------------

void updateClock() {
    struct tm now;
    if (!getLocalTime(&now, 0)) {
        setText(labelClock, "--:--");
        setText(labelDate, "Uhrzeit noch nicht gesetzt");
        return;
    }
    char timeBuf[6];
    strftime(timeBuf, sizeof(timeBuf), "%H:%M", &now);
    char dateBuf[32];
    strftime(dateBuf, sizeof(dateBuf), "%d.%m.%Y", &now);
    lv_label_set_text(labelClock, timeBuf);
    lv_label_set_text(labelDate, dateBuf);
}

}  // namespace

void begin() {
    lvgl_port_lock(-1);
    buildHomeScreen();
    buildWifiScreen();
    lv_scr_load(screenHome);
    lvgl_port_unlock();
}

void tick() {
    const uint32_t now = millis();

    // Uhr und Status regelmäßig nachziehen (unter der LVGL-Sperre).
    if (now - lastClockUpdateMs > 1000) {
        lastClockUpdateMs = now;
        lvgl_port_lock(-1);
        updateClock();
        lvgl_port_unlock();
    }
    if (now - lastStatusUpdateMs > 2000) {
        lastStatusUpdateMs = now;
        const String status = wifi_manager::statusText();
        lvgl_port_lock(-1);
        setText(labelStatus, status);
        lvgl_port_unlock();
    }

    // Angeforderte Arbeit erledigen – bewusst OHNE LVGL-Sperre, damit die
    // Anzeige während einer Netzwerkanfrage bedienbar bleibt.
    const Pending job = pending;
    if (job == Pending::None) return;
    pending = Pending::None;

    switch (job) {
        case Pending::Ping: {
            const api_client::Result result = api_client::ping();
            const String text = result.ok
                ? "Pi erreichbar, Zertifikat geprueft (HTTP " + String(result.status) + ")"
                : "Fehler: " + result.error;
            lvgl_port_lock(-1);
            setText(labelResult, text);
            lvgl_port_unlock();
            break;
        }

        case Pending::Scan: {
            // Der Suchlauf selbst ist asynchron; das Ergebnis kommt in den
            // Rückruf, der aus wifi_manager::loop() heraus gerufen wird.
            wifi_manager::startScan([](const std::vector<wifi_manager::Network> &networks) {
                lvgl_port_lock(-1);
                lv_obj_clean(listNetworks);
                for (const auto &net : networks) {
                    const char *symbol = net.encrypted ? LV_SYMBOL_WIFI : LV_SYMBOL_WARNING;
                    lv_obj_t *btn = lv_list_add_btn(listNetworks, symbol, net.ssid.c_str());
                    lv_obj_add_event_cb(btn, onNetworkClicked, LV_EVENT_CLICKED, nullptr);
                }
                setText(labelWifiHint, networks.empty() ? "Keine Netze gefunden"
                                                        : String(networks.size()) + " Netze gefunden");
                lvgl_port_unlock();
            });
            break;
        }

        case Pending::Connect: {
            const bool accepted = wifi_manager::connectWith(selectedSsid, pendingPassword);
            // Passwort im Arbeitsspeicher überschreiben, sobald es übergeben ist.
            for (size_t i = 0; i < pendingPassword.length(); ++i) pendingPassword[i] = '\0';
            pendingPassword = "";
            lvgl_port_lock(-1);
            setText(labelWifiHint, accepted ? "Zugang gespeichert, verbinde ..."
                                            : "Eingabe ungueltig (zu lang?)");
            lvgl_port_unlock();
            break;
        }

        case Pending::None:
            break;
    }
}

}  // namespace ui
