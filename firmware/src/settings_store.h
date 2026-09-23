// Dauerhafte Einstellungen des Tablets im NVS-Bereich des Flash.
//
// Hier liegen die einzigen vertraulichen Werte des Geräts: WLAN-Passwort und
// Geräte-Token. Beide stehen NICHT im Quelltext und nicht in Git (claude.md §1),
// sondern werden auf dem Gerät eingegeben bzw. per USB eingespielt.
//
// Lesen/Schreiben läuft über Arduinos Preferences (Namensraum "tablet").
#pragma once

#include <Arduino.h>

namespace settings_store {

// Obergrenzen, damit aus NVS gelesene Werte niemals Puffer sprengen.
constexpr size_t kMaxSsid = 32;      // WLAN-Standard: max. 32 Zeichen
constexpr size_t kMaxPassword = 63;  // WPA2-PSK: max. 63 Zeichen
constexpr size_t kMaxHost = 63;      // IP oder Hostname des Pi
constexpr size_t kMaxToken = 128;    // Geräte-Token (Base64url, 43 Zeichen)

// Einmalig in setup() aufrufen. Gibt false zurück, wenn der NVS nicht
// geöffnet werden kann (dann läuft das Gerät mit Voreinstellungen weiter).
bool begin();

// --- WLAN -------------------------------------------------------------------
String wifiSsid();
String wifiPassword();
// Speichert den Zugang. Zu lange Werte werden abgewiesen (Rückgabe false),
// nicht abgeschnitten – ein stillschweigend gekürztes Passwort würde später
// als "falsches Passwort" erscheinen und die Fehlersuche unnötig schwer machen.
bool setWifi(const String &ssid, const String &password);
bool hasWifi();
void clearWifi();

// --- Verbindung zum Pi ------------------------------------------------------
// Adresse von newsroom21, z. B. "192.168.178.113". Voreinstellung steht in
// tablet_config.h und lässt sich auf der Einstellungsseite ändern.
String apiHost();
bool setApiHost(const String &host);

// Geräte-Token für /api/tablet/*. Wird ab Etappe 2 verwendet.
String apiToken();
bool setApiToken(const String &token);
bool hasApiToken();
void clearApiToken();

// --- Anzeige ----------------------------------------------------------------
// Eine Helligkeitsregelung gibt es bewusst nicht: Die Hintergrundbeleuchtung
// dieses Boards hängt am IO-Expander CH422G und ist ein reiner Schalter – an
// oder aus. Ein Regler, der nichts regelt, wäre irreführend. Stattdessen wird
// über die Abschaltzeiten gesteuert, wann der Bildschirm dunkel ist.

// Nachtmodus von … bis (volle Stunden, 0..23). Beide gleich = kein Nachtmodus,
// dann gilt überall die Tageszeit.
uint8_t nightStartHour();
uint8_t nightEndHour();
bool setNightHours(uint8_t startHour, uint8_t endHour);

// Nach so vielen Minuten ohne Berührung geht der Bildschirm aus (0 = nie) –
// getrennt für Tag und Nacht, weil man nachts meist schnelle Dunkelheit will
// und tagsüber eine stehende Anzeige.
uint16_t dayOffMinutes();
bool setDayOffMinutes(uint16_t minutes);

uint16_t nightOffMinutes();
bool setNightOffMinutes(uint16_t minutes);

}  // namespace settings_store
