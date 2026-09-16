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
constexpr char kKeyBright[] = "brightness";

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

uint8_t brightness() {
    if (!ready) return cfg::kDefaultBrightness;
    const uint8_t value = prefs.getUChar(kKeyBright, cfg::kDefaultBrightness);
    // Werte außerhalb des Bereichs (etwa aus einer älteren Firmware) würden
    // das Display dunkel schalten – dann lieber die Voreinstellung.
    if (value < cfg::kMinBrightness || value > 100) return cfg::kDefaultBrightness;
    return value;
}

bool setBrightness(uint8_t percent) {
    if (!ready) return false;
    if (percent < cfg::kMinBrightness || percent > 100) return false;
    prefs.putUChar(kKeyBright, percent);
    return true;
}

}  // namespace settings_store
