#include "display_control.h"

#include <esp_display_panel.hpp>
#include <lvgl.h>
#include <time.h>

#include "lvgl_port/lvgl_v8_port.h"
#include "settings_store.h"

using namespace esp_panel::drivers;

namespace display_control {
namespace {

// Die Abdunkelungsfläche liegt über allem und nimmt keine Berührungen an –
// sonst könnte man die Oberfläche darunter nicht mehr bedienen.
lv_obj_t *dimLayer = nullptr;

bool screenOff = false;
uint32_t lastActivityMs = 0;
uint32_t lastCheckMs = 0;
bool lastNightState = false;

// Die Beleuchtung des Boards, von main.cpp durchgereicht.
Backlight *backlightDevice = nullptr;

void applyBacklight(bool on) {
    // An oder aus – mehr kann dieses Board nicht (siehe Kopf der .h-Datei).
    if (backlightDevice != nullptr) backlightDevice->setBrightness(on ? 100 : 0);
}

// Deckkraft der Abdunkelung aus der gewünschten Helligkeit.
// 100 % = gar nicht abdunkeln, 10 % = kräftig abdunkeln (aber nie ganz).
lv_opa_t dimOpacity(uint8_t percent) {
    if (percent >= 100) return LV_OPA_TRANSP;
    if (percent < 10) percent = 10;
    // 100 % -> 0, 10 % -> 216 (von 255). Linear dazwischen.
    return static_cast<lv_opa_t>((100 - percent) * 240 / 90);
}

void applyLevel(uint8_t percent) {
    if (dimLayer == nullptr) return;
    lv_obj_set_style_bg_opa(dimLayer, dimOpacity(percent), 0);
}

uint8_t currentLevel() {
    return nightActive() ? settings_store::nightBrightness() : settings_store::brightness();
}

}  // namespace

void begin(Backlight *backlight) {
    backlightDevice = backlight;
    lvgl_port_lock(-1);
    // Auf der obersten Ebene von LVGL: Sie liegt über allen Bildschirmen, also
    // wirkt die Abdunkelung auf jeder Seite – auch auf künftigen.
    dimLayer = lv_obj_create(lv_layer_top());
    lv_obj_set_size(dimLayer, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(dimLayer, 0, 0);
    lv_obj_set_style_bg_color(dimLayer, lv_color_black(), 0);
    lv_obj_set_style_border_width(dimLayer, 0, 0);
    lv_obj_set_style_radius(dimLayer, 0, 0);
    // Keine Berührungen abfangen und nicht scrollen: Die Fläche ist nur Optik.
    lv_obj_clear_flag(dimLayer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(dimLayer, LV_OBJ_FLAG_SCROLLABLE);
    applyLevel(currentLevel());
    lvgl_port_unlock();

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

    // Nachtmodus: Helligkeit wechseln, wenn die Stunde die Grenze überschreitet.
    const bool night = nightActive();
    if (night != lastNightState) {
        lastNightState = night;
        lvgl_port_lock(-1);
        applyLevel(currentLevel());
        lvgl_port_unlock();
    }

    // Abschaltzeit
    const uint16_t minutes = settings_store::screenOffMinutes();
    if (!screenOff && minutes > 0 && idleMs > static_cast<uint32_t>(minutes) * 60000UL) {
        screenOff = true;
        applyBacklight(false);
    }
}

// --- Einstellungen ----------------------------------------------------------

void setDayLevel(uint8_t percent) {
    settings_store::setBrightness(percent);
    if (!nightActive()) {
        lvgl_port_lock(-1);
        applyLevel(percent);
        lvgl_port_unlock();
    }
}
uint8_t dayLevel() { return settings_store::brightness(); }

void setNightLevel(uint8_t percent) {
    settings_store::setNightBrightness(percent);
    if (nightActive()) {
        lvgl_port_lock(-1);
        applyLevel(percent);
        lvgl_port_unlock();
    }
}
uint8_t nightLevel() { return settings_store::nightBrightness(); }

void setNightHours(uint8_t startHour, uint8_t endHour) {
    settings_store::setNightHours(startHour, endHour);
    lastNightState = nightActive();
    lvgl_port_lock(-1);
    applyLevel(currentLevel());
    lvgl_port_unlock();
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

void setOffAfterMinutes(uint16_t minutes) { settings_store::setScreenOffMinutes(minutes); }
uint16_t offAfterMinutes() { return settings_store::screenOffMinutes(); }

void turnOff() {
    screenOff = true;
    applyBacklight(false);
}

bool isOff() { return screenOff; }

}  // namespace display_control
