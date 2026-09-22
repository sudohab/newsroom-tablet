#include "ui.h"

#include <lvgl.h>
#include <time.h>

#include <vector>

#include "fonts/ui_fonts.h"
#include "lvgl_port/lvgl_v8_port.h"
#include "settings_store.h"
#include "tablet_config.h"
#include "tablet_state.h"
#include "ui_theme.h"
#include "wifi_manager.h"

namespace ui {
namespace {

using namespace ui_theme;

// --- Seiten -----------------------------------------------------------------
lv_obj_t *screenHome = nullptr;    // Uhr, Wetter, Termine, Nachrichten
lv_obj_t *screenRadio = nullptr;
lv_obj_t *screenWifi = nullptr;

// Startseite
lv_obj_t *labelClock = nullptr;
lv_obj_t *labelDate = nullptr;
lv_obj_t *labelAlarm = nullptr;
lv_obj_t *labelWeather = nullptr;
lv_obj_t *labelStatus = nullptr;
lv_obj_t *labelVolume = nullptr;
lv_obj_t *labelMessage = nullptr;
lv_obj_t *areaEvents = nullptr;
lv_obj_t *areaNews = nullptr;
lv_obj_t *bannerCall = nullptr;
lv_obj_t *labelCall = nullptr;
lv_obj_t *btnSnooze = nullptr;
lv_obj_t *btnAlarmOff = nullptr;

// Radio-Seite
lv_obj_t *listStations = nullptr;
lv_obj_t *labelRadioStatus = nullptr;
std::vector<tablet_state::Station> stations;
String pendingStationId;
int pendingSleepMinutes = 0;

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
enum class Pending { None, Scan, Connect, Snooze, AlarmOff, VolumeUp, VolumeDown,
                     RadioList, RadioPlay, RadioStop, RadioSleep, RadioSleepCancel };
volatile Pending pending = Pending::None;
String pendingPassword;

uint32_t lastClockUpdateMs = 0;

// --- Hilfsfunktionen --------------------------------------------------------

void setText(lv_obj_t *label, const String &text) {
    if (label) lv_label_set_text(label, text.c_str());
}

// Eine Zeile in eine der scrollbaren Spalten setzen.
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

// --- Ereignisse der Startseite ---------------------------------------------

void onSnoozeClicked(lv_event_t *) {
    setText(labelMessage, "Schlummern …");
    pending = Pending::Snooze;
}

void onAlarmOffClicked(lv_event_t *) {
    setText(labelMessage, "Wecker aus …");
    pending = Pending::AlarmOff;
}

void onVolumeUpClicked(lv_event_t *) { pending = Pending::VolumeUp; }
void onVolumeDownClicked(lv_event_t *) { pending = Pending::VolumeDown; }

void onBackClicked(lv_event_t *) { lv_scr_load(screenHome); }

void onRadioPageClicked(lv_event_t *) {
    lv_scr_load(screenRadio);
    setText(labelRadioStatus, "Hole Senderliste …");
    lv_obj_clean(listStations);
    pending = Pending::RadioList;
}

void onWifiPageClicked(lv_event_t *) {
    lv_scr_load(screenWifi);
    setText(labelWifiHint, "Suche nach Netzen …");
    lv_obj_clean(listNetworks);
    pending = Pending::Scan;
}

// --- Ereignisse der Radio-Seite --------------------------------------------

void onStationClicked(lv_event_t *event) {
    // Welcher Listeneintrag? Die Position in der Liste entspricht der Position
    // in `stations` – die Kennung selbst steht nicht in der Anzeige.
    lv_obj_t *btn = lv_event_get_target(event);
    const uint32_t index = lv_obj_get_index(btn);
    if (index >= stations.size()) return;
    setText(labelRadioStatus, "Starte " + stations[index].name + " …");
    pendingStationId = stations[index].id;
    pending = Pending::RadioPlay;
}

void onRadioStopClicked(lv_event_t *) {
    setText(labelRadioStatus, "Halte an …");
    pending = Pending::RadioStop;
}

void onSleep30Clicked(lv_event_t *) {
    pendingSleepMinutes = 30;
    setText(labelRadioStatus, "Einschlaf-Timer 30 Minuten …");
    pending = Pending::RadioSleep;
}

void onSleep60Clicked(lv_event_t *) {
    pendingSleepMinutes = 60;
    setText(labelRadioStatus, "Einschlaf-Timer 60 Minuten …");
    pending = Pending::RadioSleep;
}

void onSleepCancelClicked(lv_event_t *) {
    setText(labelRadioStatus, "Timer aus …");
    pending = Pending::RadioSleepCancel;
}

// --- Ereignisse der WLAN-Seite ---------------------------------------------

void onScanClicked(lv_event_t *) {
    setText(labelWifiHint, "Suche nach Netzen …");
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
        setText(labelWifiHint, "Bitte zuerst ein Netz auswählen");
        return;
    }
    pendingPassword = lv_textarea_get_text(inputPassword);
    setText(labelWifiHint, "Verbinde mit " + selectedSsid + " …");
    // Passwortfeld sofort leeren: Es soll nicht sichtbar stehen bleiben.
    lv_textarea_set_text(inputPassword, "");
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    pending = Pending::Connect;
}

// --- Seitenaufbau -----------------------------------------------------------

void buildHomeScreen() {
    screenHome = lv_obj_create(nullptr);
    applyBackground(screenHome);

    // --- Kopfzeile: Uhr, Datum, Weckruf, Wetter ---------------------------
    lv_obj_t *header = makeCard(screenHome, 20, 16, 760, 104);
    labelClock = makeLabel(header, &ui_font_56, kText, LV_ALIGN_LEFT_MID, 8, -2, "--:--");
    labelDate = makeLabel(header, &ui_font_22, kTextMuted, LV_ALIGN_LEFT_MID, 220, -18,
                          cfg::kDeviceName);
    labelAlarm = makeLabel(header, &ui_font_22, kText, LV_ALIGN_LEFT_MID, 220, 16,
                           "Weckruf –");
    labelWeather = makeLabel(header, &ui_font_30, kText, LV_ALIGN_RIGHT_MID, -8, -16);
    labelStatus = makeLabel(header, &ui_font_18, kTextMuted, LV_ALIGN_RIGHT_MID, -8, 20);

    // --- Spalten: Termine und Nachrichten, beide zum Scrollen -------------
    lv_obj_t *cardEvents = makeCard(screenHome, 20, 132, 370, 252);
    makeLabel(cardEvents, &ui_font_18, kTextMuted, LV_ALIGN_TOP_LEFT, 0, 0, "TERMINE");
    areaEvents = makeScrollArea(cardEvents, 0, 32, 338, 186);

    lv_obj_t *cardNews = makeCard(screenHome, 410, 132, 370, 252);
    makeLabel(cardNews, &ui_font_18, kTextMuted, LV_ALIGN_TOP_LEFT, 0, 0, "NACHRICHTEN");
    areaNews = makeScrollArea(cardNews, 0, 32, 338, 186);

    // --- Bedienleiste -----------------------------------------------------
    // Die Knöpfe liegen in einer Leiste, die ihre Abstände selbst berechnet.
    // Von Hand gesetzte Koordinaten waren die Ursache dafür, dass sich Knöpfe
    // überlappten, sobald eine Beschriftung länger wurde.
    lv_obj_t *bar = makeButtonBar(screenHome, 20, 400, 760, 60);
    btnSnooze = addBarButton(bar, "Schlummern", onSnoozeClicked, 150, 54, kAccent);
    btnAlarmOff = addBarButton(bar, "Aus", onAlarmOffClicked, 80, 54, kAlarm);
    addBarButton(bar, "–", onVolumeDownClicked, 54, 54);
    // Die Lautstärke steht zwischen den beiden Tasten und ist mittig
    // ausgerichtet, damit "5%" und "100%" gleich sitzen.
    labelVolume = lv_label_create(bar);
    lv_obj_set_style_text_font(labelVolume, &ui_font_22, 0);
    lv_obj_set_style_text_color(labelVolume, lv_color_hex(kText), 0);
    lv_obj_set_width(labelVolume, 64);
    lv_obj_set_style_text_align(labelVolume, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(labelVolume, "--%");
    addBarButton(bar, "+", onVolumeUpClicked, 54, 54);
    addBarSpacer(bar);
    addBarButton(bar, "Radio", onRadioPageClicked, 110, 54);
    addBarButton(bar, "WLAN", onWifiPageClicked, 110, 54);

    labelMessage = makeLabel(screenHome, &ui_font_18, kTextMuted,
                             LV_ALIGN_TOP_MID, 0, 388);

    // --- Anrufbanner: liegt über allem, sonst unsichtbar -------------------
    bannerCall = makeCard(screenHome, 60, 180, 680, 120);
    lv_obj_set_style_bg_color(bannerCall, lv_color_hex(kCall), 0);
    lv_obj_set_style_bg_opa(bannerCall, LV_OPA_COVER, 0);
    lv_obj_add_flag(bannerCall, LV_OBJ_FLAG_HIDDEN);
    labelCall = lv_label_create(bannerCall);
    lv_obj_set_style_text_font(labelCall, &ui_font_30, 0);
    lv_obj_set_style_text_color(labelCall, lv_color_hex(0xffffff), 0);
    lv_obj_center(labelCall);
}

void buildRadioScreen() {
    screenRadio = lv_obj_create(nullptr);
    applyBackground(screenRadio);

    lv_obj_t *card = makeCard(screenRadio, 20, 16, 470, 448);
    makeLabel(card, &ui_font_30, kText, LV_ALIGN_TOP_LEFT, 0, 0, "Radio");
    // Die Favoritenliste ist scrollbar – es können mehr Sender sein, als auf
    // den Bildschirm passen.
    listStations = lv_list_create(card);
    lv_obj_set_size(listStations, 438, 356);
    lv_obj_align(listStations, LV_ALIGN_TOP_LEFT, 0, 50);
    lv_obj_set_style_bg_opa(listStations, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(listStations, 0, 0);
    lv_obj_set_style_pad_all(listStations, 0, 0);

    labelRadioStatus = makeLabel(screenRadio, &ui_font_18, kTextMuted,
                                 LV_ALIGN_TOP_RIGHT, -20, 30);

    makeButton(screenRadio, "Aus", onRadioStopClicked, 250, 54,
               LV_ALIGN_TOP_RIGHT, -20, 70, kAlarm);
    // Die beiden Timer-Knöpfe in einer eigenen Leiste: So bleibt der Abstand
    // zwischen ihnen richtig, auch wenn die Beschriftung sich ändert.
    lv_obj_t *timerBar = makeButtonBar(screenRadio, 530, 140, 250, 60);
    addBarButton(timerBar, "30 Min", onSleep30Clicked, 120, 54);
    addBarButton(timerBar, "60 Min", onSleep60Clicked, 120, 54);
    makeButton(screenRadio, "Timer aus", onSleepCancelClicked, 250, 54,
               LV_ALIGN_TOP_RIGHT, -20, 210);
    makeButton(screenRadio, "Zurück", onBackClicked, 250, 54,
               LV_ALIGN_BOTTOM_RIGHT, -20, -16);
}

void buildWifiScreen() {
    screenWifi = lv_obj_create(nullptr);
    applyBackground(screenWifi);

    lv_obj_t *card = makeCard(screenWifi, 20, 16, 400, 296);
    makeLabel(card, &ui_font_30, kText, LV_ALIGN_TOP_LEFT, 0, 0, "WLAN");
    listNetworks = lv_list_create(card);
    lv_obj_set_size(listNetworks, 368, 206);
    lv_obj_align(listNetworks, LV_ALIGN_TOP_LEFT, 0, 50);
    lv_obj_set_style_bg_opa(listNetworks, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(listNetworks, 0, 0);
    lv_obj_set_style_pad_all(listNetworks, 0, 0);

    lv_obj_t *side = makeCard(screenWifi, 440, 16, 340, 296);
    labelSelected = makeLabel(side, &ui_font_22, kText, LV_ALIGN_TOP_LEFT, 0, 0, "Netz: –");

    inputPassword = lv_textarea_create(side);
    lv_obj_set_size(inputPassword, 308, 52);
    lv_obj_align(inputPassword, LV_ALIGN_TOP_LEFT, 0, 36);
    lv_obj_set_style_text_font(inputPassword, &ui_font_22, 0);
    lv_textarea_set_one_line(inputPassword, true);
    // Eingabe als Punkte anzeigen: Das WLAN-Passwort soll nicht offen auf
    // einem Bildschirm im Wohnzimmer stehen.
    lv_textarea_set_password_mode(inputPassword, true);
    lv_textarea_set_max_length(inputPassword, settings_store::kMaxPassword);
    lv_textarea_set_placeholder_text(inputPassword, "WLAN-Passwort");

    labelWifiHint = makeLabel(side, &ui_font_18, kTextMuted, LV_ALIGN_TOP_LEFT, 0, 100);

    makeButton(side, "Suchen", onScanClicked, 148, 52, LV_ALIGN_TOP_LEFT, 0, 134);
    makeButton(side, "Verbinden", onConnectClicked, 148, 52,
               LV_ALIGN_TOP_LEFT, 160, 134, kAccent);
    makeButton(side, "Zurück", onBackClicked, 148, 52, LV_ALIGN_TOP_LEFT, 0, 196);

    keyboard = lv_keyboard_create(screenWifi);
    lv_obj_set_size(keyboard, 800, 160);
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

// Überträgt den zuletzt geholten Stand in die Anzeige. Läuft unter der
// LVGL-Sperre und darf deshalb nichts Langsames tun.
void applySnapshot() {
    const tablet_state::Snapshot &state = tablet_state::current();

    // --- Kopfzeile --------------------------------------------------------
    if (state.nextAlarm.isEmpty()) {
        setText(labelAlarm, "Kein Weckruf gestellt");
    } else {
        setText(labelAlarm, "Weckruf " + state.nextAlarm +
                            (state.alarmSnoozed ? "  (schlummert)" : ""));
    }
    lv_obj_set_style_text_color(labelAlarm,
                                lv_color_hex(state.alarmActive ? kAlarm : kText), 0);

    setText(labelWeather, state.hasWeather
                ? String(state.temperature, 1) + " °C  " + state.weatherText
                : String());

    if (!state.online) {
        setText(labelStatus, state.error);
    } else if (state.mediaState == "playing" && !state.mediaTitle.isEmpty()) {
        setText(labelStatus, "Läuft: " + state.mediaTitle);
    } else {
        setText(labelStatus, "");
    }

    // --- Spalte Termine ---------------------------------------------------
    lv_obj_clean(areaEvents);
    if (state.events.empty()) {
        addLine(areaEvents, "Keine Termine", &ui_font_22, kTextMuted, 330);
    }
    for (const auto &event : state.events) {
        // Zeile 1: Tag und Uhrzeit, klein. Zeile 2: der Termin selbst.
        addLine(areaEvents, event.time.isEmpty() ? event.when : event.when + "  " + event.time,
                &ui_font_18, kTextMuted, 330);
        addLine(areaEvents, event.title, &ui_font_22, kText, 330);
    }

    // --- Spalte Nachrichten ----------------------------------------------
    lv_obj_clean(areaNews);
    if (state.news.empty()) {
        addLine(areaNews, "Keine Nachrichten", &ui_font_22, kTextMuted, 330);
    }
    for (const auto &headline : state.news) {
        if (!headline.source.isEmpty()) {
            addLine(areaNews, headline.source, &ui_font_18, kTextMuted, 330);
        }
        addLine(areaNews, headline.title, &ui_font_22, kText, 330);
    }

    // --- Anruf ------------------------------------------------------------
    if (state.callText.isEmpty()) {
        lv_obj_add_flag(bannerCall, LV_OBJ_FLAG_HIDDEN);
    } else {
        setText(labelCall, state.callText);
        lv_obj_clear_flag(bannerCall, LV_OBJ_FLAG_HIDDEN);
        // Nach vorn holen, damit das Banner wirklich über allem liegt.
        lv_obj_move_foreground(bannerCall);
    }

    // --- Bedienleiste -----------------------------------------------------
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
}

}  // namespace

void begin() {
    lvgl_port_lock(-1);
    buildHomeScreen();
    buildRadioScreen();
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
        case Pending::RadioPlay:   message = tablet_state::playStation(pendingStationId); break;
        case Pending::RadioStop:   message = tablet_state::stopRadio(); break;
        case Pending::RadioSleep:  message = tablet_state::sleepTimer(pendingSleepMinutes); break;
        case Pending::RadioSleepCancel: message = tablet_state::cancelSleepTimer(); break;

        case Pending::RadioList: {
            const String error = tablet_state::fetchStations(stations);
            lvgl_port_lock(-1);
            lv_obj_clean(listStations);
            for (const auto &station : stations) {
                lv_obj_t *btn = lv_list_add_btn(listStations, LV_SYMBOL_AUDIO,
                                                station.name.c_str());
                lv_obj_set_style_text_font(btn, &ui_font_22, 0);
                lv_obj_set_style_text_color(btn, lv_color_hex(kText), 0);
                lv_obj_set_style_bg_color(btn, lv_color_hex(kButton), 0);
                lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
                lv_obj_set_style_bg_color(btn, lv_color_hex(kButtonPressed), LV_STATE_PRESSED);
                lv_obj_set_style_radius(btn, 12, 0);
                lv_obj_set_style_pad_all(btn, 12, 0);
                lv_obj_add_event_cb(btn, onStationClicked, LV_EVENT_CLICKED, nullptr);
            }
            setText(labelRadioStatus, !error.isEmpty() ? error
                    : stations.empty() ? "Keine Favoriten – in der Weboberfläche anlegen"
                                       : String(stations.size()) + " Sender");
            lvgl_port_unlock();
            return;
        }

        case Pending::Scan:
            // Der Suchlauf ist asynchron; das Ergebnis kommt in den Rückruf.
            wifi_manager::startScan([](const std::vector<wifi_manager::Network> &networks) {
                lvgl_port_lock(-1);
                lv_obj_clean(listNetworks);
                for (const auto &net : networks) {
                    const char *symbol = net.encrypted ? LV_SYMBOL_WIFI : LV_SYMBOL_WARNING;
                    lv_obj_t *btn = lv_list_add_btn(listNetworks, symbol, net.ssid.c_str());
                    lv_obj_set_style_text_font(btn, &ui_font_22, 0);
                    lv_obj_set_style_text_color(btn, lv_color_hex(kText), 0);
                    lv_obj_set_style_bg_color(btn, lv_color_hex(kButton), 0);
                    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
                    lv_obj_set_style_radius(btn, 12, 0);
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
            setText(labelWifiHint, accepted ? "Zugang gespeichert, verbinde …"
                                            : "Eingabe ungültig (zu lang?)");
            lvgl_port_unlock();
            return;
        }

        case Pending::None:
            return;
    }

    const bool onRadioPage = lv_scr_act() == screenRadio;
    lvgl_port_lock(-1);
    if (onRadioPage) {
        setText(labelRadioStatus, message.isEmpty() ? "Erledigt" : "Fehler: " + message);
    } else {
        setText(labelMessage, message.isEmpty() ? "" : "Fehler: " + message);
    }
    lvgl_port_unlock();
}

}  // namespace ui
