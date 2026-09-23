#include "display_control.h"

#include <esp_display_panel.hpp>
#include <lvgl.h>
#include <time.h>

#include "lvgl_port/lvgl_v8_port.h"
#include "settings_store.h"

using namespace esp_panel::drivers;

namespace display_control {
namespace {

bool screenOff = false;
uint32_t lastActivityMs = 0;
uint32_t lastCheckMs = 0;

// Die Beleuchtung des Boards, von main.cpp durchgereicht.
Backlight *backlightDevice = nullptr;

void applyBacklight(bool on) {
    // An oder aus – mehr kann dieses Board nicht (siehe Kopf der .h-Datei).
    if (backlightDevice != nullptr) backlightDevice->setBrightness(on ? 100 : 0);
}

}  // namespace

void begin(Backlight *backlight) {
    backlightDevice = backlight;
    applyBacklight(true);
    lastActivityMs = millis();
}

void loop() {
    const uint32_t now = millis();
    // Zweimal je Sekunde reicht: Es geht um Minuten und Stunden.
    if (now - lastCheckMs < 500) return;
    lastCheckMs = now;

    // LVGL weiß, wann zuletzt berührt wurde – daraus ergibt sich beides:
    // Aufwecken und die Abschaltzeit.
    const uint32_t idleMs = lv_disp_get_inactive_time(nullptr);
    if (idleMs < 500) {
        lastActivityMs = now;
        if (screenOff) {
            // Erste Berührung nach dem Abschalten: nur aufwecken. Sie zählt
            // bewusst nicht als Bedienung – sonst würde man versehentlich
            // einen Sender starten, nur weil man den Bildschirm wecken wollte.
            screenOff = false;
            applyBacklight(true);
        }
    }

    // Welche Abschaltzeit gilt gerade – die für den Tag oder die für die Nacht?
    const uint16_t minutes = nightActive() ? settings_store::nightOffMinutes()
                                           : settings_store::dayOffMinutes();
    if (!screenOff && minutes > 0 && idleMs > static_cast<uint32_t>(minutes) * 60000UL) {
        screenOff = true;
        applyBacklight(false);
    }
}

// --- Einstellungen ----------------------------------------------------------

void setNightHours(uint8_t startHour, uint8_t endHour) {
    settings_store::setNightHours(startHour, endHour);
}
uint8_t nightStart() { return settings_store::nightStartHour(); }
uint8_t nightEnd() { return settings_store::nightEndHour(); }

bool nightActive() {
    const uint8_t start = settings_store::nightStartHour();
    const uint8_t end = settings_store::nightEndHour();
    if (start == end) return false;   // Nachtmodus aus

    struct tm now;
    if (!getLocalTime(&now, 0)) return false;   // ohne Uhrzeit kein Nachtmodus
    const int hour = now.tm_hour;
    // Die Nacht geht meist über Mitternacht (z. B. 22 bis 7) – dann gilt
    // „nach Beginn ODER vor Ende", sonst „dazwischen".
    return (start > end) ? (hour >= start || hour < end)
                         : (hour >= start && hour < end);
}

void setDayOffMinutes(uint16_t minutes) { settings_store::setDayOffMinutes(minutes); }
uint16_t dayOffMinutes() { return settings_store::dayOffMinutes(); }

void setNightOffMinutes(uint16_t minutes) { settings_store::setNightOffMinutes(minutes); }
uint16_t nightOffMinutes() { return settings_store::nightOffMinutes(); }

void turnOff() {
    screenOff = true;
    applyBacklight(false);
}

bool isOff() { return screenOff; }

}  // namespace display_control
