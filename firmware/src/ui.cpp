#include "ui.h"

#include <lvgl.h>
#include <time.h>

#include <vector>

#include "api_client.h"
#include "lvgl_port/lvgl_v8_port.h"
#include "settings_store.h"
#include "tablet_config.h"
#include "tablet_state.h"
#include "wifi_manager.h"

namespace ui {
namespace {

// Farben: dunkler Grund, damit das Gerät nachts auf dem Nachttisch nicht
// blendet, und kräftige Akzente für alles, was Aufmerksamkeit braucht.
constexpr uint32_t kColorBackground = 0x10131a;
constexpr uint32_t kColorText = 0xffffff;
constexpr uint32_t kColorMuted = 0x9aa4b2;
constexpr uint32_t kColorAlarm = 0xd64545;
constexpr uint32_t kColorAccent = 0x2f6fd0;

// --- Seiten -----------------------------------------------------------------
lv_obj_t *screenHome = nullptr;
lv_obj_t *screenWifi = nullptr;

// Startseite
lv_obj_t *labelClock = nullptr;
lv_obj_t *labelDate = nullptr;
lv_obj_t *labelWeather = nullptr;
lv_obj_t *labelAlarm = nullptr;
lv_obj_t *labelMedia = nullptr;
lv_obj_t *labelVolume = nullptr;
lv_obj_t *labelStatus = nullptr;
lv_obj_t *labelMessage = nullptr;
lv_obj_t *bannerCall = nullptr;
lv_obj_t *labelCall = nullptr;
lv_obj_t *btnSnooze = nullptr;
lv_obj_t *btnAlarmOff = nullptr;

// WLAN-Seite
lv_obj_t *listNetworks = nullptr;
lv_obj_t *labelSelected = nullptr;
lv_obj_t *inputPassword = nullptr;
lv_obj_t *keyboard = nullptr;
lv_obj_t *labelWifiHint = nullptr;
String selectedSsid;

// Wünsche aus der Oberfläche, die außerhalb der LVGL-Sperre erledigt werden.
// Warum so? Ein Tastendruck läuft in der LVGL-Aufgabe. Würde dort eine
// HTTPS-Anfrage starten, stünde die Anzeige bis zu acht Sekunden still.
enum class Pending { None, Scan, Connect, Snooze, AlarmOff, VolumeUp, VolumeDown };
volatile Pending pending = Pending::None;
String pendingPassword;

uint32_t lastClockUpdateMs = 0;

// --- Hilfsfunktionen --------------------------------------------------------

void setText(lv_obj_t *label, const String &text) {
    if (label) lv_label_set_text(label, text.c_str());
}

lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                    lv_align_t align, lv_coord_t x, lv_coord_t y,
                    const char *text = "") {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text);
    lv_obj_align(label, align, x, y);
    return label;
}

lv_obj_t *makeButton(lv_obj_t *parent, const char *text, lv_event_cb_t handler,
                     lv_coord_t width, lv_coord_t height,
                     lv_align_t align, lv_coord_t x, lv_coord_t y,
                     uint32_t color = kColorAccent) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, width, height);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_align(btn, align, x, y);
    lv_obj_add_event_cb(btn, handler, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_center(label);
    return btn;
}

// --- Ereignisse der Startseite ---------------------------------------------

void onSnoozeClicked(lv_event_t *) {
    setText(labelMessage, "Schlummern ...");
    pending = Pending::Snooze;
}

void onAlarmOffClicked(lv_event_t *) {
    setText(labelMessage, "Wecker aus ...");
    pending = Pending::AlarmOff;
}

void onVolumeUpClicked(lv_event_t *) { pending = Pending::VolumeUp; }
void onVolumeDownClicked(lv_event_t *) { pending = Pending::VolumeDown; }

void onWifiPageClicked(lv_event_t *) {
    lv_scr_load(screenWifi);
    setText(labelWifiHint, "Suche nach Netzen ...");
    lv_obj_clean(listNetworks);
    pending = Pending::Scan;
}

// --- Ereignisse der WLAN-Seite ---------------------------------------------

void onBackClicked(lv_event_t *) { lv_scr_load(screenHome); }

void onScanClicked(lv_event_t *) {
    setText(labelWifiHint, "Suche nach Netzen ...");
    lv_obj_clean(listNetworks);
    pending = Pending::Scan;
}

void onNetworkClicked(lv_event_t *event) {
    lv_obj_t *btn = lv_event_get_target(event);
    const char *text = lv_list_get_btn_text(listNetworks, btn);
    if (text == nullptr) return;
    selectedSsid = text;
    setText(labelSelected, "Netz: " + selectedSsid);
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
    lv_obj_set_style_bg_color(screenHome, lv_color_hex(kColorBackground), 0);

    // Uhrzeit und Datum, groß genug zum Ablesen aus dem Bett
    labelClock = makeLabel(screenHome, &lv_font_montserrat_48, kColorText,
                           LV_ALIGN_TOP_LEFT, 40, 30, "--:--");
    labelDate = makeLabel(screenHome, &lv_font_montserrat_20, kColorMuted,
                          LV_ALIGN_TOP_LEFT, 40, 95, cfg::kDeviceName);

    // Wetter rechts oben
    labelWeather = makeLabel(screenHome, &lv_font_montserrat_28, kColorText,
                             LV_ALIGN_TOP_RIGHT, -40, 40);

    // Nächster Weckruf
    labelAlarm = makeLabel(screenHome, &lv_font_montserrat_36, kColorText,
                           LV_ALIGN_TOP_LEFT, 40, 150, "Weckzeit: --");

    // Was gerade läuft (Radio/Podcast)
    labelMedia = makeLabel(screenHome, &lv_font_montserrat_20, kColorMuted,
                           LV_ALIGN_TOP_LEFT, 40, 205);

    // Anrufbanner – nur sichtbar, solange es klingelt
    bannerCall = lv_obj_create(screenHome);
    lv_obj_set_size(bannerCall, 720, 70);
    lv_obj_align(bannerCall, LV_ALIGN_TOP_MID, 0, 240);
    lv_obj_set_style_bg_color(bannerCall, lv_color_hex(kColorAccent), 0);
    lv_obj_set_style_border_width(bannerCall, 0, 0);
    lv_obj_clear_flag(bannerCall, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bannerCall, LV_OBJ_FLAG_HIDDEN);
    labelCall = lv_label_create(bannerCall);
    lv_obj_set_style_text_font(labelCall, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(labelCall, lv_color_hex(kColorText), 0);
    lv_obj_center(labelCall);

    // Bedienung: Schlummern und Wecker aus stehen groß und weit auseinander –
    // halb wach trifft man keine kleinen Knöpfe.
    btnSnooze = makeButton(screenHome, "Schlummern", onSnoozeClicked, 260, 90,
                           LV_ALIGN_BOTTOM_LEFT, 40, -120, kColorAccent);
    btnAlarmOff = makeButton(screenHome, "Wecker aus", onAlarmOffClicked, 260, 90,
                             LV_ALIGN_BOTTOM_RIGHT, -40, -120, kColorAlarm);

    // Lautstärke
    makeButton(screenHome, "Leiser", onVolumeDownClicked, 150, 70,
               LV_ALIGN_BOTTOM_LEFT, 40, -30);
    labelVolume = makeLabel(screenHome, &lv_font_montserrat_28, kColorText,
                            LV_ALIGN_BOTTOM_LEFT, 210, -50, "--%");
    makeButton(screenHome, "Lauter", onVolumeUpClicked, 150, 70,
               LV_ALIGN_BOTTOM_LEFT, 310, -30);

    makeButton(screenHome, "WLAN", onWifiPageClicked, 150, 70,
               LV_ALIGN_BOTTOM_RIGHT, -40, -30);

    // Statuszeile: Verbindung und Rückmeldungen zu Aktionen
    labelStatus = makeLabel(screenHome, &lv_font_montserrat_20, kColorMuted,
                            LV_ALIGN_TOP_RIGHT, -40, 150);
    labelMessage = makeLabel(screenHome, &lv_font_montserrat_20, kColorMuted,
                             LV_ALIGN_TOP_RIGHT, -40, 180);
}

void buildWifiScreen() {
    screenWifi = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screenWifi, lv_color_hex(kColorBackground), 0);

    makeLabel(screenWifi, &lv_font_montserrat_28, kColorText,
              LV_ALIGN_TOP_LEFT, 20, 16, "WLAN einrichten");

    listNetworks = lv_list_create(screenWifi);
    lv_obj_set_size(listNetworks, 380, 300);
    lv_obj_align(listNetworks, LV_ALIGN_TOP_LEFT, 20, 60);

    labelSelected = makeLabel(screenWifi, &lv_font_montserrat_20, kColorText,
                              LV_ALIGN_TOP_LEFT, 420, 60, "Netz: -");

    inputPassword = lv_textarea_create(screenWifi);
    lv_obj_set_size(inputPassword, 340, 56);
    lv_obj_align(inputPassword, LV_ALIGN_TOP_LEFT, 420, 100);
    lv_textarea_set_one_line(inputPassword, true);
    // Eingabe als Punkte anzeigen: Das WLAN-Passwort soll nicht offen auf
    // einem Bildschirm im Wohnzimmer stehen.
    lv_textarea_set_password_mode(inputPassword, true);
    lv_textarea_set_max_length(inputPassword, settings_store::kMaxPassword);
    lv_textarea_set_placeholder_text(inputPassword, "WLAN-Passwort");

    labelWifiHint = makeLabel(screenWifi, &lv_font_montserrat_20, kColorMuted,
                              LV_ALIGN_TOP_LEFT, 420, 170);

    makeButton(screenWifi, "Suchen", onScanClicked, 160, 56,
               LV_ALIGN_TOP_LEFT, 420, 220);
    makeButton(screenWifi, "Verbinden", onConnectClicked, 160, 56,
               LV_ALIGN_TOP_LEFT, 600, 220);
    makeButton(screenWifi, "Zurueck", onBackClicked, 160, 56,
               LV_ALIGN_TOP_LEFT, 420, 290);

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
    char dateBuf[40];
    // %A = Wochentag; die Sprache richtet sich nach der Systemeinstellung des
    // ESP (englisch). Deshalb bewusst nur Zahlen, die überall gleich aussehen.
    strftime(dateBuf, sizeof(dateBuf), "%d.%m.%Y", &now);
    lv_label_set_text(labelClock, timeBuf);
    lv_label_set_text(labelDate, dateBuf);
}

// Überträgt den zuletzt geholten Stand in die Anzeige. Läuft unter der
// LVGL-Sperre und darf deshalb nichts Langsames tun.
void applySnapshot() {
    const tablet_state::Snapshot &state = tablet_state::current();

    if (state.nextAlarm.isEmpty()) {
        setText(labelAlarm, "Kein Weckruf gestellt");
    } else {
        setText(labelAlarm, "Weckruf " + state.nextAlarm +
                            (state.alarmSnoozed ? "  (schlummert)" : ""));
    }
    // Der Weckruf sticht nur hervor, wenn er gerade klingelt.
    lv_obj_set_style_text_color(labelAlarm,
                                lv_color_hex(state.alarmActive ? kColorAlarm : kColorText), 0);

    if (state.hasWeather) {
        setText(labelWeather, String(state.temperature, 1) + " C  " + state.weatherText);
    } else {
        setText(labelWeather, "");
    }

    if (state.mediaState == "playing" && !state.mediaTitle.isEmpty()) {
        setText(labelMedia, "Laeuft: " + state.mediaTitle);
    } else {
        setText(labelMedia, "");
    }

    if (state.callText.isEmpty()) {
        lv_obj_add_flag(bannerCall, LV_OBJ_FLAG_HIDDEN);
    } else {
        setText(labelCall, state.callText);
        lv_obj_clear_flag(bannerCall, LV_OBJ_FLAG_HIDDEN);
    }

    setText(labelVolume, state.volume >= 0 ? String(state.volume) + "%" : "--%");

    // Schlummern und Wecker aus sind nur sinnvoll, solange der Wecker läuft.
    // Ausgegraut statt versteckt: Die Knöpfe bleiben dort, wo man sie sucht.
    const bool alarmRunning = state.alarmActive || state.alarmSnoozed;
    for (lv_obj_t *btn : {btnSnooze, btnAlarmOff}) {
        if (alarmRunning) {
            lv_obj_clear_state(btn, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(btn, LV_STATE_DISABLED);
        }
    }

    setText(labelStatus, state.online ? "Verbunden mit newsroom21"
                                      : "Pi: " + state.error);
}

}  // namespace

void begin() {
    lvgl_port_lock(-1);
    buildHomeScreen();
    buildWifiScreen();
    lv_scr_load(screenHome);
    applySnapshot();
    lvgl_port_unlock();
}

void tick() {
    const uint32_t now = millis();

    if (now - lastClockUpdateMs > 1000) {
        lastClockUpdateMs = now;
        lvgl_port_lock(-1);
        updateClock();
        lvgl_port_unlock();
    }

    // Neue Daten vom Pi? Dann die Anzeige nachziehen.
    if (tablet_state::consumeChanged()) {
        lvgl_port_lock(-1);
        applySnapshot();
        lvgl_port_unlock();
    }

    // Angeforderte Arbeit erledigen – bewusst OHNE LVGL-Sperre, damit die
    // Anzeige während einer Netzwerkanfrage bedienbar bleibt.
    const Pending job = pending;
    if (job == Pending::None) return;
    pending = Pending::None;

    String message;
    switch (job) {
        case Pending::Snooze:      message = tablet_state::snooze(); break;
        case Pending::AlarmOff:    message = tablet_state::alarmOff(); break;
        case Pending::VolumeUp:    message = tablet_state::volumeUp(); break;
        case Pending::VolumeDown:  message = tablet_state::volumeDown(); break;

        case Pending::Scan:
            // Der Suchlauf ist asynchron; das Ergebnis kommt in den Rückruf.
            wifi_manager::startScan([](const std::vector<wifi_manager::Network> &networks) {
                lvgl_port_lock(-1);
                lv_obj_clean(listNetworks);
                for (const auto &net : networks) {
                    const char *symbol = net.encrypted ? LV_SYMBOL_WIFI : LV_SYMBOL_WARNING;
                    lv_obj_t *btn = lv_list_add_btn(listNetworks, symbol, net.ssid.c_str());
                    lv_obj_add_event_cb(btn, onNetworkClicked, LV_EVENT_CLICKED, nullptr);
                }
                setText(labelWifiHint, networks.empty()
                            ? "Keine Netze gefunden"
                            : String(networks.size()) + " Netze gefunden");
                lvgl_port_unlock();
            });
            return;

        case Pending::Connect: {
            const bool accepted = wifi_manager::connectWith(selectedSsid, pendingPassword);
            // Passwort im Arbeitsspeicher überschreiben, sobald es übergeben ist.
            for (size_t i = 0; i < pendingPassword.length(); ++i) pendingPassword[i] = '\0';
            pendingPassword = "";
            lvgl_port_lock(-1);
            setText(labelWifiHint, accepted ? "Zugang gespeichert, verbinde ..."
                                            : "Eingabe ungueltig (zu lang?)");
            lvgl_port_unlock();
            return;
        }

        case Pending::None:
            return;
    }

    lvgl_port_lock(-1);
    setText(labelMessage, message.isEmpty() ? "" : "Fehler: " + message);
    lvgl_port_unlock();
}

}  // namespace ui
