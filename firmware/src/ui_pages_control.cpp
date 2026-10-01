#include <vector>

#include "fonts/ui_fonts.h"
#include "lvgl_port/lvgl_v8_port.h"
#include "settings_store.h"
#include "tablet_data.h"
#include "tablet_state.h"
#include "ui_pages.h"
#include "ui_theme.h"
#include "wifi_manager.h"

// Die Bedienseiten: Radio, Wecker, WLAN.
//
// Grundregel wie überall: Ein Knopfdruck merkt sich nur den Wunsch; ausgeführt
// wird er in `work()`, also außerhalb der LVGL-Sperre. Sonst stünde die
// Anzeige während jeder Anfrage still.

namespace ui_pages {
namespace {
using namespace ui_theme;
constexpr lv_coord_t kWidth = 752;
constexpr lv_coord_t kHeight = 300;
}  // namespace

// ---------------------------------------------------------------------------
// Radio
// ---------------------------------------------------------------------------
namespace radio {
namespace {

enum class Job { None, List, Play, Stop, Sleep, SleepCancel, VolumeUp, VolumeDown };
volatile Job job = Job::None;

lv_obj_t *page = nullptr;
lv_obj_t *list = nullptr;
lv_obj_t *status = nullptr;
lv_obj_t *labelVolume = nullptr;
std::vector<tablet_state::Station> stations;
String pendingStationId;
int pendingMinutes = 0;
bool loaded = false;

void onStation(lv_event_t *event) {
    const uint32_t index = lv_obj_get_index(lv_event_get_target(event));
    if (index >= stations.size()) return;
    lv_label_set_text(status, ("Starte " + stations[index].name + " …").c_str());
    pendingStationId = stations[index].id;
    job = Job::Play;
}

void onStop(lv_event_t *) { lv_label_set_text(status, "Halte an …"); job = Job::Stop; }
void onSleep30(lv_event_t *) { pendingMinutes = 30; job = Job::Sleep; }
void onSleep60(lv_event_t *) { pendingMinutes = 60; job = Job::Sleep; }
void onSleepOff(lv_event_t *) { job = Job::SleepCancel; }
void onLouder(lv_event_t *) { job = Job::VolumeUp; }
void onQuieter(lv_event_t *) { job = Job::VolumeDown; }

}  // namespace

lv_obj_t *create(lv_obj_t *parent) {
    page = makeSection(parent, 0, 0, kWidth, kHeight);
    makeTitle(page, "Radio");
    status = makeStatus(page);

    list = lv_list_create(page);
    lv_obj_set_size(list, 430, kHeight - 50);
    lv_obj_set_pos(list, 0, 50);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 8, 0);

    // Rechte Spalte: anhalten, Einschlaf-Timer, Lautstärke
    makeButton(page, "Aus", onStop, 250, 46, LV_ALIGN_TOP_RIGHT, 0, 50, kAlarm);

    lv_obj_t *timerBar = makeButtonBar(page, kWidth - 250, 108, 250, 46);
    addBarButton(timerBar, "30 Min", onSleep30, 120, 46);
    addBarButton(timerBar, "60 Min", onSleep60, 120, 46);
    makeButton(page, "Timer aus", onSleepOff, 250, 46, LV_ALIGN_TOP_RIGHT, 0, 166);

    // Lautstärke gehört auf diese Seite (Wunsch von Hannes).
    lv_obj_t *volumeBar = makeButtonBar(page, kWidth - 250, 224, 250, 46);
    addBarButton(volumeBar, "–", onQuieter, 70, 46);
    labelVolume = lv_label_create(volumeBar);
    lv_obj_set_style_text_font(labelVolume, &ui_font_22, 0);
    lv_obj_set_style_text_color(labelVolume, lv_color_hex(kText), 0);
    lv_obj_set_width(labelVolume, 90);
    lv_obj_set_style_text_align(labelVolume, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(labelVolume, "--%");
    addBarButton(volumeBar, "+", onLouder, 70, 46);
    return page;
}

void activate() {
    if (!loaded) job = Job::List;
}

void work() {
    // Lautstärke laufend nachziehen – sie ändert sich auch von anderswo.
    const tablet_state::Snapshot &state = tablet_state::current();
    lvgl_port_lock(-1);
    lv_label_set_text(labelVolume, state.volume >= 0
                          ? (String(state.volume) + "%").c_str() : "--%");
    lvgl_port_unlock();

    const Job current = job;
    if (current == Job::None) return;
    job = Job::None;

    String message;
    switch (current) {
        case Job::Play:        message = tablet_state::playStation(pendingStationId); break;
        case Job::Stop:        message = tablet_state::stopRadio(); break;
        case Job::Sleep:       message = tablet_state::sleepTimer(pendingMinutes); break;
        case Job::SleepCancel: message = tablet_state::cancelSleepTimer(); break;
        case Job::VolumeUp:    message = tablet_state::volumeUp(); break;
        case Job::VolumeDown:  message = tablet_state::volumeDown(); break;

        case Job::List: {
            const String error = tablet_state::fetchStations(stations);
            loaded = error.isEmpty();
            lvgl_port_lock(-1);
            lv_obj_clean(list);
            for (const auto &station : stations) {
                lv_obj_t *btn = lv_list_add_btn(list, nullptr, station.name.c_str());
                styleListButton(btn);
                lv_obj_add_event_cb(btn, onStation, LV_EVENT_CLICKED, nullptr);
            }
            lv_label_set_text(status, !error.isEmpty() ? error.c_str()
                : stations.empty() ? "Keine Favoriten – in der Weboberfläche anlegen"
                                   : (String(stations.size()) + " Sender").c_str());
            lvgl_port_unlock();
            return;
        }
        case Job::None: return;
    }

    lvgl_port_lock(-1);
    lv_label_set_text(status, message.isEmpty() ? "Erledigt" : ("Fehler: " + message).c_str());
    lvgl_port_unlock();
}
}  // namespace radio

// ---------------------------------------------------------------------------
// Wecker
// ---------------------------------------------------------------------------
namespace alarms {
namespace {

enum class Job { None, Load, Save, Toggle, AlarmVolume };
volatile Job job = Job::None;

lv_obj_t *page = nullptr;
lv_obj_t *status = nullptr;
lv_obj_t *area = nullptr;
lv_obj_t *labelAlarmVolume = nullptr;
int pendingAlarmVolume = -1;
// Der Pi nimmt höchstens eine Aktion je Sekunde an; und bis die nächste
// Zustandsabfrage den neuen Wert bringt, bleibt die Anzeige beim Gewünschten.
uint32_t lastAlarmVolumeSentMs = 0;
constexpr uint32_t kActionGapMs = 1100;
constexpr uint32_t kHoldDisplayMs = 4000;
lv_obj_t *rollerHour = nullptr;
lv_obj_t *rollerMinute = nullptr;
lv_obj_t *daySwitches[7] = {};

std::vector<tablet_data::Alarm> list;
String pendingId;
bool pendingEnabled = false;

const char *kDayNames[7] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};

void onSave(lv_event_t *) {
    lv_label_set_text(status, "Speichere …");
    job = Job::Save;
}

// Ein vorhandener Wecker wurde angetippt: Zeit und Tage in die Einsteller
// übernehmen, damit man ihn ändern kann statt einen neuen anzulegen.
void onAlarmClicked(lv_event_t *event) {
    const uint32_t index = lv_obj_get_index(lv_event_get_target(event));
    if (index >= list.size()) return;
    const tablet_data::Alarm &alarm = list[index];
    pendingId = alarm.id;
    lv_roller_set_selected(rollerHour, alarm.time.substring(0, 2).toInt(), LV_ANIM_ON);
    lv_roller_set_selected(rollerMinute, alarm.time.substring(3, 5).toInt(), LV_ANIM_ON);
    for (int day = 0; day < 7; ++day) {
        if (alarm.days[day]) lv_obj_add_state(daySwitches[day], LV_STATE_CHECKED);
        else lv_obj_clear_state(daySwitches[day], LV_STATE_CHECKED);
    }
    lv_label_set_text(status, ("Ändere " + alarm.time).c_str());
}

String twoDigits(int value) {
    return (value < 10 ? "0" : "") + String(value);
}

// Wecker-Lautstärke in 10er-Schritten. Ausgangspunkt ist der zuletzt
// gewünschte Wert, sonst der vom Pi gemeldete – so zählen schnelle
// Doppeltipper richtig, auch bevor der Pi geantwortet hat.
void changeAlarmVolume(int step) {
    const int base = pendingAlarmVolume >= 0 ? pendingAlarmVolume
                                             : tablet_state::current().alarmVolume;
    if (base < 0) return;
    pendingAlarmVolume = constrain(base + step, 0, 100);
    lv_label_set_text(labelAlarmVolume, (String(pendingAlarmVolume) + "%").c_str());
    job = Job::AlarmVolume;
}
void onAlarmLouder(lv_event_t *) { changeAlarmVolume(10); }
void onAlarmQuieter(lv_event_t *) { changeAlarmVolume(-10); }

}  // namespace

lv_obj_t *create(lv_obj_t *parent) {
    page = makeSection(parent, 0, 0, kWidth, kHeight);
    makeTitle(page, "Wecker");
    status = makeStatus(page);

    // --- links: Zeit einstellen -------------------------------------------
    // Zwei Walzen statt Tastatur: Das trifft man auch halb wach.
    String hours, minutes;
    for (int h = 0; h < 24; ++h) hours += twoDigits(h) + (h < 23 ? "\n" : "");
    for (int m = 0; m < 60; ++m) minutes += twoDigits(m) + (m < 59 ? "\n" : "");

    rollerHour = lv_roller_create(page);
    lv_roller_set_options(rollerHour, hours.c_str(), LV_ROLLER_MODE_NORMAL);
    lv_obj_set_pos(rollerHour, 0, 50);
    lv_obj_set_width(rollerHour, 90);
    lv_roller_set_visible_row_count(rollerHour, 3);
    lv_obj_set_style_text_font(rollerHour, &ui_font_30, 0);
    lv_obj_set_style_bg_color(rollerHour, lv_color_hex(kSurface), 0);
    lv_obj_set_style_text_color(rollerHour, lv_color_hex(kText), 0);
    lv_obj_set_style_border_width(rollerHour, 0, 0);
    lv_obj_set_style_bg_color(rollerHour, lv_color_hex(kAccent), LV_PART_SELECTED);

    rollerMinute = lv_roller_create(page);
    lv_roller_set_options(rollerMinute, minutes.c_str(), LV_ROLLER_MODE_NORMAL);
    lv_obj_set_pos(rollerMinute, 100, 50);
    lv_obj_set_width(rollerMinute, 90);
    lv_roller_set_visible_row_count(rollerMinute, 3);
    lv_obj_set_style_text_font(rollerMinute, &ui_font_30, 0);
    lv_obj_set_style_bg_color(rollerMinute, lv_color_hex(kSurface), 0);
    lv_obj_set_style_text_color(rollerMinute, lv_color_hex(kText), 0);
    lv_obj_set_style_border_width(rollerMinute, 0, 0);
    lv_obj_set_style_bg_color(rollerMinute, lv_color_hex(kAccent), LV_PART_SELECTED);

    // --- Wochentage als Schalter ------------------------------------------
    makeLabel(page, &ui_font_18, kTextMuted, LV_ALIGN_TOP_LEFT, 0, 170, "WOCHENTAGE");
    for (int day = 0; day < 7; ++day) {
        const lv_coord_t x = day * 62;
        makeLabel(page, &ui_font_18, kTextMuted, LV_ALIGN_TOP_LEFT, x + 8, 196, kDayNames[day]);
        lv_obj_t *sw = lv_switch_create(page);
        lv_obj_set_size(sw, 48, 26);
        lv_obj_set_pos(sw, x, 220);
        lv_obj_set_style_bg_color(sw, lv_color_hex(kSurface), 0);
        lv_obj_set_style_bg_color(sw, lv_color_hex(kAccent), LV_PART_INDICATOR | LV_STATE_CHECKED);
        daySwitches[day] = sw;
        // Montag bis Freitag sind vorausgewählt – der häufigste Fall.
        if (day >= 1 && day <= 5) lv_obj_add_state(sw, LV_STATE_CHECKED);
    }

    makeButton(page, "Speichern", onSave, 200, 46, LV_ALIGN_TOP_LEFT, 0, 254, kAccent);

    // --- rechts: vorhandene Wecker ----------------------------------------
    makeSeparator(page, 440, 46, 1, kHeight - 50);
    makeLabel(page, &ui_font_18, kTextMuted, LV_ALIGN_TOP_LEFT, 464, 50, "GESTELLT (PI)");
    area = makeScrollArea(page, 464, 76, kWidth - 464, 120);

    // Wecker-Lautstärke (Weckton, Ansage, Weckradio) – getrennt von der
    // Lautstärke für Radio und Podcast auf der Radio-Seite. Hier stand vorher
    // der ausgegraute Schalter „am Gerät klingeln": Das Board hat keinen Ton
    // (Idee zurückgestellt, siehe BAUPLAN 30.09.2026).
    makeLabel(page, &ui_font_18, kTextMuted, LV_ALIGN_TOP_LEFT, 464, 204, "WECKER-LAUTSTÄRKE");
    lv_obj_t *volumeBar = makeButtonBar(page, 464, 230, 250, 46);
    addBarButton(volumeBar, "–", onAlarmQuieter, 70, 46);
    labelAlarmVolume = lv_label_create(volumeBar);
    lv_obj_set_style_text_font(labelAlarmVolume, &ui_font_22, 0);
    lv_obj_set_style_text_color(labelAlarmVolume, lv_color_hex(kText), 0);
    lv_obj_set_width(labelAlarmVolume, 90);
    lv_obj_set_style_text_align(labelAlarmVolume, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(labelAlarmVolume, "--%");
    addBarButton(volumeBar, "+", onAlarmLouder, 70, 46);
    return page;
}

void activate() { job = Job::Load; }

void work() {
    // Wecker-Lautstärke nachziehen – sie ändert sich auch am Handy oder in
    // der Weboberfläche. Solange ein eigener Wunsch unterwegs ist, bleibt er
    // stehen.
    if (pendingAlarmVolume < 0 && millis() - lastAlarmVolumeSentMs > kHoldDisplayMs) {
        const int value = tablet_state::current().alarmVolume;
        lvgl_port_lock(-1);
        lv_label_set_text(labelAlarmVolume, value >= 0 ? (String(value) + "%").c_str() : "--%");
        lvgl_port_unlock();
    }

    const Job current = job;
    if (current == Job::None) return;
    job = Job::None;

    if (current == Job::AlarmVolume) {
        // Mehrere Tipps hintereinander: erst nach der Pause des Pi schicken,
        // dann gleich den letzten gewünschten Wert.
        if (millis() - lastAlarmVolumeSentMs < kActionGapMs) { job = Job::AlarmVolume; return; }
        const int wanted = pendingAlarmVolume;
        const String error = tablet_state::setAlarmVolume(wanted);
        lastAlarmVolumeSentMs = millis();
        if (pendingAlarmVolume != wanted) { job = Job::AlarmVolume; return; }
        pendingAlarmVolume = -1;
        lvgl_port_lock(-1);
        lv_label_set_text(status, error.isEmpty() ? "" : ("Fehler: " + error).c_str());
        lvgl_port_unlock();
        return;
    }

    if (current == Job::Load) {
        const String error = tablet_data::fetchAlarms(list);
        lvgl_port_lock(-1);
        lv_label_set_text(status, error.isEmpty() ? "" : error.c_str());
        lv_obj_clean(area);
        if (list.empty()) addLine(area, "Kein Wecker gestellt", &ui_font_22, kTextMuted, 260);
        for (const auto &alarm : list) {
            String days;
            for (int day = 0; day < 7; ++day) {
                if (alarm.days[day]) days += String(kDayNames[day]) + " ";
            }
            lv_obj_t *entry = lv_btn_create(area);
            lv_obj_set_width(entry, 260);
            lv_obj_set_height(entry, LV_SIZE_CONTENT);
            styleListButton(entry);
            lv_obj_add_event_cb(entry, onAlarmClicked, LV_EVENT_CLICKED, nullptr);
            lv_obj_t *label = lv_label_create(entry);
            lv_obj_set_style_text_font(label, &ui_font_22, 0);
            lv_obj_set_style_text_color(label,
                lv_color_hex(alarm.enabled ? kText : kTextMuted), 0);
            lv_label_set_text(label, (alarm.time + "   " + days).c_str());
        }
        lvgl_port_unlock();
        return;
    }

    if (current == Job::Save) {
        // Zeit und Tage aus den Einstellern lesen – unter der Sperre, weil es
        // LVGL-Objekte sind.
        String time;
        bool days[7] = {};
        lvgl_port_lock(-1);
        time = twoDigits(lv_roller_get_selected(rollerHour)) + ":"
             + twoDigits(lv_roller_get_selected(rollerMinute));
        for (int day = 0; day < 7; ++day) {
            days[day] = lv_obj_has_state(daySwitches[day], LV_STATE_CHECKED);
        }
        lvgl_port_unlock();

        const String error = tablet_data::saveAlarm(pendingId, time, days, true);
        pendingId = "";
        lvgl_port_lock(-1);
        lv_label_set_text(status, error.isEmpty() ? "Gespeichert" : ("Fehler: " + error).c_str());
        lvgl_port_unlock();
        if (error.isEmpty()) job = Job::Load;   // Liste auffrischen
    }
}
}  // namespace alarms

// ---------------------------------------------------------------------------
// WLAN
// ---------------------------------------------------------------------------
namespace wifi {
namespace {

enum class Job { None, Scan, Connect };
volatile Job job = Job::None;

lv_obj_t *page = nullptr;
lv_obj_t *list = nullptr;
lv_obj_t *status = nullptr;
lv_obj_t *selected = nullptr;
lv_obj_t *input = nullptr;
lv_obj_t *keyboard = nullptr;
String selectedSsid;
String pendingPassword;

void onScan(lv_event_t *) {
    lv_label_set_text(status, "Suche …");
    lv_obj_clean(list);
    job = Job::Scan;
}

void onNetwork(lv_event_t *event) {
    const char *text = lv_list_get_btn_text(list, lv_event_get_target(event));
    if (text == nullptr) return;
    selectedSsid = text;
    lv_label_set_text(selected, ("Netz: " + selectedSsid).c_str());
    lv_keyboard_set_textarea(keyboard, input);
    lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}

void onConnect(lv_event_t *) {
    if (selectedSsid.isEmpty()) {
        lv_label_set_text(status, "Erst ein Netz wählen");
        return;
    }
    pendingPassword = lv_textarea_get_text(input);
    // Passwortfeld sofort leeren: Es soll nicht sichtbar stehen bleiben.
    lv_textarea_set_text(input, "");
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(status, "Verbinde …");
    job = Job::Connect;
}

}  // namespace

lv_obj_t *create(lv_obj_t *parent) {
    page = makeSection(parent, 0, 0, kWidth, kHeight);
    makeTitle(page, "WLAN");
    status = makeStatus(page);

    list = lv_list_create(page);
    lv_obj_set_size(list, 380, kHeight - 50);
    lv_obj_set_pos(list, 0, 50);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 8, 0);

    selected = makeLabel(page, &ui_font_22, kText, LV_ALIGN_TOP_LEFT, 410, 50, "Netz: –");

    input = lv_textarea_create(page);
    lv_obj_set_size(input, 320, 48);
    lv_obj_set_pos(input, 410, 84);
    lv_obj_set_style_text_font(input, &ui_font_22, 0);
    lv_obj_set_style_bg_color(input, lv_color_hex(kSurface), 0);
    lv_obj_set_style_text_color(input, lv_color_hex(kText), 0);
    lv_obj_set_style_border_width(input, 0, 0);
    lv_obj_set_style_radius(input, 10, 0);
    lv_textarea_set_one_line(input, true);
    // Eingabe als Punkte: Das WLAN-Passwort soll nicht offen auf einem
    // Bildschirm im Wohnzimmer stehen.
    lv_textarea_set_password_mode(input, true);
    lv_textarea_set_max_length(input, settings_store::kMaxPassword);
    lv_textarea_set_placeholder_text(input, "WLAN-Passwort");

    lv_obj_t *bar = makeButtonBar(page, 410, 146, 330, 46);
    addBarButton(bar, "Suchen", onScan, 150, 46);
    addBarButton(bar, "Verbinden", onConnect, 160, 46, kAccent);

    // Die Tastatur liegt über der Seite und verdeckt beim Tippen den Rest.
    keyboard = lv_keyboard_create(page);
    lv_obj_set_size(keyboard, kWidth, 150);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(kBackground), 0);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    return page;
}

void activate() {
    lvgl_port_lock(-1);
    lv_label_set_text(status, wifi_manager::statusText().c_str());
    lvgl_port_unlock();
}

void work() {
    const Job current = job;
    if (current == Job::None) return;
    job = Job::None;

    if (current == Job::Scan) {
        wifi_manager::startScan([](const std::vector<wifi_manager::Network> &networks) {
            lvgl_port_lock(-1);
            lv_obj_clean(list);
            for (const auto &net : networks) {
                lv_obj_t *btn = lv_list_add_btn(list, nullptr, net.ssid.c_str());
                styleListButton(btn);
                lv_obj_add_event_cb(btn, onNetwork, LV_EVENT_CLICKED, nullptr);
            }
            lv_label_set_text(status, networks.empty()
                                  ? "Keine Netze gefunden"
                                  : (String(networks.size()) + " Netze").c_str());
            lvgl_port_unlock();
        });
        return;
    }

    if (current == Job::Connect) {
        const bool accepted = wifi_manager::connectWith(selectedSsid, pendingPassword);
        // Passwort im Arbeitsspeicher überschreiben, sobald es übergeben ist.
        for (size_t i = 0; i < pendingPassword.length(); ++i) pendingPassword[i] = '\0';
        pendingPassword = "";
        lvgl_port_lock(-1);
        lv_label_set_text(status, accepted ? "Zugang gespeichert" : "Eingabe ungültig");
        lvgl_port_unlock();
    }
}
}  // namespace wifi

}  // namespace ui_pages
