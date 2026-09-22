// Daten der einzelnen Seiten vom Pi holen.
//
// Der laufende Zustand (Uhr, Wecker, Wetter, Kacheln) steckt in
// tablet_state.h und wird ständig abgefragt. Hier liegen die Listen, die
// **nur beim Öffnen einer Seite** geholt werden: Termine, Nachrichten,
// Warnungen, Anrufe, Weckzeiten.
//
// Alle Funktionen blockieren bis zu acht Sekunden und dürfen deshalb nur aus
// der Hauptschleife aufgerufen werden, niemals aus einem LVGL-Rückruf.
// Rückgabe ist jeweils ein leerer Text bei Erfolg, sonst die Meldung für die
// Anzeige.
#pragma once

#include <Arduino.h>

#include <vector>

namespace tablet_data {

// --- Wetter -----------------------------------------------------------------
struct Weather {
    bool valid = false;
    float temp = 0;
    int wmo = -1;            // Wettercode, daraus wählt das Gerät das Symbol
    String text;
    int tempMin = 0;
    int tempMax = 0;
    int rainProbability = -1;   // Prozent, -1 = unbekannt
    int wind = -1;
    int humidity = -1;
    String dayText;
};

String fetchWeather(Weather &weather);

// --- Termine ----------------------------------------------------------------
struct Event {
    String title;
    String location;
    String when;     // "Do 18.9."
    String date;     // "2026-09-18"
    String time;     // "09:30", leer bei ganztägig
    String end;
};

struct Month {
    int year = 0;
    int month = 0;
    int today = 0;
    // Tagesnummern mit Terminen – der Pi rechnet das aus, das Gerät malt nur.
    std::vector<int> daysWithEvents;
};

String fetchCalendar(std::vector<Event> &events, Month &month);

// --- Nachrichten ------------------------------------------------------------
struct Headline {
    String source;
    String title;
};

// Die Meldungen kommen bereits reihum über die Quellen sortiert (Quelle 1
// Meldung 1, Quelle 2 Meldung 1, …) – genau in der Reihenfolge, in der die
// Startseite sie durchwechselt.
String fetchNews(std::vector<Headline> &news);

// --- Warnungen --------------------------------------------------------------
struct Warning {
    String headline;
    String event;
    String severity;
};

String fetchWarnings(std::vector<Warning> &warnings);

// --- Anrufe -----------------------------------------------------------------
struct Call {
    String name;
    String number;
    String time;     // "2026-09-22T19:05:00"
};

String fetchCalls(std::vector<Call> &calls);

// --- Wecker -----------------------------------------------------------------
struct Alarm {
    String id;
    String time;        // "06:30"
    bool days[7] = {};  // 0 = Sonntag … 6 = Samstag
    bool enabled = false;
    String label;
};

String fetchAlarms(std::vector<Alarm> &alarms);

// Weckzeit anlegen oder ändern. `id` leer = neuer Wecker.
String saveAlarm(const String &id, const String &time, const bool days[7], bool enabled);
String deleteAlarm(const String &id);
String toggleAlarm(const String &id, bool enabled);

}  // namespace tablet_data
