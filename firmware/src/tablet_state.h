// Zustand, den das Tablet bei newsroom21 abholt und anzeigt.
//
// Das Tablet fragt den Pi regelmäßig nach dem aktuellen Stand (Wecker,
// Wetter, Anrufer, Lautstärke) und schickt Bedienwünsche zurück. Gerechnet
// wird hier nichts – die Wahrheit steht immer auf dem Pi.
//
// Die Abfrage läuft aus der Hauptschleife heraus, nicht aus der LVGL-Aufgabe:
// Eine HTTPS-Anfrage darf bis zu acht Sekunden dauern, und so lange soll die
// Anzeige nicht einfrieren.
#pragma once

#include <Arduino.h>

#include <vector>

namespace tablet_state {

struct Snapshot {
    // Verbindung
    bool online = false;          // letzte Abfrage hat geklappt
    String error;                 // Klartext für die Statuszeile

    // Wecker
    bool alarmActive = false;     // klingelt gerade
    bool alarmSnoozed = false;
    String nextAlarm;             // "" = kein Wecker gestellt

    // Wetter
    bool hasWeather = false;
    float temperature = 0;
    String weatherText;

    // Anruf (nur während es klingelt)
    String callText;

    // Medien
    String mediaState;            // "playing", "stopped", …
    String mediaTitle;

    // System
    int volume = -1;              // -1 = unbekannt

    // Einstellungen: ändert sich die Version, holt das Tablet sie neu
    int configVersion = -1;

    // Offener Probier-Befehl aus der Weboberfläche
    int commandId = 0;
    String commandType;
};

// Einmal beim Start aufrufen.
void begin();

// Regelmäßig aus loop() aufrufen. Fragt höchstens alle zwei Sekunden beim Pi
// nach und tut sonst nichts – der Aufruf ist also billig.
void loop();

// Der zuletzt geholte Stand. Bleibt bei Verbindungsverlust stehen (mit
// online = false), damit die Uhr nicht plötzlich leer ist.
const Snapshot &current();

// True, sobald sich seit dem letzten Abruf etwas geändert hat (die
// Oberfläche zeichnet dann neu).
bool consumeChanged();

// Eine Aktion an den Pi schicken (z. B. "snooze"). Blockiert bis zu acht
// Sekunden – nur aus der Hauptschleife aufrufen, nie aus einem LVGL-Rückruf.
// Rückgabe: leerer Text bei Erfolg, sonst die Fehlermeldung für die Anzeige.
String sendAction(const String &json);

// --- Radio ------------------------------------------------------------------

struct Station {
    String id;
    String name;
};

// Holt die Favoritenliste vom Pi. Blockiert bis zu acht Sekunden, also nur aus
// der Hauptschleife aufrufen. Rückgabe: leerer Text bei Erfolg, sonst der
// Fehler für die Anzeige.
String fetchStations(std::vector<Station> &stations);

String playStation(const String &id);
String stopRadio();
String sleepTimer(int minutes);
String cancelSleepTimer();

// Bequeme Kurzformen für die Oberfläche.
String snooze();
String alarmOff();
String volumeUp();
String volumeDown();

}  // namespace tablet_state
