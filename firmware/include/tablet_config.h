// Feste Vorgaben des Tablets. Hier stehen ausdrücklich KEINE Zugangsdaten –
// WLAN-Passwort und Geräte-Token liegen ausschließlich im NVS des Geräts
// (siehe src/settings_store.h, claude.md §1).
#pragma once

#include "firmware_info.h"

#include <stdint.h>

namespace cfg {

// Anzeigename in Logs und auf der Startseite
constexpr char kDeviceName[] = "newsroom21 Tisch-Tablet";
constexpr char kFirmwareVersion[] = FIRMWARE_VERSION;   // firmware_info.h

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

// Nachtmodus: von 22 Uhr bis 7 Uhr
constexpr uint8_t kDefaultNightStart = 22;
constexpr uint8_t kDefaultNightEnd = 7;

// Abschaltzeiten in Minuten (0 = nie abschalten).
// Tagsüber bleibt die Anzeige stehen – ein Wecker, der von selbst dunkel wird,
// überrascht. Nachts geht er nach fünf Minuten aus, damit er nicht ins Zimmer
// leuchtet; eine Berührung weckt ihn.
constexpr uint16_t kDefaultDayOffMinutes = 0;
constexpr uint16_t kDefaultNightOffMinutes = 5;

// Zeitverhalten
constexpr uint32_t kWifiConnectTimeoutMs = 20000;  // Verbindungsversuch
constexpr uint32_t kHttpTimeoutMs = 8000;          // je HTTPS-Anfrage
constexpr uint32_t kStatusPollMs = 2000;           // Statusabfrage beim Pi

}  // namespace cfg
