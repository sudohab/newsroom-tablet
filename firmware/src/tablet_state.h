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

    // Wetter (für die Kopfzeile auf allen Seiten)
    bool hasWeather = false;
    float temperature = 0;
    String weatherText;
    int weatherWmo = -1;      // Wettercode, daraus wählt das Gerät das Symbol
    int tempMin = 0;
    int tempMax = 0;
    int rainProbability = -1;

    // Die drei Felder rechts auf der Startseite
    int missedCallCount = 0;
    int warningCount = 0;
    String warningHeadline;

    // Anruf (nur während es klingelt)
    String callText;

    // Klingelnder Kurzzeitwecker – Grundlage für das Aufblenden auf JEDER
    // Seite. Die ganze Timerliste steht nicht hier drin, die holt sich die
    // Timer-Seite selbst.
    int timerExpired = 0;         // wie viele klingeln
    String timerId;               // der erste davon, zum Abstellen
    String timerLabel;
    int timerDuration = 0;        // Sekunden, für „Timer über 5 Min"

    // Termine und Nachrichten für die beiden Spalten der Ansicht
    struct Event {
        String title;
        String when;   // "Do 18.9."
        String time;   // "09:30", leer bei ganztägig
    };
    struct Headline {
        String title;
        String source;
    };
    std::vector<Event> events;
    std::vector<Headline> news;

    // Medien
    String mediaState;            // "playing", "stopped", …
    String mediaKind;             // "radio" oder "podcast"
    String mediaTitle;

    // System
    int volume = -1;              // -1 = unbekannt
    int alarmVolume = -1;         // Wecker-Lautstärke (Weckton, Ansage, Weckradio)

    // Einstellungen: ändert sich die Version, holt das Tablet sie neu
    int configVersion = -1;

    // Offener Probier-Befehl aus der Weboberfläche
    int commandId = 0;
    String commandType;

    // Waschmaschine fertig (nur solange sie nicht als erledigt markiert ist)
    bool washerDone = false;
    String washerName;

    // 3D-Drucker: nur während eines Drucks oder solange „fertig" wartet
    // (sonst printerState leer).
    String printerState;          // "printing", "paused", "complete"
    String printerName;
    String printerFile;
    int printerProgress = -1;     // Prozent, -1 = unbekannt
    int printerRemaining = -1;    // Sekunden, -1 = unbekannt
};

// Einmal beim Start aufrufen.
void begin();

// Regelmäßig aus loop() aufrufen. Fragt höchstens alle zwei Sekunden beim Pi
// nach und tut sonst nichts – der Aufruf ist also billig.
void loop();

// Der zuletzt geholte Stand. Bleibt bei Verbindungsverlust stehen (mit
// online = false), damit die Uhr nicht plötzlich leer ist.
const Snapshot &current();

// True, sobald sich seit dem letzten Abruf etwas geändert hat (Uhrzeit,
// Wecker, Wetter, Lautstärke …). Die Oberfläche zieht dann ihre Beschriftungen
// nach.
bool consumeChanged();

// True nur, wenn sich **Termine oder Nachrichten** geändert haben.
//
// Getrennt gezählt, weil die beiden Listen teuer sind: Sie werden dafür
// vollständig neu aufgebaut. Das bei jeder Lautstärkeänderung zu tun, hieße,
// alle zwei Sekunden den halben Bildschirm neu zu zeichnen – sichtbar als
// Unruhe im Bild.
bool consumeListsChanged();

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

// Halt fuer alles, was gerade laeuft -- Radio wie Podcast. Die Podcast-Seite
// braucht das, denn `radio_stop` beendet nur den Sender.
String stopMedia();
String sleepTimer(int minutes);
String cancelSleepTimer();

// Bequeme Kurzformen für die Oberfläche.
String snooze();
String alarmOff();
String volumeUp();
String volumeDown();
// Wecker-Lautstärke 0–100 % (getrennt von Radio/Podcast)
String setAlarmVolume(int percent);

}  // namespace tablet_state
