// Firmware-Updates über das Netz (OTA) – gleiches Verfahren wie beim
// Waschmaschinen-Sensor und den Matrix-Uhren.
//
// Ablauf (angestoßen per „Installieren“ in der newsroom21-Weboberfläche):
//   1. Die Zustandsabfrage bringt das Manifest mit: Modell, Build-Nummer,
//      Größe, SHA-256, Signatur.
//   2. Das Tablet prüft VOR dem Laden: eigenes Modell, höhere Build-Nummer,
//      Signatur (ECDSA P-256) mit dem eingebauten öffentlichen Schlüssel.
//   3. Laden über eine geprüfte HTTPS-Verbindung zu newsroom21, dabei SHA-256
//      mitrechnen und direkt in den freien Programmbereich schreiben.
//   4. Passt die SHA-256: umschalten und neu starten.
//   5. Die neue Firmware gilt erst als gut, wenn sie newsroom21 erreicht hat.
//      Schafft sie das nicht binnen 5 Minuten, kehrt das Tablet zur alten
//      zurück (Rückfall des Bootloaders).
//
// Signiert wird  newsroom21-fw|<modell>|<build>|<größe>|<sha256>  auf dem PC
// (newsroom21/scripts/firmware/publish.py). Wer newsroom21 übernimmt, kann
// Updates verhindern, aber keine fremde Firmware aufspielen.
//
// Läuft in der Hauptschleife auf Kern 0; das Bild auf Kern 1 zeigt währenddessen
// einen ruhigen Hinweis. Der Programmcode liegt im PSRAM – Schreiben in den
// Flash hält die Bildausgabe also nicht an.
#pragma once

#include <Arduino.h>

namespace ota {

struct Offer {
    String model;
    long long build = 0;
    long long size = 0;
    String sha256;
    String signature;
    String label;
};

// Beim Start: Läuft eine neue Firmware noch auf Bewährung?
void begin();

// Nach jeder erfolgreichen Zustandsabfrage: bestätigt eine neue Firmware.
void confirmWorking();

// Aus der Hauptschleife: Rückfall, wenn eine neue Firmware newsroom21 nicht
// binnen 5 Minuten erreicht.
void loop();

// Angebot prüfen und – wenn alles stimmt – laden, umschalten, neu starten.
// Kehrt nur bei einem Fehler zurück (Grund im seriellen Protokoll).
void handleOffer(const Offer &offer);

// true, solange ein Abbild geladen wird.
bool busy();

}  // namespace ota
