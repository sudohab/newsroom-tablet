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
// „Unbekannt“ für Temperaturen (dort ist -1 ein gültiger Wert).
constexpr int kNoTemp = -1000;

// Eine Stunde der Vorschau (Seite „Wetter“: Stundenleiste und Kurve).
struct WeatherHour {
    String hour;          // "07"
    int temp = kNoTemp;   // °C
    int rain = -1;        // Regenwahrscheinlichkeit in %, -1 = unbekannt
    int wmo = -1;         // Wettercode
};

// Ein Tag der Vorschau. Der erste heißt „Heute“.
struct WeatherDay {
    String name;          // "Heute", "Di", …
    int max = kNoTemp;
    int min = kNoTemp;
    int rain = -1;
    int wmo = -1;
};

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
    // Ab Firmware 0.8.0 / newsroom21 vom 18.10.2026 – fehlt bei älteren Pi-Fassungen
    int feels = kNoTemp;        // gefühlte Temperatur
    int gusts = -1;             // Böen km/h
    String windDir;             // "W", "SO", …
    String sunrise;             // "07:41"
    String sunset;
    std::vector<WeatherHour> hours;   // höchstens 24, ab der laufenden Stunde
    std::vector<WeatherDay> days;     // höchstens 7, ab heute
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

// --- Matrix-Uhren -----------------------------------------------------------
struct Clock {
    String id;
    String name;
    bool online = false;
};

// Zusaetzliches Ziel "Familie (Signal)": Der Pi schickt den Text in seine fest
// eingestellte Signal-Familiengruppe. Das Geraet erfaehrt nur, DASS es das
// Ziel gibt -- keine Gruppe, keine Mitglieder, keine Rufnummer.
struct SignalTarget {
    bool family = false;   // in der Weboberflaeche freigeschaltet?
    int textMax = 160;     // Zeichen je Nachricht
};

// Uhren und die in der Weboberflaeche gepflegten Texte in einem Zug -- beides
// gehoert auf dieselbe Seite und aendert sich selten.
String fetchClocks(std::vector<Clock> &clocks, std::vector<String> &presets,
                   SignalTarget &signal);

// Einen Text in die Signal-Familiengruppe schicken. Es gibt bewusst keinen
// Empfaenger-Parameter: Wohin die Nachricht geht, entscheidet allein der Pi.
String sendSignalFamily(const String &text);

// Einen der gepflegten Texte schicken. `clockId` leer = an alle Uhren.
String sendPreset(const String &clockId, int presetIndex);

// Einen frei getippten Text schicken.
String sendClockText(const String &clockId, const String &text);

// --- Signal (Seite "Signal") ------------------------------------------------
// Die letzten Nachrichten, die der Pi ueber die Uhren geschickt hat -- zum
// Nachlesen. Der Pi liefert nur Name (aus seiner Freigabeliste), Uhrzeit und
// Text; Kennungen und Rufnummern kommen hier nie an.
struct SignalMessage {
    String when;    // "14:03" oder "09.10. 14:03"
    String label;   // "Oma", "Familie", "Familienterminal"
    String text;
    bool own = false;   // vom Tisch-Display selbst geschickt
};

struct SignalInbox {
    bool enabled = false;   // in der Weboberflaeche eingeschaltet?
    bool canSend = false;   // Ziel "Familie (Signal)" auf der Seite "Uhren" vorhanden?
    long seq = -1;          // aendert sich mit jeder neuen Nachricht
};

String fetchSignal(std::vector<SignalMessage> &messages, SignalInbox &inbox);

// --- Drucker ------------------------------------------------------------------
// 3D-Drucker (Moonraker) und Bürodrucker (IPP) – newsroom21 fragt beide ab,
// das Tablet holt nur das Ergebnis. Nicht eingerichtet = present false.
struct Printer3d {
    bool present = false;
    String name;
    String state;        // offline, standby, printing, paused, complete, cancelled, error
    String file;
    int progress = -1;   // Prozent
    int remaining = -1;  // Sekunden
    int nozzle = -1, nozzleTarget = -1;
    int bed = -1, bedTarget = -1;
};

struct Ink {
    String name;
    uint32_t color = 0x8e8e93;
    int level = -1;      // Prozent, -1 = unbekannt
};

struct OfficePrinter {
    bool present = false;
    String name;
    String state;        // offline, idle, processing, stopped
    std::vector<String> reasons;
    std::vector<Ink> inks;
};

String fetchPrinters(Printer3d &printer3d, OfficePrinter &office);

// --- Kamera -------------------------------------------------------------------
// Die Kamera schickt Bilder nur auf Anforderung an newsroom21; das Tablet
// holt dort die Liste und das zuletzt angekommene Bild (verkleinert, JPEG).
struct Camera {
    String id;
    String name;
    bool online = false;
    bool snapshot = false;   // Einzelbild angefordert, noch nicht da
    bool stream = false;
    int streamSecondsLeft = 0;
    int seq = 0;             // laufende Nummer des letzten Bildes, 0 = keines
    int age = -1;            // Alter in Sekunden
};

String fetchCameras(std::vector<Camera> &cameras);

// Holt das Bild als JPEG in `buffer`. `received` = 0 heißt: unverändert
// (das Tablet hat `haveSeq` schon) oder kein Bild da.
String fetchCameraFrame(const String &cameraId, int haveSeq,
                        uint8_t *buffer, size_t capacity, size_t &received);

String cameraSnapshot(const String &cameraId);
String cameraStream(const String &cameraId, bool on);

// --- Steckdosen (Seite „Smart Home“, ab 0.9.0) --------------------------------
// LEDVANCE/Tuya und FRITZ!DECT – geschaltet wird über newsroom21, das Tablet
// kennt weder Adressen noch Schlüssel. Der Pi liefert nur die Steckdosen, die
// in der Weboberfläche „auf dem Tablet zeigen“ haben (höchstens 45 = drei Seiten).
struct Plug {
    String id;        // „tuya-…“ oder „fritz-…“
    String name;
    String kind;      // „tuya“ oder „fritz“
    bool online = false;
    int on = -1;      // 1 an, 0 aus, -1 unbekannt
    int watts = -1;   // -1 = misst nicht
};

String fetchPlugs(std::vector<Plug> &plugs);
String switchPlug(const String &plugId, bool on);

}  // namespace tablet_data
