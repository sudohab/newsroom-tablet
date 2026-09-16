// WLAN-Anbindung des Tablets.
//
// Grundsatz: Das Gerät macht KEINEN eigenen Hotspot auf. Ein offener
// Einrichtungs-AP (wie bei den Matrix-Uhren, die kein Display haben) wäre hier
// unnötig – das Tablet hat einen Bildschirm mit Tastatur, die Zugangsdaten
// werden direkt darauf eingegeben (claude.md: sichere Voreinstellung statt
// bequemer offener Schnittstelle).
#pragma once

#include <Arduino.h>
#include <functional>
#include <vector>

namespace wifi_manager {

struct Network {
    String ssid;
    int32_t rssi;     // Signalstärke in dBm
    bool encrypted;   // false = offenes Netz
};

enum class State {
    Idle,          // noch kein Zugang hinterlegt
    Connecting,
    Connected,
    Failed,        // Passwort falsch oder Netz nicht erreichbar
};

// In setup() aufrufen: schaltet das Funkmodul in den Client-Betrieb und
// verbindet, falls im NVS ein Zugang steht.
void begin();

// Regelmäßig aus der Hauptschleife aufrufen: verfolgt den Verbindungsaufbau,
// versucht nach Abbrüchen neu zu verbinden (mit wachsendem Abstand, damit ein
// dauerhaft falsches Passwort nicht das Funkmodul blockiert).
void loop();

State state();
String statusText();      // kurzer Text für die Anzeige
String ipAddress();       // "" solange nicht verbunden
int32_t rssi();           // Signalstärke der aktuellen Verbindung

// Startet einen Suchlauf. Das Ergebnis kommt asynchron an den Rückruf, damit
// die Oberfläche währenddessen bedienbar bleibt.
void startScan(std::function<void(const std::vector<Network> &)> onResult);

// Speichert Zugangsdaten und verbindet damit neu. Rückgabe false, wenn die
// Werte ungültig sind (zu lang, Steuerzeichen).
bool connectWith(const String &ssid, const String &password);

// Löscht den gespeicherten Zugang und trennt die Verbindung.
void forget();

}  // namespace wifi_manager
