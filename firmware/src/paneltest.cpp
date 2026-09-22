/**
 * Stufentest: Was macht das Bild unruhig?
 *
 * Das feste Testbild allein steht ruhig (das war der erste Versuch). Diese
 * Fassung schaltet die übrigen Verdächtigen **nacheinander** zu und zeigt die
 * Stufe als **Farbbalken am oberen Rand** an. Dadurch braucht man nicht auf das
 * serielle Log zu schauen, sondern nur auf den Bildschirm:
 *
 *   ROT    (0–15 s)   nur das Bild, kein Funk, keine Abfragen
 *   GELB              WLAN wird eingeschaltet und verbindet
 *   GRÜN              WLAN verbunden, aber keine Abfragen
 *   BLAU  (ab ~45 s)  zusätzlich alle 2 s eine HTTPS-Abfrage beim Pi
 *
 * Das Bild selbst wird in keiner Stufe neu gezeichnet – nur der Balken oben
 * wechselt die Farbe. Wird das Bild also bei einer bestimmten Farbe unruhig,
 * liegt es an dem, was in dieser Stufe dazugekommen ist:
 *
 *   • unruhig ab GELB/GRÜN  → das Funkmodul stört die Bildausgabe
 *   • unruhig erst ab BLAU  → die Abfragen (Verschlüsselung) stören
 *   • durchgehend ruhig     → es liegt an LVGL, also am Zeichnen selbst
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

#include <WiFi.h>

#include "api_client.h"
#include "settings_store.h"
#include "wifi_manager.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

namespace {

constexpr uint16_t kWidth = 800;
constexpr uint16_t kHeight = 480;
constexpr uint16_t kBarHeight = 24;    // Höhe des Stufenbalkens oben

Board *board = nullptr;
LCD *lcd = nullptr;
uint16_t *lineBuffer = nullptr;

// Zeitpunkte der Stufen
// Stufe ROT laeuft lange, damit sich der reine Panelbetrieb in Ruhe beurteilen
// laesst. Danach kommen WLAN und Abfragen dazu - und am Ende wird das WLAN
// wieder ABGESCHALTET. Wird das Bild dann wieder ruhig, ist der Funkbetrieb
// zweifelsfrei die Ursache.
constexpr uint32_t kWifiAtMs = 30000;
constexpr uint32_t kPollAfterConnectedMs = 20000;
constexpr uint32_t kWifiOffAfterPollMs = 25000;

uint32_t connectedSinceMs = 0;
uint32_t lastPollMs = 0;
bool pollingStarted = false;

uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
}

// Zaehlt, wie oft der Rueckruf kommt. Ohne diese Zahl wuesste man nicht, ob
// die Gegenmassnahme gegen die Bilddrift ueberhaupt ausgeloest wird - oder ob
// sie toter Code ist.
volatile uint32_t refreshCount = 0;
volatile uint32_t restartErrors = 0;

bool onRefreshFinish(void *user_data) {
    refreshCount++;
#if PANEL_RESTART_EACH_FRAME
    // Espressif fuehrt dieses Neusynchronisieren in seiner Fehlerliste als
    // "nicht als erste Loesung empfohlen" - es kann selbst stoeren. Deshalb
    // hier abschaltbar, um genau das zu pruefen.
    if (esp_lcd_rgb_panel_restart(static_cast<esp_lcd_panel_handle_t>(user_data)) != ESP_OK) {
        restartErrors++;
    }
#else
    (void)user_data;
#endif
    return false;
}

// Nur den Balken oben umfärben – der Rest des Bildes bleibt unberührt.
void drawBar(uint16_t color) {
    if (lineBuffer == nullptr || lcd == nullptr) return;
    for (uint16_t x = 0; x < kWidth; ++x) lineBuffer[x] = color;
    for (uint16_t y = 0; y < kBarHeight; ++y) {
        lcd->drawBitmap(0, y, kWidth, 1, reinterpret_cast<const uint8_t *>(lineBuffer));
    }
}

void drawTestImage() {
    // Acht senkrechte Farbbalken: gleichmäßige Flächen zeigen Flimmern am
    // deutlichsten, ihre Kanten zeigen ein Verrutschen.
    const uint16_t bars[8] = {
        rgb(255, 255, 255), rgb(255, 255, 0), rgb(0, 255, 255), rgb(0, 255, 0),
        rgb(255, 0, 255),   rgb(255, 0, 0),   rgb(0, 0, 255),   rgb(40, 40, 40),
    };
    for (uint16_t x = 0; x < kWidth; ++x) lineBuffer[x] = bars[(x * 8) / kWidth];
    for (uint16_t y = kBarHeight; y < kHeight; ++y) {
        lcd->drawBitmap(0, y, kWidth, 1, reinterpret_cast<const uint8_t *>(lineBuffer));
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.printf("\nStufentest: %d MHz Pixeltakt, Bounce-Puffer %d Zeilen, "
                  "Neusynchronisierung je Bild: %s\n",
                  PANEL_PCLK_MHZ, PANEL_BOUNCE_LINES,
                  PANEL_RESTART_EACH_FRAME ? "an" : "aus");

    settings_store::begin();

    board = new Board();
    if (!board->init()) {
        Serial.println("Board init fehlgeschlagen");
        return;
    }
    lcd = board->getLCD();
    auto lcdBus = lcd->getBus();
    if (lcdBus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
        // Pixeltakt aus der Bauumgebung (siehe platformio.ini): So laesst sich
        // derselbe Test mit verschiedenen Takten fahren, ohne Code zu aendern.
        static_cast<BusRGB *>(lcdBus)->configRGB_FreqHz(PANEL_PCLK_MHZ * 1000 * 1000);
        static_cast<BusRGB *>(lcdBus)->configRGB_BounceBufferSize(kWidth * PANEL_BOUNCE_LINES);
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

    drawTestImage();
    drawBar(rgb(255, 0, 0));   // Stufe 1: ROT
    Serial.println("Stufe ROT: nur Bild. WLAN folgt in 15 s.");
}

void loop() {
    static bool wifiStarted = false;
    static bool wasConnected = false;

    // Stufe 2: WLAN einschalten
    if (!wifiStarted && millis() > kWifiAtMs) {
        wifiStarted = true;
        drawBar(rgb(255, 200, 0));   // GELB
        Serial.println("Stufe GELB: WLAN wird eingeschaltet");
        wifi_manager::begin();
    }

    if (wifiStarted) {
        wifi_manager::loop();

        // Stufe 3: verbunden, aber noch keine Abfragen
        const bool connected = wifi_manager::state() == wifi_manager::State::Connected;
        if (connected && !wasConnected) {
            wasConnected = true;
            connectedSinceMs = millis();
            drawBar(rgb(0, 220, 80));   // GRÜN
            Serial.println("Stufe GRUEN: WLAN verbunden, keine Abfragen. "
                           "Abfragen folgen in 20 s.");
        }

        // Stufe 4: Abfragen alle zwei Sekunden, wie in der richtigen Firmware
        if (wasConnected && !pollingStarted
                && millis() - connectedSinceMs > kPollAfterConnectedMs) {
            pollingStarted = true;
            drawBar(rgb(0, 120, 255));   // BLAU
            Serial.println("Stufe BLAU: ab jetzt alle 2 s eine HTTPS-Abfrage");
        }
        // Stufe 5: WLAN wieder abschalten – der Gegenbeweis.
        static bool wifiTurnedOff = false;
        static uint32_t pollingSinceMs = 0;
        if (pollingStarted && pollingSinceMs == 0) pollingSinceMs = millis();
        if (pollingStarted && !wifiTurnedOff
                && millis() - pollingSinceMs > kWifiOffAfterPollMs) {
            wifiTurnedOff = true;
            pollingStarted = false;
            drawBar(rgb(190, 100, 255));   // VIOLETT
            Serial.println("Stufe VIOLETT: WLAN wird abgeschaltet. "
                           "Wird das Bild jetzt wieder ruhig?");
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
        }

        if (pollingStarted && millis() - lastPollMs > 2000) {
            lastPollMs = millis();
            const api_client::Result result = api_client::get("/api/tablet/state");
            Serial.printf("[abfrage] Status %d, %u Byte, Fehler: %s, frei: %u\n", result.status, result.body.length(), result.error.c_str(), ESP.getFreeHeap());
        }
    }

    // Alle fuenf Sekunden melden: Wie viele Bilder wurden gezeichnet, und
    // greift die Neusynchronisierung?
    static uint32_t lastReportMs = 0;
    static uint32_t lastCount = 0;
    if (millis() - lastReportMs > 5000) {
        const uint32_t count = refreshCount;
        Serial.printf("[bild] %lu Bilder in 5 s (%.1f je Sekunde), Fehler: %lu\n",
                      (unsigned long)(count - lastCount),
                      (count - lastCount) / 5.0f, (unsigned long)restartErrors);
        lastCount = count;
        lastReportMs = millis();
    }

    delay(5);
}
