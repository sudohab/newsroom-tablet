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

// --- Podcasts ---------------------------------------------------------------
struct Podcast {
    String id;
    String title;
    String author;
    int episodeCount = 0;
};

String fetchPodcasts(std::vector<Podcast> &podcasts, bool &quietTime);

struct Episode {
    String id;
    String title;
    long publishedTs = 0;   // Sekunden seit 1970, 0 = unbekannt
    int duration = 0;       // Sekunden, 0 = unbekannt
    bool played = false;
    int position = 0;       // angehoerte Sekunden
    bool video = false;
};

String fetchEpisodes(const String &podcastId, std::vector<Episode> &episodes);

// Folge abspielen. `fromStart` uebergeht einen gemerkten Stand.
String playEpisode(const String &podcastId, const String &episodeId, bool fromStart);

// Folge als gehoert oder ungehoert markieren.
String markEpisodePlayed(const String &episodeId, bool played);

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

// --- Kurzzeitwecker ---------------------------------------------------------
//
// Der Pi schickt die Restzeit in Sekunden und den Zustand. Heruntergezaehlt
// wird hier im Geraet, damit die Anzeige nicht im Sekundentakt fragen muss;
// beim naechsten Abruf zieht sie sich wieder am Pi gerade.
struct Timer {
    String id;
    String label;
    int duration = 0;     // Sekunden
    int remaining = 0;    // Sekunden
    String state;         // "idle", "running", "paused", "expired"
    bool repeat = false;
};

String fetchTimers(std::vector<Timer> &timers, int &maxTimers);

String createTimer(int seconds, const String &label, bool repeat);
String startTimer(const String &id);
String pauseTimer(const String &id);
String stopTimer(const String &id);
String deleteTimer(const String &id);
String setTimerRepeat(const String &id, bool repeat);

}  // namespace tablet_data
