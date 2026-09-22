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

// Hinweis zur Bilddrift (Bild wandert nach oben oder unten):
//
// Hier stand eine eigene Neusynchronisierung, die nach jedem Bild
// esp_lcd_rgb_panel_restart() aufrief. Sie ist wieder entfernt - aus zwei
// Gruenden, beide am Geraet nachgewiesen:
//
//  1. Der Arduino-Kern hat CONFIG_LCD_RGB_RESTART_IN_VSYNC bereits
//     eingeschaltet (nachgesehen in der sdkconfig der vorgefertigten
//     Bibliotheken). Unsere lief also zusaetzlich, doppelt.
//  2. Mit ihr wanderte das Bild sichtbar staerker als ohne. Espressif fuehrt
//     dieses Neusynchronisieren in seiner Fehlerliste selbst als "nicht als
//     erste Loesung empfohlen".

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

    // Gegen Flackern: Der Bounce-Puffer liegt im schnellen internen RAM und
    // fuettert das Panel vor. Ohne ihn liest das Panel jede Zeile direkt aus
    // dem PSRAM - und wenn dort gleichzeitig gezeichnet oder gefunkt wird,
    // kommen die Daten zu spaet und das Bild flackert.
    //
    // Bewusst nur EIN Bildpuffer (kein Anti-Tearing-Modus): Mehrere Puffer
    // erzeugten mehr PSRAM-Verkehr und machten das Flackern schlimmer.
    auto lcd = board->getLCD();
    auto lcdBus = lcd->getBus();
    if (lcdBus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
        // Pixeltakt 21 MHz statt der 16 MHz aus der Bibliotheksvorlage.
        //
        // Aus dem Takt ergibt sich die Bildwiederholrate: Eine Zeile umfasst
        // 800 sichtbare Pixel plus 20 Austastpixel, ein Bild 480 Zeilen plus
        // 20. Bei 16 MHz sind das 16.000.000 / (820 x 500) = 39 Bilder je
        // Sekunde - unter etwa 50 sieht das Auge das Flimmern. Mit 21 MHz sind
        // es 51 Bilder je Sekunde. 21 MHz ist auch der Wert aus Waveshares
        // eigenem Beispiel fuer dieses Board.
        static_cast<BusRGB *>(lcdBus)->configRGB_FreqHz(21 * 1000 * 1000);

        // 30 Zeilen; die Groesse muss die Bildhoehe glatt teilen
        // (800 x 30 Pixel x 16 = 800 x 480). Zwei solche Puffer belegen
        // zusammen 96 KB internen RAM - mehr vertraegt das Geraet nicht,
        // ohne dass WLAN und TLS zu wenig uebrig bleibt.
        static_cast<BusRGB *>(lcdBus)->configRGB_BounceBufferSize(lcd->getFrameWidth() * 30);
    }

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

    Serial.printf("[start] vor LVGL: %u Byte intern frei\n", ESP.getFreeHeap());
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
