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
#include "settings_store.h"
#include "tablet_config.h"
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
    if (!board->init() || !board->begin()) {
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

    ui::begin();

    // --- Netzwerk -----------------------------------------------------------
    wifi_manager::begin();

    Serial.printf("[start] freier Speicher: %u Byte intern, %u Byte PSRAM\n",
                  ESP.getFreeHeap(), ESP.getFreePsram());
}

void loop() {
    wifi_manager::loop();
    configureTimeOnce();
    ui::tick();
    // LVGL selbst läuft in einer eigenen Aufgabe; diese Schleife muss nur
    // regelmäßig drankommen und darf den Prozessor nicht blockieren.
    delay(5);
}
