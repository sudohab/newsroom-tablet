#include "tablet_data.h"

#include <ArduinoJson.h>

#include "api_client.h"

namespace tablet_data {
namespace {

// Text aus dem JSON übernehmen und begrenzen. Der Pi ist vertrauenswürdig,
// aber die Anzeige soll auch bei unerwartet langen Werten nicht den Speicher
// füllen.
String take(JsonVariantConst value, size_t maxLen = 120) {
    if (!value.is<const char *>()) return String();
    String text = value.as<const char *>();
    if (text.length() > maxLen) text.remove(maxLen);
    return text;
}

// Eine Liste holen und auswerten. `parse` bekommt das geprüfte JSON.
// Spart in jeder Abruffunktion dieselben zehn Zeilen Fehlerbehandlung.
String fetchList(const char *path, const char *whatFailed,
                 const JsonDocument &filter,
                 void (*parse)(JsonDocument &doc, void *out), void *out) {
    const api_client::Result result = api_client::get(path);
    if (!result.ok) return result.error.isEmpty() ? String(whatFailed) : result.error;

    JsonDocument doc;
    if (deserializeJson(doc, result.body, DeserializationOption::Filter(filter))) {
        return "Antwort nicht lesbar";
    }
    parse(doc, out);
    return String();
}

// Kennungen aus Antworten des Pi, bevor sie in eine Anfrage zurückgehen.
bool looksLikeId(const String &id) {
    if (id.isEmpty() || id.length() > 64) return false;
    for (size_t i = 0; i < id.length(); ++i) {
        const char c = id[i];
        if (!isHexadecimalDigit(c) && c != '-') return false;
    }
    return true;
}

String quoted(const String &value) { return "\"" + value + "\""; }

}  // namespace

// --- Wetter -----------------------------------------------------------------

String fetchWeather(Weather &weather) {
    weather = Weather();
    const api_client::Result result = api_client::get("/api/tablet/weather");
    if (!result.ok) return result.error.isEmpty() ? String("Wetter nicht abrufbar") : result.error;

    JsonDocument doc;
    if (deserializeJson(doc, result.body)) return "Antwort nicht lesbar";
    JsonObjectConst w = doc["weather"];
    if (w.isNull()) return "Keine Wetterdaten";

    weather.valid = true;
    weather.temp = w["temp"] | 0.0f;
    weather.wmo = w["wmo"] | -1;
    weather.text = take(w["text"], 60);
    weather.tempMin = w["temp_min"] | 0;
    weather.tempMax = w["temp_max"] | 0;
    weather.rainProbability = w["rain_probability"] | -1;
    weather.wind = w["wind"] | -1;
    weather.humidity = w["humidity"] | -1;
    weather.dayText = take(w["day_text"], 60);
    return String();
}

// --- Termine ----------------------------------------------------------------

String fetchCalendar(std::vector<Event> &events, Month &month) {
    events.clear();
    month = Month();

    JsonDocument filter;
    filter["events"][0]["title"] = true;
    filter["events"][0]["location"] = true;
    filter["events"][0]["when"] = true;
    filter["events"][0]["date"] = true;
    filter["events"][0]["time"] = true;
    filter["events"][0]["end"] = true;
    filter["month"] = true;

    struct Out { std::vector<Event> *events; Month *month; } out{&events, &month};
    return fetchList("/api/tablet/calendar", "Termine nicht abrufbar", filter,
                     [](JsonDocument &doc, void *raw) {
        auto *o = static_cast<Out *>(raw);
        for (JsonObjectConst entry : doc["events"].as<JsonArrayConst>()) {
            Event event;
            event.title = take(entry["title"], 80);
            event.location = take(entry["location"], 60);
            event.when = take(entry["when"], 20);
            event.date = take(entry["date"], 10);
            event.time = take(entry["time"], 5);
            event.end = take(entry["end"], 5);
            if (!event.title.isEmpty()) o->events->push_back(event);
            if (o->events->size() >= 30) break;
        }
        JsonObjectConst m = doc["month"];
        o->month->year = m["year"] | 0;
        o->month->month = m["month"] | 0;
        o->month->today = m["today"] | 0;
        for (JsonVariantConst day : m["days_with_events"].as<JsonArrayConst>()) {
            const int value = day.as<int>();
            if (value >= 1 && value <= 31) o->month->daysWithEvents.push_back(value);
        }
    }, &out);
}

// --- Nachrichten ------------------------------------------------------------

String fetchNews(std::vector<Headline> &news) {
    news.clear();
    JsonDocument filter;
    filter["news"][0]["source"] = true;
    filter["news"][0]["title"] = true;

    return fetchList("/api/tablet/news", "Nachrichten nicht abrufbar", filter,
                     [](JsonDocument &doc, void *raw) {
        auto *out = static_cast<std::vector<Headline> *>(raw);
        for (JsonObjectConst entry : doc["news"].as<JsonArrayConst>()) {
            Headline headline;
            headline.source = take(entry["source"], 40);
            headline.title = take(entry["title"], 140);
            if (!headline.title.isEmpty()) out->push_back(headline);
            if (out->size() >= 40) break;
        }
    }, &news);
}

// --- Warnungen --------------------------------------------------------------

String fetchWarnings(std::vector<Warning> &warnings) {
    warnings.clear();
    JsonDocument filter;
    filter["warnings"][0]["headline"] = true;
    filter["warnings"][0]["event"] = true;
    filter["warnings"][0]["severity"] = true;

    return fetchList("/api/tablet/warnings", "Warnungen nicht abrufbar", filter,
                     [](JsonDocument &doc, void *raw) {
        auto *out = static_cast<std::vector<Warning> *>(raw);
        for (JsonObjectConst entry : doc["warnings"].as<JsonArrayConst>()) {
            Warning warning;
            warning.headline = take(entry["headline"], 160);
            warning.event = take(entry["event"], 80);
            warning.severity = take(entry["severity"], 20);
            if (!warning.headline.isEmpty()) out->push_back(warning);
            if (out->size() >= 20) break;
        }
    }, &warnings);
}

// --- Anrufe -----------------------------------------------------------------

String fetchCalls(std::vector<Call> &calls) {
    calls.clear();
    JsonDocument filter;
    filter["calls"][0]["name"] = true;
    filter["calls"][0]["number"] = true;
    filter["calls"][0]["time"] = true;

    return fetchList("/api/tablet/calls", "Anrufliste nicht abrufbar", filter,
                     [](JsonDocument &doc, void *raw) {
        auto *out = static_cast<std::vector<Call> *>(raw);
        for (JsonObjectConst entry : doc["calls"].as<JsonArrayConst>()) {
            Call call;
            call.name = take(entry["name"], 60);
            call.number = take(entry["number"], 32);
            call.time = take(entry["time"], 19);
            if (!call.number.isEmpty() || !call.name.isEmpty()) out->push_back(call);
            if (out->size() >= 20) break;
        }
    }, &calls);
}

// --- Podcasts ---------------------------------------------------------------

String fetchPodcasts(std::vector<Podcast> &podcasts, bool &quietTime) {
    podcasts.clear();
    quietTime = false;
    const api_client::Result result = api_client::get("/api/tablet/podcasts");
    if (!result.ok) {
        return result.error.isEmpty() ? String("Podcasts nicht abrufbar") : result.error;
    }

    JsonDocument filter;
    filter["quiet_time"] = true;
    filter["subscriptions"][0]["id"] = true;
    filter["subscriptions"][0]["title"] = true;
    filter["subscriptions"][0]["author"] = true;
    filter["subscriptions"][0]["episode_count"] = true;

    JsonDocument doc;
    if (deserializeJson(doc, result.body, DeserializationOption::Filter(filter))) {
        return "Antwort nicht lesbar";
    }
    quietTime = doc["quiet_time"] | false;
    for (JsonObjectConst entry : doc["subscriptions"].as<JsonArrayConst>()) {
        Podcast podcast;
        podcast.id = take(entry["id"], 64);
        podcast.title = take(entry["title"], 60);
        podcast.author = take(entry["author"], 60);
        podcast.episodeCount = entry["episode_count"] | 0;
        if (!podcast.id.isEmpty()) podcasts.push_back(podcast);
        // Mehr Abos zeigt die Liste nicht; der Pi laesst ohnehin nur wenige zu.
        if (podcasts.size() >= 20) break;
    }
    return String();
}

String fetchEpisodes(const String &podcastId, std::vector<Episode> &episodes) {
    episodes.clear();
    // Die Kennung kam gerade vom Pi, geht aber gleich als Teil des Pfades
    // zurueck. Deshalb hier pruefen: So kann aus einer unerwarteten Antwort
    // kein veraenderter Pfad werden.
    if (!looksLikeId(podcastId)) return "Unbekannter Podcast";

    const String path = "/api/tablet/podcasts/" + podcastId + "/episodes";
    const api_client::Result result = api_client::get(path.c_str());
    if (!result.ok) {
        return result.error.isEmpty() ? String("Folgen nicht abrufbar") : result.error;
    }

    JsonDocument filter;
    JsonObject wanted = filter["episodes"].add<JsonObject>();
    wanted["id"] = true;
    wanted["title"] = true;
    wanted["published_ts"] = true;
    wanted["duration"] = true;
    wanted["played"] = true;
    wanted["position"] = true;
    wanted["video"] = true;

    JsonDocument doc;
    if (deserializeJson(doc, result.body, DeserializationOption::Filter(filter))) {
        return "Antwort nicht lesbar";
    }
    for (JsonObjectConst entry : doc["episodes"].as<JsonArrayConst>()) {
        Episode episode;
        episode.id = take(entry["id"], 64);
        episode.title = take(entry["title"], 90);
        episode.publishedTs = entry["published_ts"] | 0L;
        episode.duration = entry["duration"] | 0;
        episode.played = entry["played"] | false;
        episode.position = entry["position"] | 0;
        episode.video = entry["video"] | false;
        if (!episode.id.isEmpty()) episodes.push_back(episode);
        // Die Liste ist zum Antippen da, nicht zum Durchblaettern eines
        // Archivs. 40 Folgen sind rund zwei Bildschirmhoehen Scrollweg.
        if (episodes.size() >= 40) break;
    }
    return String();
}

String playEpisode(const String &podcastId, const String &episodeId, bool fromStart) {
    if (!looksLikeId(podcastId)) return "Unbekannter Podcast";
    if (!looksLikeId(episodeId)) return "Unbekannte Folge";
    const String json = "{\"action\":\"podcast_play\",\"podcast_id\":" + quoted(podcastId)
                      + ",\"episode_id\":" + quoted(episodeId)
                      + ",\"from_start\":" + (fromStart ? "true" : "false") + "}";
    const api_client::Result result = api_client::postJson("/api/tablet/action", json);
    return result.ok ? String()
                     : (result.error.isEmpty() ? String("Abspielen fehlgeschlagen") : result.error);
}

String markEpisodePlayed(const String &episodeId, bool played) {
    if (!looksLikeId(episodeId)) return "Unbekannte Folge";
    const String json = "{\"action\":\"podcast_played\",\"episode_id\":" + quoted(episodeId)
                      + ",\"played\":" + (played ? "true" : "false") + "}";
    const api_client::Result result = api_client::postJson("/api/tablet/action", json);
    return result.ok ? String()
                     : (result.error.isEmpty() ? String("Markieren fehlgeschlagen") : result.error);
}

// --- Wecker -----------------------------------------------------------------

String fetchAlarms(std::vector<Alarm> &alarms) {
    alarms.clear();
    JsonDocument filter;
    filter["alarms"][0]["id"] = true;
    filter["alarms"][0]["time"] = true;
    filter["alarms"][0]["days"] = true;
    filter["alarms"][0]["enabled"] = true;
    filter["alarms"][0]["label"] = true;

    return fetchList("/api/tablet/alarms", "Weckzeiten nicht abrufbar", filter,
                     [](JsonDocument &doc, void *raw) {
        auto *out = static_cast<std::vector<Alarm> *>(raw);
        for (JsonObjectConst entry : doc["alarms"].as<JsonArrayConst>()) {
            Alarm alarm;
            alarm.id = take(entry["id"], 64);
            alarm.time = take(entry["time"], 5);
            alarm.enabled = entry["enabled"] | false;
            alarm.label = take(entry["label"], 40);
            for (JsonVariantConst day : entry["days"].as<JsonArrayConst>()) {
                const int value = day.as<int>();
                if (value >= 0 && value <= 6) alarm.days[value] = true;
            }
            if (!alarm.time.isEmpty()) out->push_back(alarm);
            if (out->size() >= 10) break;
        }
    }, &alarms);
}

String saveAlarm(const String &id, const String &time, const bool days[7], bool enabled) {
    // Zeit prüfen, bevor sie in den JSON-Körper geht: Der Server weist
    // Unsinn zwar ebenfalls ab, aber ein Gerät soll gar nicht erst
    // Unsinn schicken.
    if (time.length() != 5 || time[2] != ':'
            || !isDigit(time[0]) || !isDigit(time[1])
            || !isDigit(time[3]) || !isDigit(time[4])) {
        return "Ungueltige Uhrzeit";
    }
    if (!id.isEmpty() && !looksLikeId(id)) return "Unbekannter Wecker";

    String json = "{\"action\":\"alarm_save\",\"time\":" + quoted(time);
    if (!id.isEmpty()) json += ",\"alarm_id\":" + quoted(id);
    json += ",\"days\":[";
    bool first = true;
    for (int day = 0; day < 7; ++day) {
        if (!days[day]) continue;
        if (!first) json += ",";
        json += String(day);
        first = false;
    }
    json += "],\"enabled\":";
    json += enabled ? "true" : "false";
    json += "}";

    const api_client::Result result = api_client::postJson("/api/tablet/action", json);
    return result.ok ? String()
                     : (result.error.isEmpty() ? String("Speichern fehlgeschlagen") : result.error);
}

String deleteAlarm(const String &id) {
    if (!looksLikeId(id)) return "Unbekannter Wecker";
    const api_client::Result result = api_client::postJson(
        "/api/tablet/action", "{\"action\":\"alarm_delete\",\"alarm_id\":" + quoted(id) + "}");
    return result.ok ? String()
                     : (result.error.isEmpty() ? String("Loeschen fehlgeschlagen") : result.error);
}

String toggleAlarm(const String &id, bool enabled) {
    if (!looksLikeId(id)) return "Unbekannter Wecker";
    const String json = "{\"action\":\"alarm_toggle\",\"alarm_id\":" + quoted(id)
                      + ",\"enabled\":" + (enabled ? "true" : "false") + "}";
    const api_client::Result result = api_client::postJson("/api/tablet/action", json);
    return result.ok ? String()
                     : (result.error.isEmpty() ? String("Umschalten fehlgeschlagen") : result.error);
}

// --- Kurzzeitwecker ---------------------------------------------------------

namespace {

// Die Kennungen des Timer-Moduls sind reine Hexzeichen ohne Bindestrich.
// `looksLikeId` liesse auch Bindestriche zu -- hier genuegt die engere Form.
bool looksLikeTimerId(const String &id) {
    if (id.length() < 8 || id.length() > 32) return false;
    for (size_t i = 0; i < id.length(); ++i) {
        if (!isHexadecimalDigit(id[i])) return false;
    }
    return true;
}

// Alle Timer-Aktionen schicken dasselbe Muster: Aktion plus Kennung.
String timerAction(const char *action, const String &id, const String &extra = String()) {
    if (!looksLikeTimerId(id)) return "Unbekannter Timer";
    String json = "{\"action\":\"" + String(action) + "\",\"timer_id\":" + quoted(id);
    if (!extra.isEmpty()) json += "," + extra;
    json += "}";
    const api_client::Result result = api_client::postJson("/api/tablet/action", json);
    return result.ok ? String()
                     : (result.error.isEmpty() ? String("Timer nicht erreichbar") : result.error);
}

}  // namespace

String fetchTimers(std::vector<Timer> &timers, int &maxTimers) {
    timers.clear();
    maxTimers = 4;
    JsonDocument filter;
    filter["max"] = true;
    filter["timers"][0]["id"] = true;
    filter["timers"][0]["label"] = true;
    filter["timers"][0]["duration"] = true;
    filter["timers"][0]["remaining"] = true;
    filter["timers"][0]["state"] = true;
    filter["timers"][0]["repeat"] = true;

    const api_client::Result result = api_client::get("/api/tablet/timers");
    if (!result.ok) {
        return result.error.isEmpty() ? String("Timer nicht abrufbar") : result.error;
    }
    JsonDocument doc;
    if (deserializeJson(doc, result.body, DeserializationOption::Filter(filter))) {
        return "Antwort nicht lesbar";
    }
    maxTimers = doc["max"] | 4;
    for (JsonObjectConst entry : doc["timers"].as<JsonArrayConst>()) {
        Timer timer;
        timer.id = take(entry["id"], 32);
        timer.label = take(entry["label"], 40);
        timer.duration = entry["duration"] | 0;
        timer.remaining = entry["remaining"] | 0;
        timer.state = take(entry["state"], 10);
        timer.repeat = entry["repeat"] | false;
        if (!timer.id.isEmpty()) timers.push_back(timer);
        if (static_cast<int>(timers.size()) >= maxTimers) break;
    }
    return String();
}

String createTimer(int seconds, const String &label, bool repeat) {
    // Dieselben Grenzen wie im Pi. Das Geraet soll gar nicht erst Unsinn
    // schicken, auch wenn der Server ihn ohnehin abweisen wuerde.
    if (seconds < 10 || seconds > 24 * 3600) return "Dauer ausserhalb des Bereichs";
    String json = "{\"action\":\"timer_create\",\"seconds\":" + String(seconds);
    if (!label.isEmpty()) json += ",\"label\":" + quoted(label);
    json += ",\"repeat\":";
    json += repeat ? "true" : "false";
    json += "}";
    const api_client::Result result = api_client::postJson("/api/tablet/action", json);
    return result.ok ? String()
                     : (result.error.isEmpty() ? String("Anlegen fehlgeschlagen") : result.error);
}

String startTimer(const String &id)  { return timerAction("timer_start", id); }
String pauseTimer(const String &id)  { return timerAction("timer_pause", id); }
String stopTimer(const String &id)   { return timerAction("timer_stop", id); }
String deleteTimer(const String &id) { return timerAction("timer_delete", id); }

String setTimerRepeat(const String &id, bool repeat) {
    return timerAction("timer_repeat", id,
                       String("\"repeat\":") + (repeat ? "true" : "false"));
}

// --- Matrix-Uhren -----------------------------------------------------------

namespace {

// Uhrenkennungen sind Kleinbuchstaben, Ziffern und Bindestrich -- dieselbe
// Zeichenmenge wie im Server. `looksLikeId` passt hier nicht: Das erwartet
// Hexziffern.
bool looksLikeClockId(const String &id) {
    if (id.isEmpty() || id.length() > 32) return false;
    for (size_t i = 0; i < id.length(); ++i) {
        const char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    }
    return true;
}

// Ein Text geht als JSON-Zeichenkette auf die Reise. Anfuehrungszeichen und
// Rueckstriche muessen also maskiert werden, Steuerzeichen fallen weg -- eine
// Laufschrift kann damit ohnehin nichts anfangen.
String jsonString(const String &value) {
    String out = "\"";
    for (size_t i = 0; i < value.length(); ++i) {
        const char c = value[i];
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (static_cast<unsigned char>(c) >= 0x20) out += c;
    }
    out += "\"";
    return out;
}

String sendClockCommand(const String &clockId, const String &payload) {
    const String target = clockId.isEmpty() ? String("all") : clockId;
    if (target != "all" && !looksLikeClockId(target)) return "Unbekannte Uhr";
    const String json = "{\"action\":\"clock_message\",\"clock_id\":" + quoted(target)
                      + "," + payload + "}";
    const api_client::Result result = api_client::postJson("/api/tablet/action", json);
    return result.ok ? String()
                     : (result.error.isEmpty() ? String("Senden fehlgeschlagen") : result.error);
}

}  // namespace

String fetchClocks(std::vector<Clock> &clocks, std::vector<String> &presets) {
    clocks.clear();
    presets.clear();
    JsonDocument filter;
    filter["clocks"][0]["id"] = true;
    filter["clocks"][0]["name"] = true;
    filter["clocks"][0]["online"] = true;
    filter["presets"] = true;

    const api_client::Result result = api_client::get("/api/tablet/clocks");
    if (!result.ok) {
        return result.error.isEmpty() ? String("Uhren nicht abrufbar") : result.error;
    }
    JsonDocument doc;
    if (deserializeJson(doc, result.body, DeserializationOption::Filter(filter))) {
        return "Antwort nicht lesbar";
    }
    for (JsonObjectConst entry : doc["clocks"].as<JsonArrayConst>()) {
        Clock clock;
        clock.id = take(entry["id"], 32);
        clock.name = take(entry["name"], 40);
        clock.online = entry["online"] | false;
        if (!clock.id.isEmpty()) clocks.push_back(clock);
        if (clocks.size() >= 8) break;
    }
    for (JsonVariantConst entry : doc["presets"].as<JsonArrayConst>()) {
        const String text = take(entry, 80);
        if (!text.isEmpty()) presets.push_back(text);
        if (presets.size() >= 12) break;
    }
    return String();
}

String sendPreset(const String &clockId, int presetIndex) {
    if (presetIndex < 0 || presetIndex > 11) return "Text nicht vorhanden";
    return sendClockCommand(clockId, "\"preset_index\":" + String(presetIndex));
}

String sendClockText(const String &clockId, const String &text) {
    const String trimmed = [&text]() { String t = text; t.trim(); return t; }();
    if (trimmed.isEmpty()) return "Leerer Text";
    if (trimmed.length() > 80) return "Text zu lang";
    return sendClockCommand(clockId, "\"text\":" + jsonString(trimmed));
}

}  // namespace tablet_data
