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
#include "display_control.h"
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
        // Pixeltakt 16 MHz - der Wert aus der Boarddefinition.
        //
        // Zwischenzeitlich standen hier 21 MHz, um das Flimmern durch eine
        // hoehere Bildwiederholrate zu bekaempfen (39 statt 51 Bilder/s). Das
        // eigentliche Flimmern hatte aber eine andere Ursache (Neustart-
        // schleife, spaeter der Flash-Zugriff). Geblieben ist von 21 MHz nur
        // der Nachteil: ein Viertel mehr Datenstrom aus dem PSRAM, jede
        // Sekunde. Das zeigte sich als Streifen am linken Rand und als ein
        // Bild, das ein paar Zeilen nach oben rutscht.
        static_cast<BusRGB *>(lcdBus)->configRGB_FreqHz(16 * 1000 * 1000);

        // 20 Zeilen (800 x 20 Pixel x 24 = 800 x 480): zwei Puffer zu je
        // 32 KB, zusammen 64 KB internen RAM.
        //
        // Der Puffer fuellt den Zeilenanfang vor. Ist er zu klein, laeuft er
        // beim Zeilenwechsel leer - und genau das sieht man als flimmernde
        // Streifen am LINKEN Bildrand. Mit 10 Zeilen trat das auf, sobald die
        // Oberflaeche mehr zu zeichnen hatte.
        //
        // Der Platz dafuer kommt aus den LVGL-Zeichenpuffern (siehe
        // lvgl_v8_port.h, dort von 20 auf 10 Zeilen): Der interne RAM ist mit
        // rund 320 KB knapp, und fuer ein ruhiges Bild ist er beim Panel
        // besser angelegt als beim Zeichnen.
        static_cast<BusRGB *>(lcdBus)->configRGB_BounceBufferSize(lcd->getFrameWidth() * 20);

        // Austastluecken und Bounce-Puffer stehen in unserer eigenen
        // Boarddefinition (include/esp_panel_board_custom_conf.h) - die
        // Bibliothek laesst sie zur Laufzeit nicht aendern.
    }

    if (!board->begin()) {
        // Ohne Anzeige ist das Gerät nutzlos. Neustart statt stiller Fehlfunktion.
        Serial.println("[panel] Board konnte nicht gestartet werden – Neustart");
        delay(3000);
        ESP.restart();
    }

    // Beleuchtung einschalten. Ein Prozentwert waere hier irrefuehrend: Der
    // Schalter am CH422G kennt nur an und aus. Ab wann sie ausgeht, regelt
    // display_control anhand der Abschaltzeiten.
    auto backlight = board->getBacklight();
    if (backlight != nullptr) backlight->setBrightness(100);

    Serial.printf("[start] vor LVGL: %u Byte intern frei\n", ESP.getFreeHeap());
    if (!lvgl_port_init(board->getLCD(), board->getTouch())) {
        Serial.println("[lvgl] Start fehlgeschlagen – Neustart");
        delay(3000);
        ESP.restart();
    }

    tablet_state::begin();
    ui::begin();
    // Nach dem Aufbau der Oberflaeche: Abdunkelung liegt ueber allem.
    display_control::begin(board->getBacklight());

    // --- Netzwerk -----------------------------------------------------------
    wifi_manager::begin();

    Serial.printf("[start] freier Speicher: %u Byte intern, %u Byte PSRAM\n",
                  ESP.getFreeHeap(), ESP.getFreePsram());
    Serial.println("[start] Einrichtung per USB: HELP eingeben");
}

void loop() {
    // Alle 30 Sekunden Temperatur und freien Speicher melden. Klingt nach
    // Kleinkram, ist aber das Gegenmittel gegen Raterei: Wird der Chip heiss
    // oder geht der Speicher zur Neige, sieht man es hier, statt es aus dem
    // Bildverhalten zu erschliessen.
    static uint32_t lastHealthMs = 0;
    if (millis() - lastHealthMs > 30000) {
        lastHealthMs = millis();
        Serial.printf("[zustand] %.1f Grad, %u MHz, %u Byte intern frei\n",
                      temperatureRead(), getCpuFrequencyMhz(), ESP.getFreeHeap());
    }

    // Einrichtung per USB (Token, Pi-Adresse) – siehe serial_console.h
    serial_console::loop();
    display_control::loop();
    wifi_manager::loop();
    configureTimeOnce();
    tablet_state::loop();
    ui::tick();
    // LVGL selbst läuft in einer eigenen Aufgabe; diese Schleife muss nur
    // regelmäßig drankommen und darf den Prozessor nicht blockieren.
    delay(5);
}
