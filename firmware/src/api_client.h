// HTTPS-Verbindung zu newsroom21 auf dem Pi.
//
// Warum esp_http_client und nicht WiFiClientSecure/HTTPClient?
// Der Pi wird über seine IP-Adresse angesprochen. Die Zertifikatsprüfung
// besteht aus zwei Teilen: (1) Ist das Zertifikat von unserer CA unterschrieben?
// (2) Passt der Name im Zertifikat zur aufgerufenen Adresse? Teil (2) ist bei
// IP-Adressen unzuverlässig. esp_http_client erlaubt es, GENAU diesen
// Namensabgleich abzuschalten und Teil (1) – den wichtigen Teil – zu behalten.
// Ein "setInsecure()", das jede Prüfung abschaltet, kommt nicht in Frage.
#pragma once

#include <Arduino.h>

namespace api_client {

struct Result {
    bool ok = false;        // true: Anfrage durchgekommen UND Status 2xx
    int status = 0;         // HTTP-Status, 0 wenn die Verbindung scheiterte
    String error;           // Klartext für die Anzeige, nie Token-Inhalte
    String body;            // Antworttext (auf kMaxBody begrenzt)
};

// Obergrenze für Antworten. Ein fehlerhafter oder bösartiger Server soll den
// Arbeitsspeicher des Tablets nicht füllen können.
constexpr size_t kMaxBody = 16384;

// Einfacher Verbindungstest: ruft die Startseite des Pi auf. Jeder gültige
// HTTP-Status beweist, dass WLAN, DNS, TLS und die Zertifikatskette stimmen –
// auch eine Weiterleitung zum Login (302).
Result ping();

// GET auf einen Pfad der Tablet-Schnittstelle, mit Geräte-Token.
// Der Pfad muss mit "/api/tablet/" beginnen; alles andere wird abgelehnt,
// damit ein Programmierfehler nicht versehentlich andere Endpunkte anspricht.
Result get(const String &path);

// POST mit JSON-Körper auf die Tablet-Schnittstelle, mit Geräte-Token.
Result postJson(const String &path, const String &json);

}  // namespace api_client
