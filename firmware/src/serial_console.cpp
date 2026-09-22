#include "serial_console.h"

#include <Arduino.h>

#include "settings_store.h"
#include "tablet_config.h"
#include "wifi_manager.h"

namespace serial_console {
namespace {

// Längste sinnvolle Zeile: Befehlswort + längster Wert (Token) + Reserve.
constexpr size_t kMaxLine = 200;
String line;

void printStatus() {
    Serial.println("--- newsroom-tablet ---");
    Serial.printf("Firmware:   %s\n", cfg::kFirmwareVersion);
    // Der Netzname darf gezeigt werden, das Passwort nicht.
    Serial.printf("WLAN-Netz:  %s\n",
                  settings_store::hasWifi() ? settings_store::wifiSsid().c_str() : "(keines)");
    Serial.printf("WLAN:       %s\n", wifi_manager::statusText().c_str());
    Serial.printf("Pi-Adresse: %s\n", settings_store::apiHost().c_str());
    // Nur ob, nie welcher.
    Serial.printf("Token:      %s\n",
                  settings_store::hasApiToken() ? "hinterlegt" : "(keiner)");
    Serial.printf("Speicher:   %u Byte intern, %u Byte PSRAM frei\n",
                  ESP.getFreeHeap(), ESP.getFreePsram());
    // Temperatur des Chips selbst. Ueber etwa 70 Grad wird es kritisch: Der
    // Baustein arbeitet dann am Rand seiner Spezifikation, und das zeigt sich
    // zuerst als unzuverlaessige Zeitablaeufe - also genau als unruhiges Bild.
    Serial.printf("Chip:       %.1f Grad, Takt %u MHz\n",
                  temperatureRead(), getCpuFrequencyMhz());
    Serial.println("-----------------------");
}

void printHelp() {
    Serial.println("Befehle: STATUS | TOKEN <wert> | HOST <adresse> | "
                   "FORGET-TOKEN | FORGET-WIFI | HELP");
}

// Wert aus dem Speicher überschreiben, sobald er gespeichert ist. Er soll
// nicht als Rest im Arbeitsspeicher stehen bleiben.
void wipe(String &value) {
    for (size_t i = 0; i < value.length(); ++i) value[i] = '\0';
    value = "";
}

void handle(String input) {
    input.trim();
    if (input.isEmpty()) return;

    const int space = input.indexOf(' ');
    String command = space < 0 ? input : input.substring(0, space);
    String value = space < 0 ? String() : input.substring(space + 1);
    command.toUpperCase();
    value.trim();

    if (command == "STATUS") {
        printStatus();
    } else if (command == "HELP" || command == "?") {
        printHelp();
    } else if (command == "TOKEN") {
        // settings_store prüft Länge und erlaubte Zeichen. Der Wert selbst
        // wird nie ausgegeben – auch nicht bei einem Fehler.
        const bool ok = settings_store::setApiToken(value);
        Serial.println(ok ? "OK: Token gespeichert"
                          : "FEHLER: Token ungueltig (43-128 Zeichen, A-Z a-z 0-9 - _)");
        wipe(value);
        wipe(input);
    } else if (command == "HOST") {
        const bool ok = settings_store::setApiHost(value);
        Serial.println(ok ? "OK: Adresse gespeichert"
                          : "FEHLER: Adresse ungueltig (nur Buchstaben, Ziffern, Punkt, Bindestrich)");
    } else if (command == "FORGET-TOKEN") {
        settings_store::clearApiToken();
        Serial.println("OK: Token geloescht");
    } else if (command == "FORGET-WIFI") {
        wifi_manager::forget();
        Serial.println("OK: WLAN-Zugang geloescht");
    } else {
        Serial.println("FEHLER: unbekannter Befehl");
        printHelp();
    }
}

}  // namespace

void loop() {
    while (Serial.available() > 0) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\n' || c == '\r') {
            if (line.length() > 0) {
                String input = line;
                line = "";
                handle(input);
            }
            return;  // höchstens eine Zeile je Durchlauf
        }
        if (line.length() < kMaxLine) {
            line += c;
        } else {
            // Überlange Zeile verwerfen, statt den Speicher volllaufen zu
            // lassen. Der Rest bis zum Zeilenende landet dann im Leeren.
            line = "";
        }
    }
}

}  // namespace serial_console
