// Feste Vorgaben des Tablets. Hier stehen ausdrücklich KEINE Zugangsdaten –
// WLAN-Passwort und Geräte-Token liegen ausschließlich im NVS des Geräts
// (siehe src/settings_store.h, claude.md §1).
#pragma once

#include <stdint.h>

namespace cfg {

// Anzeigename in Logs und auf der Startseite
constexpr char kDeviceName[] = "newsroom21 Tisch-Tablet";
constexpr char kFirmwareVersion[] = "0.1.0";

// Voreingestellte Adresse von newsroom21 (Pi 5 im Heimnetz). Änderbar auf der
// Einstellungsseite des Geräts; die Vorgabe spart die Ersteinrichtung.
constexpr char kDefaultApiHost[] = "192.168.178.113";
constexpr uint16_t kApiPort = 443;

// Zeitzone Deutschland inklusive Sommerzeitregel (POSIX-Schreibweise).
// So rechnet das Gerät die Umstellung selbst aus und braucht dafür keinen
// Server.
constexpr char kTimezone[] = "CET-1CEST,M3.5.0,M10.5.0/3";
// Zeitserver: bewusst der Router im Heimnetz zuerst. Er ist erreichbar, auch
// wenn das Tablet keinen Weg ins Internet hat.
constexpr char kNtpPrimary[] = "192.168.178.1";
constexpr char kNtpSecondary[] = "de.pool.ntp.org";

// Helligkeit in Prozent
constexpr uint8_t kDefaultBrightness = 80;
constexpr uint8_t kDefaultNightBrightness = 25;   // nachts deutlich dunkler
constexpr uint8_t kDefaultNightStart = 22;        // Nachtmodus von 22 Uhr …
constexpr uint8_t kDefaultNightEnd = 7;           // … bis 7 Uhr
// Bildschirm nach dieser Zeit ohne Berührung abschalten. 0 = nie.
// Voreinstellung 0: Ein Wecker, der von selbst dunkel wird, überrascht sonst.
constexpr uint16_t kDefaultScreenOffMinutes = 0;
constexpr uint8_t kMinBrightness = 10;  // nie ganz dunkel: sonst wirkt das
                                        // Gerät defekt und ist nicht bedienbar

// Zeitverhalten
constexpr uint32_t kWifiConnectTimeoutMs = 20000;  // Verbindungsversuch
constexpr uint32_t kHttpTimeoutMs = 8000;          // je HTTPS-Anfrage
constexpr uint32_t kStatusPollMs = 2000;           // Statusabfrage beim Pi

}  // namespace cfg
