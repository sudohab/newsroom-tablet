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
uint32_t lostSinceMs = 0;      // seit wann die Verbindung weg ist (0 = steht)
std::function<void(const std::vector<Network> &)> scanCallback;
bool scanRunning = false;
// Letzter Abbruchgrund des Funkmoduls – als Klartext für die Anzeige.
String lastReason;

// Übersetzt den Zahlencode, den das Funkmodul beim Trennen meldet, in einen
// Satz, mit dem man etwas anfangen kann. Die Codes stehen in esp_wifi_types.h;
// hier sind die Fälle aufgeführt, die bei einem Heimnetz vorkommen.
String reasonText(uint8_t reason) {
    switch (reason) {
        case WIFI_REASON_AUTH_EXPIRE:
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            return "Passwort abgelehnt";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
            // Klassiker: Das Passwort ist falsch – der Router antwortet dann
            // beim Schlüsseltausch einfach nicht mehr.
            return "Passwort falsch (kein Schluesseltausch)";
        case WIFI_REASON_NO_AP_FOUND:
            return "Netz nicht gefunden (5 GHz? falscher Kanal?)";
        case WIFI_REASON_ASSOC_FAIL:
        case WIFI_REASON_ASSOC_EXPIRE:
        case WIFI_REASON_NOT_ASSOCED:
            return "Router hat die Anmeldung abgelehnt";
        case WIFI_REASON_BEACON_TIMEOUT:
            return "Funkverbindung abgerissen (Signal zu schwach?)";
        case WIFI_REASON_CONNECTION_FAIL:
            return "Verbindungsaufbau fehlgeschlagen";
        default:
            return "Grund " + String(reason);
    }
}

// Ereignisse des Funkmoduls mitschreiben. Ohne diesen Rückruf sieht man nur
// "hat nicht geklappt", nicht aber warum – und genau der Grund entscheidet,
// ob es am Passwort, am Kanal oder am Signal liegt.
void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        const uint8_t reason = info.wifi_sta_disconnected.reason;
        lastReason = reasonText(reason);
        Serial.printf("[wifi] getrennt: %s (Code %u)\n", lastReason.c_str(), reason);
    } else if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
        lastReason = "";
        Serial.printf("[wifi] angemeldet, Kanal %u\n", info.wifi_sta_connected.channel);
    }
}

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
    // WLAN-Puffer in den internen RAM legen, nicht in den PSRAM.
    //
    // Der Arduino-Kern hat CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y: WLAN- und
    // Netzwerkpuffer landen dann im PSRAM - also genau in dem Speicher, aus
    // dem das Panel dauernd sein Bild liest. Jedes Funkpaket nimmt dem Panel
    // Bandbreite weg, und das Bild verrutscht.
    //
    // Am Geraet nachgewiesen: Im Stufentest (src/paneltest.cpp) wandert das
    // Bild, sobald das WLAN eingeschaltet wird - noch bevor eine einzige
    // Abfrage laeuft.
    //
    // useStaticBuffers(true) legt feste Puffer im internen RAM an. Das kostet
    // dort Platz, haelt aber den PSRAM fuer das Panel frei.
    WiFi.useStaticBuffers(true);

    WiFi.mode(WIFI_STA);
    WiFi.onEvent(onWifiEvent);
    // Länderkennung Deutschland: Hier sind die Funkkanäle 1 bis 13 erlaubt.
    // Ohne diese Angabe arbeitet das Funkmodul in einer weltweit sicheren
    // Voreinstellung und tut sich mit den Kanälen 12 und 13 schwer – die eine
    // Fritz!Box durchaus automatisch wählt.
    esp_wifi_set_country_code("DE", true);
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
                // Nicht sofort neu verbinden: Ein kurzer Aussetzer (der Router
                // funkt z. B. gerade einen Kanalwechsel) verschwindet oft von
                // selbst. Ein sofortiger Neuaufbau würde die Verbindung erst
                // recht abreißen lassen und sich endlos wiederholen.
                if (lostSinceMs == 0) {
                    lostSinceMs = millis();
                } else if (millis() - lostSinceMs > 5000) {
                    Serial.println("[wifi] Verbindung verloren");
                    current = State::Failed;
                    lostSinceMs = 0;
                    nextRetryMs = millis() + 2000;
                }
            } else {
                lostSinceMs = 0;
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
        case State::Failed:
            return lastReason.isEmpty() ? "WLAN nicht erreichbar"
                                        : "WLAN: " + lastReason;
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
