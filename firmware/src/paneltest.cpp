/**
 * Testfirmware: nur das Panel, sonst nichts.
 *
 * Zweck: Flimmern und Bilddrift eindeutig zuordnen. Hier laufen **kein WLAN,
 * kein TLS, kein LVGL und keine Abfragen** – nur ein festes Bild aus
 * senkrechten Farbbalken plus ein waagerechtes Lineal.
 *
 *   • Ist das Bild hier ruhig, liegt die Ursache in unserer Software
 *     (Speicherbedarf, Funkbetrieb, Zeichenlast) – nicht am Panel.
 *   • Flimmert oder wandert es auch hier, liegt es an der Ansteuerung
 *     (Pixeltakt, Austastlücken, Speicherbandbreite).
 *
 * Das Lineal am oberen Rand macht ein Verrutschen sofort sichtbar: Wandert
 * das Bild, steht die farbige Linie nicht mehr oben.
 *
 * Bauen und flashen:
 *   ~/.platformio/penv/bin/pio run -d firmware -e paneltest -t upload
 * Zurück zur richtigen Firmware:
 *   ~/.platformio/penv/bin/pio run -d firmware -t upload
 */

#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <esp_lcd_panel_rgb.h>
#include <esp_heap_caps.h>

using namespace esp_panel::drivers;
using namespace esp_panel::board;

namespace {

constexpr uint16_t kWidth = 800;
constexpr uint16_t kHeight = 480;

// Eine Bildzeile wird einmal gebaut und dann Zeile für Zeile geschrieben.
// Mehr Speicher braucht der Test nicht.
uint16_t *lineBuffer = nullptr;

// Farbe in dem Format, das das Panel erwartet (RGB565).
uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
}

bool onRefreshFinish(void *user_data) {
    // Dieselbe Neusynchronisierung wie in der richtigen Firmware – damit der
    // Test denselben Stand prüft.
    esp_lcd_rgb_panel_restart(static_cast<esp_lcd_panel_handle_t>(user_data));
    return false;
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\nPanel-Test: nur Anzeige, kein WLAN, kein LVGL");

    Board *board = new Board();
    if (!board->init()) {
        Serial.println("Board init fehlgeschlagen");
        return;
    }

    auto lcd = board->getLCD();
    auto lcdBus = lcd->getBus();
    if (lcdBus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
        static_cast<BusRGB *>(lcdBus)->configRGB_FreqHz(21 * 1000 * 1000);
        static_cast<BusRGB *>(lcdBus)->configRGB_BounceBufferSize(kWidth * 30);
    }
    if (!board->begin()) {
        Serial.println("Board begin fehlgeschlagen");
        return;
    }
    lcd->attachRefreshFinishCallback(onRefreshFinish, lcd->getRefreshPanelHandle());

    auto backlight = board->getBacklight();
    if (backlight != nullptr) backlight->setBrightness(100);

    lineBuffer = static_cast<uint16_t *>(heap_caps_malloc(kWidth * sizeof(uint16_t),
                                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (lineBuffer == nullptr) {
        Serial.println("Kein Speicher fuer die Zeile");
        return;
    }

    // Acht senkrechte Farbbalken – gleichmäßige Flächen zeigen Flimmern am
    // deutlichsten, Kanten zeigen ein Verrutschen.
    const uint16_t bars[8] = {
        rgb(255, 255, 255), rgb(255, 255, 0), rgb(0, 255, 255), rgb(0, 255, 0),
        rgb(255, 0, 255),   rgb(255, 0, 0),   rgb(0, 0, 255),   rgb(30, 30, 30),
    };
    for (uint16_t x = 0; x < kWidth; ++x) lineBuffer[x] = bars[(x * 8) / kWidth];

    for (uint16_t y = 0; y < kHeight; ++y) {
        // Die obersten vier Zeilen rot: ein Lineal, das ein Verrutschen des
        // Bildes sofort sichtbar macht.
        if (y < 4) {
            for (uint16_t x = 0; x < kWidth; ++x) lineBuffer[x] = rgb(255, 0, 0);
        } else if (y == 4) {
            for (uint16_t x = 0; x < kWidth; ++x) lineBuffer[x] = bars[(x * 8) / kWidth];
        }
        lcd->drawBitmap(0, y, kWidth, 1, reinterpret_cast<const uint8_t *>(lineBuffer));
    }

    Serial.printf("Testbild steht. Frei: %u Byte intern, %u Byte PSRAM\n",
                  ESP.getFreeHeap(), ESP.getFreePsram());
    Serial.println("Beobachten: flimmert die Flaeche? wandert der rote Streifen?");
}

void loop() {
    // Absichtlich nichts tun: Das Bild bleibt stehen, es wird nicht neu
    // gezeichnet. Alles, was jetzt noch unruhig ist, kommt von der Ansteuerung.
    delay(1000);
}
