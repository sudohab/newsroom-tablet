#include "settings_store.h"

#include <Preferences.h>

#include "tablet_config.h"

namespace settings_store {
namespace {

Preferences prefs;
bool ready = false;

// Schlüsselnamen im NVS. Kurz halten: NVS erlaubt höchstens 15 Zeichen.
constexpr char kNsp[] = "tablet";
constexpr char kKeySsid[] = "wifi_ssid";
constexpr char kKeyPass[] = "wifi_pass";
constexpr char kKeyHost[] = "api_host";
constexpr char kKeyToken[] = "api_token";
constexpr char kKeyNightStart[] = "night_start";
constexpr char kKeyNightEnd[] = "night_end";
constexpr char kKeyDayOff[] = "day_off";
constexpr char kKeyNightOff[] = "night_off";

// Liest einen Text und begrenzt ihn auf die erlaubte Länge. Ein manipulierter
// oder beschädigter NVS soll nirgends zu überlangen Werten führen.
String readLimited(const char *key, size_t maxLen, const String &fallback) {
    if (!ready) return fallback;
    // Erst nachsehen, ob der Schlüssel überhaupt existiert: getString schreibt
    // sonst bei jedem Aufruf eine Fehlerzeile ins Log ("NOT_FOUND"), obwohl
    // ein fehlender Wert der Normalfall ist (frisches Gerät).
    if (!prefs.isKey(key)) return fallback;
    String value = prefs.getString(key, fallback);
    if (value.length() > maxLen) return fallback;
    return value;
}

// Prüft, ob ein Text nur druckbare Zeichen enthält. Steuerzeichen in SSID,
// Host oder Token deuten auf Fehleingabe oder Manipulation hin.
bool isPrintable(const String &value) {
    for (size_t i = 0; i < value.length(); ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        if (c < 0x20 || c == 0x7f) return false;
    }
    return true;
}

}  // namespace

bool begin() {
    // false = Lese- und Schreibzugriff
    ready = prefs.begin(kNsp, false);
    if (!ready) {
        Serial.println("[settings] NVS konnte nicht geoeffnet werden");
    }
    return ready;
}

// --- WLAN -------------------------------------------------------------------

String wifiSsid() { return readLimited(kKeySsid, kMaxSsid, ""); }

String wifiPassword() { return readLimited(kKeyPass, kMaxPassword, ""); }

bool setWifi(const String &ssid, const String &password) {
    if (!ready) return false;
    if (ssid.isEmpty() || ssid.length() > kMaxSsid || !isPrintable(ssid)) return false;
    // Offene Netze (leeres Passwort) sind erlaubt, aber unerwünscht – der
    // Aufrufer warnt davor. Zu lange Passwörter werden abgewiesen.
    if (password.length() > kMaxPassword || !isPrintable(password)) return false;
    prefs.putString(kKeySsid, ssid);
    prefs.putString(kKeyPass, password);
    return true;
}

bool hasWifi() { return !wifiSsid().isEmpty(); }

void clearWifi() {
    if (!ready) return;
    prefs.remove(kKeySsid);
    prefs.remove(kKeyPass);
}

// --- Verbindung zum Pi ------------------------------------------------------

String apiHost() { return readLimited(kKeyHost, kMaxHost, cfg::kDefaultApiHost); }

bool setApiHost(const String &host) {
    if (!ready) return false;
    if (host.isEmpty() || host.length() > kMaxHost || !isPrintable(host)) return false;
    // Nur Zeichen, die in IP-Adressen und Hostnamen vorkommen. Damit kann über
    // die Einstellungsseite keine URL mit Pfad, Port-Trick oder Leerzeichen
    // eingeschleust werden, die später in eine Anfrage-URL wandert.
    for (size_t i = 0; i < host.length(); ++i) {
        const char c = host[i];
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                        (c >= 'A' && c <= 'Z') || c == '.' || c == '-';
        if (!ok) return false;
    }
    prefs.putString(kKeyHost, host);
    return true;
}

String apiToken() { return readLimited(kKeyToken, kMaxToken, ""); }

bool setApiToken(const String &token) {
    if (!ready) return false;
    // Der Server erzeugt Tokens als Base64url aus 32 Zufallsbytes (43 Zeichen).
    if (token.length() < 43 || token.length() > kMaxToken) return false;
    for (size_t i = 0; i < token.length(); ++i) {
        const char c = token[i];
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                        (c >= 'A' && c <= 'Z') || c == '-' || c == '_';
        if (!ok) return false;
    }
    prefs.putString(kKeyToken, token);
    return true;
}

bool hasApiToken() { return !apiToken().isEmpty(); }

void clearApiToken() {
    if (!ready) return;
    prefs.remove(kKeyToken);
}

// --- Anzeige ----------------------------------------------------------------

uint8_t nightStartHour() {
    if (!ready || !prefs.isKey(kKeyNightStart)) return cfg::kDefaultNightStart;
    const uint8_t value = prefs.getUChar(kKeyNightStart, cfg::kDefaultNightStart);
    return value <= 23 ? value : cfg::kDefaultNightStart;
}

uint8_t nightEndHour() {
    if (!ready || !prefs.isKey(kKeyNightEnd)) return cfg::kDefaultNightEnd;
    const uint8_t value = prefs.getUChar(kKeyNightEnd, cfg::kDefaultNightEnd);
    return value <= 23 ? value : cfg::kDefaultNightEnd;
}

bool setNightHours(uint8_t startHour, uint8_t endHour) {
    if (!ready || startHour > 23 || endHour > 23) return false;
    prefs.putUChar(kKeyNightStart, startHour);
    prefs.putUChar(kKeyNightEnd, endHour);
    return true;
}

// Beide Abschaltzeiten lesen sich gleich: höchstens ein Tag, 0 heißt „nie".
uint16_t readMinutes(const char *key, uint16_t fallback) {
    if (!ready || !prefs.isKey(key)) return fallback;
    const uint16_t value = prefs.getUShort(key, fallback);
    return value <= 1440 ? value : fallback;
}

uint16_t dayOffMinutes() { return readMinutes(kKeyDayOff, cfg::kDefaultDayOffMinutes); }

bool setDayOffMinutes(uint16_t minutes) {
    if (!ready || minutes > 1440) return false;
    prefs.putUShort(kKeyDayOff, minutes);
    return true;
}

uint16_t nightOffMinutes() { return readMinutes(kKeyNightOff, cfg::kDefaultNightOffMinutes); }

bool setNightOffMinutes(uint16_t minutes) {
    if (!ready || minutes > 1440) return false;
    prefs.putUShort(kKeyNightOff, minutes);
    return true;
}

}  // namespace settings_store
