/**
 * newsroom-tablet – Firmware für das Waveshare ESP32-S3-Touch-LCD-7
 *
 * Etappe 1: Das Gerät startet Panel und Touch, richtet auf Wunsch das WLAN
 * direkt am Bildschirm ein, holt sich die Uhrzeit und prüft auf Knopfdruck die
 * verschlüsselte Verbindung zu newsroom21 auf dem Pi.
 *
 * Ablauf beim Start:
 *   1. NVS öffnen (gespeicherte Einstellungen, WLAN, Geräte-Token)
 *   2. Board hochfahren: RGB-Panel, GT911-Touch, CH422G-Expander, Backlight
 *   3. LVGL starten (läuft in einer eigenen Aufgabe)
 *   4. Oberfläche aufbauen
 *   5. WLAN verbinden, sobald ein Zugang hinterlegt ist
 *   6. Uhrzeit vom Router bzw. aus dem Internet holen
 *
 * Geheimnisse stehen ausschließlich im NVS des Geräts, niemals im Quelltext
 * (claude.md §1).
 */

#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include <time.h>

#include "api_client.h"
#include "lvgl_port/lvgl_v8_port.h"
#include "root_ca.h"
#include "serial_console.h"
#include "settings_store.h"
#include "tablet_config.h"
#include "tablet_state.h"
#include "ui.h"
#include "wifi_manager.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

namespace {

Board *board = nullptr;
bool timeConfigured = false;

// Holt die Uhrzeit, sobald das WLAN steht. Zuerst wird der Router gefragt –
// er antwortet auch dann, wenn das Tablet keinen Weg ins Internet hat.
void configureTimeOnce() {
    if (timeConfigured || wifi_manager::state() != wifi_manager::State::Connected) return;
    configTzTime(cfg::kTimezone, cfg::kNtpPrimary, cfg::kNtpSecondary);
    timeConfigured = true;
    Serial.println("[zeit] Zeitabgleich gestartet");
}

}  // namespace

void setup() {
    Serial.begin(115200);
    // Kurz warten, damit die ersten Meldungen über USB-CDC nicht verloren
    // gehen – der Port meldet sich beim Rechner erst nach dem Start an.
    delay(300);
    Serial.printf("\n%s %s startet\n", cfg::kDeviceName, cfg::kFirmwareVersion);

    settings_store::begin();

    // --- Anzeige ------------------------------------------------------------
    board = new Board();
    if (!board->init()) {
        Serial.println("[panel] Board konnte nicht gestartet werden - Neustart");
        delay(3000);
        ESP.restart();
    }

#if LVGL_PORT_AVOID_TEARING_MODE
    // Gegen Flackern und Reissen: Das Panel bekommt zwei vollstaendige
    // Bildpuffer, zwischen denen umgeschaltet wird, statt Streifen ins
    // laufende Bild zu schieben.
    auto lcd = board->getLCD();
    lcd->configFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
    auto lcdBus = lcd->getBus();
    if (lcdBus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
        // Der Bounce-Puffer liegt im schnellen internen RAM und fuettert das
        // Panel gleichmaessig nach. Ohne ihn reicht die PSRAM-Bandbreite bei
        // 800x480 nicht zuverlaessig, und das Bild verrutscht zeilenweise.
        //
        // 40 Zeilen statt 10: Mit dem kleinen Puffer flackerte es weiterhin
        // gelegentlich - immer dann, wenn gleichzeitig WLAN und TLS arbeiten
        // und der PSRAM-Zugriff sich staut. Zwei Puffer a 800x40x2 Byte
        // belegen zusammen 128 KB internen RAM (von gut 220 KB frei).
        static_cast<BusRGB *>(lcdBus)->configRGB_BounceBufferSize(lcd->getFrameWidth() * 40);
    }
#endif

    if (!board->begin()) {
        // Ohne Anzeige ist das Gerät nutzlos. Neustart statt stiller Fehlfunktion.
        Serial.println("[panel] Board konnte nicht gestartet werden – Neustart");
        delay(3000);
        ESP.restart();
    }

    auto backlight = board->getBacklight();
    if (backlight != nullptr) {
        backlight->setBrightness(settings_store::brightness());
    }

    if (!lvgl_port_init(board->getLCD(), board->getTouch())) {
        Serial.println("[lvgl] Start fehlgeschlagen – Neustart");
        delay(3000);
        ESP.restart();
    }

    tablet_state::begin();
    ui::begin();

    // --- Netzwerk -----------------------------------------------------------
    wifi_manager::begin();

    Serial.printf("[start] freier Speicher: %u Byte intern, %u Byte PSRAM\n",
                  ESP.getFreeHeap(), ESP.getFreePsram());
    Serial.println("[start] Einrichtung per USB: HELP eingeben");
}

void loop() {
    // Einrichtung per USB (Token, Pi-Adresse) – siehe serial_console.h
    serial_console::loop();
    wifi_manager::loop();
    configureTimeOnce();
    tablet_state::loop();
    ui::tick();
    // LVGL selbst läuft in einer eigenen Aufgabe; diese Schleife muss nur
    // regelmäßig drankommen und darf den Prozessor nicht blockieren.
    delay(5);
}
