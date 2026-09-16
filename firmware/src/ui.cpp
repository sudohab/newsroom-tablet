#include "ui.h"

#include <lvgl.h>
#include <time.h>

#include <vector>

#include "api_client.h"
#include "lvgl_port/lvgl_v8_port.h"
#include "settings_store.h"
#include "tablet_config.h"
#include "screen_view.h"
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
constexpr uint32_t kColorAccentText = 0x6ea8ff;   // Spaltenüberschriften
constexpr uint32_t kColorLine = 0x2a2f3a;         // Trennlinien

// --- Seiten -----------------------------------------------------------------
// screenNewsroom zeigt die Ansicht, die der Pi zeichnet (orbital & Co.) und
// ist die Startseite. Ein Tipp darauf führt zur Bedienseite; von dort geht es
// mit "Zurueck" wieder zurück. So bleibt der Blick im Alltag auf der Ansicht,
// die Bedienung ist aber immer nur eine Berührung entfernt.
lv_obj_t *screenNewsroom = nullptr;
lv_obj_t *screenHome = nullptr;
lv_obj_t *screenWifi = nullptr;
lv_obj_t *screenRadio = nullptr;
bool newsroomAvailable = false;

// Startseite
lv_obj_t *labelClock = nullptr;
lv_obj_t *labelDate = nullptr;
lv_obj_t *labelWeather = nullptr;
lv_obj_t *labelAlarm = nullptr;
lv_obj_t *labelVolume = nullptr;
lv_obj_t *labelStatus = nullptr;
lv_obj_t *labelMessage = nullptr;
lv_obj_t *bannerCall = nullptr;
lv_obj_t *labelCall = nullptr;
lv_obj_t *btnSnooze = nullptr;
lv_obj_t *btnAlarmOff = nullptr;
lv_obj_t *btnNewsroom = nullptr;
lv_obj_t *listEvents = nullptr;
lv_obj_t *listNews = nullptr;
lv_obj_t *labelMedia = nullptr;

// Radio-Seite
lv_obj_t *labelNewsroomHint = nullptr;
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

void onNewsroomClicked(lv_event_t *) {
    lv_scr_load(screenHome);
}

void onShowNewsroomClicked(lv_event_t *) {
    if (newsroomAvailable) lv_scr_load(screenNewsroom);
}

void onRadioPageClicked(lv_event_t *) {
    lv_scr_load(screenRadio);
    setText(labelRadioStatus, "Hole Senderliste ...");
    lv_obj_clean(listStations);
    pending = Pending::RadioList;
}

void onStationClicked(lv_event_t *event) {
    // Welcher Listeneintrag? Die Position in der Liste entspricht der Position
    // in `stations` – die Kennung selbst steht nicht in der Anzeige.
    lv_obj_t *btn = lv_event_get_target(event);
    const uint32_t index = lv_obj_get_index(btn);
    if (index >= stations.size()) return;
    setText(labelRadioStatus, "Starte " + stations[index].name + " ...");
    pendingStationId = stations[index].id;
    pending = Pending::RadioPlay;
}

void onRadioStopClicked(lv_event_t *) {
    setText(labelRadioStatus, "Halte an ...");
    pending = Pending::RadioStop;
}

void onSleep30Clicked(lv_event_t *) {
    pendingSleepMinutes = 30;
    setText(labelRadioStatus, "Einschlaf-Timer 30 Minuten ...");
    pending = Pending::RadioSleep;
}

void onSleep60Clicked(lv_event_t *) {
    pendingSleepMinutes = 60;
    setText(labelRadioStatus, "Einschlaf-Timer 60 Minuten ...");
    pending = Pending::RadioSleep;
}

void onSleepCancelClicked(lv_event_t *) {
    setText(labelRadioStatus, "Timer aus ...");
    pending = Pending::RadioSleepCancel;
}

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
    lv_obj_clear_flag(screenHome, LV_OBJ_FLAG_SCROLLABLE);

    // --- Kopfzeile: Uhr, Datum, Weckruf, Wetter ---------------------------
    labelClock = makeLabel(screenHome, &lv_font_montserrat_48, kColorText,
                           LV_ALIGN_TOP_LEFT, 30, 14, "--:--");
    labelDate = makeLabel(screenHome, &lv_font_montserrat_20, kColorMuted,
                          LV_ALIGN_TOP_LEFT, 210, 22, cfg::kDeviceName);
    labelAlarm = makeLabel(screenHome, &lv_font_montserrat_20, kColorText,
                           LV_ALIGN_TOP_LEFT, 210, 52, "Weckruf --");
    labelWeather = makeLabel(screenHome, &lv_font_montserrat_28, kColorText,
                             LV_ALIGN_TOP_RIGHT, -30, 20);
    labelStatus = makeLabel(screenHome, &lv_font_montserrat_20, kColorMuted,
                            LV_ALIGN_TOP_RIGHT, -30, 60);

    // Trennlinie unter der Kopfzeile
    lv_obj_t *headerLine = lv_obj_create(screenHome);
    lv_obj_set_size(headerLine, 740, 2);
    lv_obj_align(headerLine, LV_ALIGN_TOP_MID, 0, 104);
    lv_obj_set_style_bg_color(headerLine, lv_color_hex(kColorLine), 0);
    lv_obj_set_style_border_width(headerLine, 0, 0);

    // --- Spalte links: Termine -------------------------------------------
    makeLabel(screenHome, &lv_font_montserrat_20, kColorAccentText,
              LV_ALIGN_TOP_LEFT, 30, 118, "TERMINE");
    listEvents = lv_obj_create(screenHome);
    lv_obj_set_size(listEvents, 350, 240);
    lv_obj_align(listEvents, LV_ALIGN_TOP_LEFT, 30, 146);
    lv_obj_set_style_bg_opa(listEvents, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(listEvents, 0, 0);
    lv_obj_set_style_pad_all(listEvents, 0, 0);
    // Untereinander, mit etwas Luft dazwischen
    lv_obj_set_flex_flow(listEvents, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(listEvents, 10, 0);
    lv_obj_clear_flag(listEvents, LV_OBJ_FLAG_SCROLLABLE);

    // Senkrechte Trennlinie zwischen den Spalten
    lv_obj_t *columnLine = lv_obj_create(screenHome);
    lv_obj_set_size(columnLine, 2, 240);
    lv_obj_align(columnLine, LV_ALIGN_TOP_LEFT, 399, 146);
    lv_obj_set_style_bg_color(columnLine, lv_color_hex(kColorLine), 0);
    lv_obj_set_style_border_width(columnLine, 0, 0);

    // --- Spalte rechts: Nachrichten ---------------------------------------
    makeLabel(screenHome, &lv_font_montserrat_20, kColorAccentText,
              LV_ALIGN_TOP_LEFT, 420, 118, "NACHRICHTEN");
    listNews = lv_obj_create(screenHome);
    lv_obj_set_size(listNews, 350, 240);
    lv_obj_align(listNews, LV_ALIGN_TOP_LEFT, 420, 146);
    lv_obj_set_style_bg_opa(listNews, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(listNews, 0, 0);
    lv_obj_set_style_pad_all(listNews, 0, 0);
    lv_obj_set_flex_flow(listNews, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(listNews, 10, 0);
    lv_obj_clear_flag(listNews, LV_OBJ_FLAG_SCROLLABLE);

    // --- Fußzeile: Bedienung ---------------------------------------------
    btnSnooze = makeButton(screenHome, "Schlummern", onSnoozeClicked, 180, 66,
                           LV_ALIGN_BOTTOM_LEFT, 30, -14, kColorAccent);
    btnAlarmOff = makeButton(screenHome, "Aus", onAlarmOffClicked, 90, 66,
                             LV_ALIGN_BOTTOM_LEFT, 220, -14, kColorAlarm);
    makeButton(screenHome, "-", onVolumeDownClicked, 66, 66,
               LV_ALIGN_BOTTOM_LEFT, 325, -14);
    labelVolume = makeLabel(screenHome, &lv_font_montserrat_28, kColorText,
                            LV_ALIGN_BOTTOM_LEFT, 400, -30, "--%");
    makeButton(screenHome, "+", onVolumeUpClicked, 66, 66,
               LV_ALIGN_BOTTOM_LEFT, 475, -14);
    makeButton(screenHome, "Radio", onRadioPageClicked, 110, 66,
               LV_ALIGN_BOTTOM_RIGHT, -150, -14);
    makeButton(screenHome, "WLAN", onWifiPageClicked, 110, 66,
               LV_ALIGN_BOTTOM_RIGHT, -30, -14);
    btnNewsroom = makeButton(screenHome, "Ansicht", onShowNewsroomClicked, 110, 66,
                             LV_ALIGN_BOTTOM_RIGHT, -270, -14);

    labelMessage = makeLabel(screenHome, &lv_font_montserrat_20, kColorMuted,
                             LV_ALIGN_BOTTOM_MID, 0, -92);

    // --- Anrufbanner: liegt über allem und ist sonst unsichtbar ------------
    bannerCall = lv_obj_create(screenHome);
    lv_obj_set_size(bannerCall, 800, 120);
    lv_obj_align(bannerCall, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(bannerCall, lv_color_hex(kColorAccent), 0);
    lv_obj_set_style_border_width(bannerCall, 0, 0);
    lv_obj_set_style_radius(bannerCall, 0, 0);
    lv_obj_clear_flag(bannerCall, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bannerCall, LV_OBJ_FLAG_HIDDEN);
    labelCall = lv_label_create(bannerCall);
    lv_obj_set_style_text_font(labelCall, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(labelCall, lv_color_hex(kColorText), 0);
    lv_obj_center(labelCall);
}

void buildRadioScreen() {
    screenRadio = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screenRadio, lv_color_hex(kColorBackground), 0);

    makeLabel(screenRadio, &lv_font_montserrat_28, kColorText,
              LV_ALIGN_TOP_LEFT, 30, 20, "Radio");

    // Die Favoritenliste ist bewusst breit und die Einträge hoch: Sender
    // wählt man im Vorbeigehen, oft ohne hinzusehen.
    listStations = lv_list_create(screenRadio);
    lv_obj_set_size(listStations, 480, 380);
    lv_obj_align(listStations, LV_ALIGN_TOP_LEFT, 30, 70);

    labelRadioStatus = makeLabel(screenRadio, &lv_font_montserrat_20, kColorMuted,
                                 LV_ALIGN_TOP_RIGHT, -30, 70);

    makeButton(screenRadio, "Aus", onRadioStopClicked, 240, 70,
               LV_ALIGN_TOP_RIGHT, -30, 120, kColorAlarm);
    makeButton(screenRadio, "30 Min", onSleep30Clicked, 115, 70,
               LV_ALIGN_TOP_RIGHT, -155, 200);
    makeButton(screenRadio, "60 Min", onSleep60Clicked, 115, 70,
               LV_ALIGN_TOP_RIGHT, -30, 200);
    makeButton(screenRadio, "Timer aus", onSleepCancelClicked, 240, 70,
               LV_ALIGN_TOP_RIGHT, -30, 280);
    makeButton(screenRadio, "Zurueck", onBackClicked, 240, 70,
               LV_ALIGN_TOP_RIGHT, -30, 380);
}

void buildNewsroomScreen() {
    screenNewsroom = lv_obj_create(nullptr);
    // Weiß, weil die Ansicht für E-Ink gezeichnet ist: schwarze Schrift auf
    // weißem Grund. Ein dunkler Rand ringsum sähe aus wie ein Fehler.
    lv_obj_set_style_bg_color(screenNewsroom, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_pad_all(screenNewsroom, 0, 0);
    lv_obj_clear_flag(screenNewsroom, LV_OBJ_FLAG_SCROLLABLE);

    newsroomAvailable = screen_view::begin(screenNewsroom);
    if (newsroomAvailable) {
        lv_obj_add_event_cb(screen_view::image(), onNewsroomClicked, LV_EVENT_CLICKED, nullptr);
    }
    // Nach dem Bild angelegt, liegt also darüber: Solange kein Bild da ist
    // (Ansicht abgeschaltet, Pi nicht erreichbar), stünde man sonst vor einer
    // weißen Fläche ohne Erklärung.
    labelNewsroomHint = makeLabel(screenNewsroom, &lv_font_montserrat_20, 0x333333,
                                  LV_ALIGN_CENTER, 0, 0, "Hole Ansicht vom Pi ...");
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

    // --- Kopfzeile --------------------------------------------------------
    if (state.nextAlarm.isEmpty()) {
        setText(labelAlarm, "Kein Weckruf gestellt");
    } else {
        setText(labelAlarm, "Weckruf " + state.nextAlarm +
                            (state.alarmSnoozed ? "  (schlummert)" : ""));
    }
    // Der Weckruf sticht nur hervor, wenn er gerade klingelt.
    lv_obj_set_style_text_color(labelAlarm,
                                lv_color_hex(state.alarmActive ? kColorAlarm : kColorText), 0);

    setText(labelWeather, state.hasWeather
                ? String(state.temperature, 1) + " C  " + state.weatherText
                : String());

    // Statuszeile: Verbindung, oder was gerade läuft
    if (!state.online) {
        setText(labelStatus, "Pi: " + state.error);
    } else if (state.mediaState == "playing" && !state.mediaTitle.isEmpty()) {
        setText(labelStatus, "Laeuft: " + state.mediaTitle);
    } else {
        setText(labelStatus, "");
    }

    // --- Spalte Termine ---------------------------------------------------
    lv_obj_clean(listEvents);
    if (state.events.empty()) {
        lv_obj_t *label = lv_label_create(listEvents);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(kColorMuted), 0);
        lv_label_set_text(label, "Keine Termine");
    }
    for (const auto &event : state.events) {
        // Zeile 1: Tag und Uhrzeit, klein. Zeile 2: der Termin selbst, groß.
        lv_obj_t *when = lv_label_create(listEvents);
        lv_obj_set_style_text_font(when, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(when, lv_color_hex(kColorMuted), 0);
        lv_label_set_text(when, (event.time.isEmpty() ? event.when
                                                      : event.when + "  " + event.time).c_str());
        lv_obj_t *title = lv_label_create(listEvents);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(kColorText), 0);
        // Lange Titel umbrechen statt abschneiden – die Spalte ist schmal.
        lv_obj_set_width(title, 340);
        lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
        lv_label_set_text(title, event.title.c_str());
    }

    // --- Spalte Nachrichten ----------------------------------------------
    lv_obj_clean(listNews);
    if (state.news.empty()) {
        lv_obj_t *label = lv_label_create(listNews);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(kColorMuted), 0);
        lv_label_set_text(label, "Keine Nachrichten");
    }
    for (const auto &headline : state.news) {
        if (!headline.source.isEmpty()) {
            lv_obj_t *source = lv_label_create(listNews);
            lv_obj_set_style_text_font(source, &lv_font_montserrat_20, 0);
            lv_obj_set_style_text_color(source, lv_color_hex(kColorMuted), 0);
            lv_label_set_text(source, headline.source.c_str());
        }
        lv_obj_t *title = lv_label_create(listNews);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(kColorText), 0);
        lv_obj_set_width(title, 340);
        lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
        lv_label_set_text(title, headline.title.c_str());
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

    // --- Fußzeile ---------------------------------------------------------
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

    // Der Knopf zur Pi-Ansicht gibt es nur, wenn der Bildpuffer angelegt
    // werden konnte.
    if (!newsroomAvailable) lv_obj_add_flag(btnNewsroom, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace

void begin() {
    lvgl_port_lock(-1);
    buildHomeScreen();
    buildWifiScreen();
    buildRadioScreen();
    buildNewsroomScreen();
    // Startseite ist die eigene Ansicht (Uhr, Wetter, Termine, Nachrichten).
    // Die vom Pi gezeichnete Ansicht ist über den Knopf "Ansicht" erreichbar,
    // sobald von dort ein Bild kommt.
    lv_scr_load(screenHome);
    applySnapshot();
    lvgl_port_unlock();
}

void tick() {
    const uint32_t now = millis();

    // Das Bild nur holen, während die Ansicht tatsächlich zu sehen ist –
    // 48 KB für eine Seite, die niemand ansieht, wären verschwendet.
    const bool newsroomVisible = newsroomAvailable && lv_scr_act() == screenNewsroom;
    screen_view::loop(newsroomVisible);
    if (newsroomVisible) {
        const String hint = screen_view::lastError();
        lvgl_port_lock(-1);
        if (!hint.isEmpty()) {
            setText(labelNewsroomHint, hint + " – tippen fuer zurueck");
            lv_obj_clear_flag(labelNewsroomHint, LV_OBJ_FLAG_HIDDEN);
        } else if (screen_view::hasImage()) {
            lv_obj_add_flag(labelNewsroomHint, LV_OBJ_FLAG_HIDDEN);
        }
        lvgl_port_unlock();
    }

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
                lv_obj_set_style_text_font(btn, &lv_font_montserrat_20, 0);
                lv_obj_add_event_cb(btn, onStationClicked, LV_EVENT_CLICKED, nullptr);
            }
            setText(labelRadioStatus, !error.isEmpty() ? error
                    : stations.empty() ? "Keine Favoriten – in der Weboberflaeche anlegen"
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
