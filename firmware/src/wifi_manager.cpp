#include "wifi_manager.h"

#include <WiFi.h>
#include <esp_wifi.h>

#include "settings_store.h"
#include "tablet_config.h"

namespace wifi_manager {
namespace {

State current = State::Idle;
uint32_t connectStartedMs = 0;
uint32_t nextRetryMs = 0;
uint32_t retryDelayMs = 5000;  // wächst bei wiederholtem Fehlschlag
std::function<void(const std::vector<Network> &)> scanCallback;
bool scanRunning = false;

// Verbindungsversuch mit den gespeicherten Daten starten.
void startConnect() {
    const String ssid = settings_store::wifiSsid();
    if (ssid.isEmpty()) {
        current = State::Idle;
        return;
    }
    const String pass = settings_store::wifiPassword();
    // Das Passwort steht nur hier im Arbeitsspeicher und wird nie geloggt
    // (claude.md §5). Im Log erscheint ausschließlich der Netzname.
    Serial.printf("[wifi] verbinde mit \"%s\"\n", ssid.c_str());
    WiFi.begin(ssid.c_str(), pass.c_str());
    current = State::Connecting;
    connectStartedMs = millis();
}

}  // namespace

void begin() {
    WiFi.mode(WIFI_STA);
    // Kein eigener Hotspot, keine Verbindungsdaten im WLAN-Stack speichern:
    // Die Zugangsdaten verwaltet ausschließlich settings_store, damit es nur
    // einen Ort gibt, an dem sie stehen (und gelöscht werden können).
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    // Stromsparmodus aus: Er verzögert Antworten deutlich, und das Tablet
    // hängt ohnehin am Netzteil.
    WiFi.setSleep(false);
    WiFi.setHostname("newsroom-tablet");

    if (settings_store::hasWifi()) {
        startConnect();
    } else {
        current = State::Idle;
        Serial.println("[wifi] kein Zugang hinterlegt – Einrichtung am Bildschirm");
    }
}

void loop() {
    // Suchlauf abgeschlossen? Ergebnis einsammeln und weiterreichen.
    if (scanRunning) {
        const int16_t found = WiFi.scanComplete();
        if (found >= 0) {
            std::vector<Network> networks;
            for (int16_t i = 0; i < found; ++i) {
                Network net;
                net.ssid = WiFi.SSID(i);
                net.rssi = WiFi.RSSI(i);
                net.encrypted = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
                // Netze ohne Namen (versteckte SSID) lassen sich nicht
                // antippen – sie würden die Liste nur verstopfen.
                if (!net.ssid.isEmpty()) networks.push_back(net);
            }
            WiFi.scanDelete();
            scanRunning = false;
            if (scanCallback) scanCallback(networks);
            scanCallback = nullptr;
        } else if (found == WIFI_SCAN_FAILED) {
            scanRunning = false;
            if (scanCallback) scanCallback({});
            scanCallback = nullptr;
        }
    }

    switch (current) {
        case State::Connecting:
            if (WiFi.status() == WL_CONNECTED) {
                current = State::Connected;
                retryDelayMs = 5000;  // nach Erfolg zurücksetzen
                Serial.printf("[wifi] verbunden, IP %s\n", WiFi.localIP().toString().c_str());
            } else if (millis() - connectStartedMs > cfg::kWifiConnectTimeoutMs) {
                current = State::Failed;
                WiFi.disconnect();
                nextRetryMs = millis() + retryDelayMs;
                // Abstand verdoppeln, höchstens 5 Minuten: Ein falsches
                // Passwort soll nicht dauerhaft Funk und Strom verbrauchen.
                retryDelayMs = min<uint32_t>(retryDelayMs * 2, 300000);
                Serial.println("[wifi] Verbindung fehlgeschlagen");
            }
            break;

        case State::Connected:
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("[wifi] Verbindung verloren");
                current = State::Failed;
                nextRetryMs = millis() + 2000;
            }
            break;

        case State::Failed:
            if (millis() >= nextRetryMs && settings_store::hasWifi()) startConnect();
            break;

        case State::Idle:
            break;
    }
}

State state() { return current; }

String statusText() {
    switch (current) {
        case State::Idle:       return "WLAN nicht eingerichtet";
        case State::Connecting: return "WLAN verbindet ...";
        case State::Connected:  return "WLAN verbunden (" + WiFi.localIP().toString() + ")";
        case State::Failed:     return "WLAN nicht erreichbar";
    }
    return "";
}

String ipAddress() {
    return current == State::Connected ? WiFi.localIP().toString() : String();
}

int32_t rssi() { return current == State::Connected ? WiFi.RSSI() : 0; }

void startScan(std::function<void(const std::vector<Network> &)> onResult) {
    if (scanRunning) return;  // laufenden Suchlauf nicht doppelt starten
    scanCallback = std::move(onResult);
    scanRunning = true;
    WiFi.scanDelete();
    WiFi.scanNetworks(true /* asynchron */);
}

bool connectWith(const String &ssid, const String &password) {
    if (!settings_store::setWifi(ssid, password)) return false;
    WiFi.disconnect();
    retryDelayMs = 5000;
    startConnect();
    return true;
}

void forget() {
    settings_store::clearWifi();
    WiFi.disconnect(false, true /* gespeicherte Daten im Stack löschen */);
    current = State::Idle;
}

}  // namespace wifi_manager
