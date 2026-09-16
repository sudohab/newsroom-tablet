#include "tablet_state.h"

#include <ArduinoJson.h>

#include "api_client.h"
#include "settings_store.h"
#include "tablet_config.h"
#include "wifi_manager.h"

namespace tablet_state {
namespace {

Snapshot snapshot;
bool changed = false;
uint32_t lastPollMs = 0;
// Nach einem Fehler nicht sofort wieder anklopfen – sonst hängt die Schleife
// bei einem abgeschalteten Pi dauerhaft im Zeitlimit fest.
uint32_t nextPollDelayMs = cfg::kStatusPollMs;

// Nur diese Felder werden aus der Antwort gelesen. Alles andere überspringt
// ArduinoJson, ohne Speicher dafür zu belegen – die Antwort enthält je nach
// Einstellung auch Radio- und Podcastlisten, die die Startseite nicht braucht.
JsonDocument makeFilter() {
    JsonDocument filter;
    filter["alarm"]["active"] = true;
    filter["alarm"]["snoozed"] = true;
    filter["alarm"]["next"] = true;
    filter["weather"]["temp"] = true;
    filter["weather"]["text"] = true;
    filter["call"]["text"] = true;
    filter["media"]["state"] = true;
    filter["media"]["title"] = true;
    filter["system"]["volume"] = true;
    filter["config_version"] = true;
    filter["command"] = true;
    return filter;
}

// Einen Text aus dem JSON übernehmen und dabei begrenzen. Der Pi ist zwar
// vertrauenswürdig, aber eine Anzeige soll auch bei unerwartet langen Werten
// nicht den Speicher füllen.
String take(JsonVariantConst value, size_t maxLen = 120) {
    if (!value.is<const char *>()) return String();
    String text = value.as<const char *>();
    if (text.length() > maxLen) text.remove(maxLen);
    return text;
}

void applyError(const String &message) {
    if (snapshot.online || snapshot.error != message) changed = true;
    snapshot.online = false;
    snapshot.error = message;
}

void parse(const String &body) {
    JsonDocument doc;
    const JsonDocument filter = makeFilter();
    const DeserializationError error =
        deserializeJson(doc, body, DeserializationOption::Filter(filter));
    if (error) {
        applyError("Antwort nicht lesbar");
        return;
    }

    Snapshot next;
    next.online = true;

    JsonObjectConst alarm = doc["alarm"];
    next.alarmActive = alarm["active"] | false;
    next.alarmSnoozed = alarm["snoozed"] | false;
    next.nextAlarm = take(alarm["next"], 40);

    JsonObjectConst weather = doc["weather"];
    if (!weather.isNull() && !weather["temp"].isNull()) {
        next.hasWeather = true;
        next.temperature = weather["temp"] | 0.0f;
        next.weatherText = take(weather["text"], 60);
    }

    next.callText = take(doc["call"]["text"], 80);

    JsonObjectConst media = doc["media"];
    next.mediaState = take(media["state"], 20);
    next.mediaTitle = take(media["title"], 80);

    next.volume = doc["system"]["volume"] | -1;
    next.configVersion = doc["config_version"] | -1;

    JsonObjectConst command = doc["command"];
    if (!command.isNull()) {
        next.commandId = command["id"] | 0;
        next.commandType = take(command["type"], 16);
    }

    // Nur neu zeichnen, wenn sich wirklich etwas geändert hat – sonst flackert
    // die Anzeige im Zwei-Sekunden-Takt.
    const bool same = next.online == snapshot.online
        && next.alarmActive == snapshot.alarmActive
        && next.alarmSnoozed == snapshot.alarmSnoozed
        && next.nextAlarm == snapshot.nextAlarm
        && next.hasWeather == snapshot.hasWeather
        && next.temperature == snapshot.temperature
        && next.weatherText == snapshot.weatherText
        && next.callText == snapshot.callText
        && next.mediaState == snapshot.mediaState
        && next.mediaTitle == snapshot.mediaTitle
        && next.volume == snapshot.volume
        && next.configVersion == snapshot.configVersion
        && next.commandId == snapshot.commandId;
    snapshot = next;
    if (!same) changed = true;
}

}  // namespace

void begin() {
    snapshot = Snapshot();
    snapshot.error = "Noch keine Verbindung";
}

void loop() {
    if (millis() - lastPollMs < nextPollDelayMs) return;
    lastPollMs = millis();

    if (wifi_manager::state() != wifi_manager::State::Connected) {
        applyError("Kein WLAN");
        nextPollDelayMs = cfg::kStatusPollMs;
        return;
    }
    if (!settings_store::hasApiToken()) {
        applyError("Kein Geraete-Token (per USB einspielen)");
        nextPollDelayMs = 10000;
        return;
    }

    const api_client::Result result = api_client::get("/api/tablet/state");
    if (!result.ok) {
        applyError(result.error);
        // Bei Fehlern langsamer nachfragen, höchstens alle 15 Sekunden.
        nextPollDelayMs = min<uint32_t>(nextPollDelayMs * 2, 15000);
        return;
    }
    nextPollDelayMs = cfg::kStatusPollMs;
    parse(result.body);
}

const Snapshot &current() { return snapshot; }

bool consumeChanged() {
    const bool value = changed;
    changed = false;
    return value;
}

String sendAction(const String &json) {
    const api_client::Result result = api_client::postJson("/api/tablet/action", json);
    if (!result.ok) return result.error.isEmpty() ? String("Aktion fehlgeschlagen") : result.error;
    // Nach einer Aktion sofort neu abfragen, damit die Anzeige stimmt.
    lastPollMs = 0;
    return String();
}

String snooze() { return sendAction("{\"action\":\"snooze\"}"); }
String alarmOff() { return sendAction("{\"action\":\"alarm_off\"}"); }
String volumeUp() { return sendAction("{\"action\":\"volume_up\"}"); }
String volumeDown() { return sendAction("{\"action\":\"volume_down\"}"); }

}  // namespace tablet_state
