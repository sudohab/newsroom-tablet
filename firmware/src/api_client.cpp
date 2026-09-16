#include "api_client.h"

#include <esp_crt_bundle.h>
#include <esp_http_client.h>

#include "root_ca.h"
#include "settings_store.h"
#include "tablet_config.h"
#include "wifi_manager.h"

namespace api_client {
namespace {

// Sammelt die Antwort in einem String und bricht ab, wenn sie zu groß wird.
struct Collector {
    String body;
    bool truncated = false;
};

esp_err_t onEvent(esp_http_client_event_t *evt) {
    if (evt->event_id != HTTP_EVENT_ON_DATA || evt->user_data == nullptr) return ESP_OK;
    auto *collector = static_cast<Collector *>(evt->user_data);
    if (collector->body.length() + evt->data_len > kMaxBody) {
        collector->truncated = true;
        return ESP_OK;  // Rest verwerfen, Verbindung sauber zu Ende führen
    }
    collector->body.concat(static_cast<const char *>(evt->data), evt->data_len);
    return ESP_OK;
}

// Baut die URL aus der gespeicherten Adresse. Der Pfad wird vom Aufrufer
// geprüft (siehe checkPath) – hier wird nichts zusammengesetzt, was von außen
// kommt.
String buildUrl(const String &path) {
    return "https://" + settings_store::apiHost() + ":" + String(cfg::kApiPort) + path;
}

// Nur die Tablet-Schnittstelle ist erlaubt, und nur einfache Pfadzeichen.
// Das verhindert, dass ein Fehler an anderer Stelle (oder ein Wert aus einer
// Server-Antwort) zu einer Anfrage an einen beliebigen Endpunkt führt.
bool checkPath(const String &path) {
    if (!path.startsWith("/api/tablet/") || path.length() > 128) return false;
    for (size_t i = 0; i < path.length(); ++i) {
        const char c = path[i];
        const bool ok = isAlphaNumeric(c) || c == '/' || c == '-' || c == '_' || c == '.';
        if (!ok) return false;
    }
    // ".." könnte zwar der Server ohnehin nicht missverstehen, aber eine
    // Anfrage, die so aussieht, soll das Gerät gar nicht erst stellen.
    return path.indexOf("..") < 0;
}

Result request(const String &url, esp_http_client_method_t method, const String &body,
               bool withToken) {
    Result result;

    if (wifi_manager::state() != wifi_manager::State::Connected) {
        result.error = "Kein WLAN";
        return result;
    }

    Collector collector;

    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = method;
    config.timeout_ms = cfg::kHttpTimeoutMs;
    config.event_handler = onEvent;
    config.user_data = &collector;
    // Die eigene Root-CA – NICHT das öffentliche Zertifikatsbündel.
    config.cert_pem = kNewsroomRootCa;
    config.crt_bundle_attach = nullptr;
    // Einziger abgeschalteter Teil der Prüfung: der Namensabgleich, weil der
    // Pi über seine IP-Adresse angesprochen wird. Die Unterschrift unserer CA
    // wird weiterhin geprüft.
    config.skip_cert_common_name_check = true;
    // Weiterleitungen nicht automatisch folgen: Ein umgeleiteter Aufruf könnte
    // den Token an eine andere Adresse tragen.
    config.disable_auto_redirect = true;
    config.buffer_size = 2048;
    config.buffer_size_tx = 1024;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        result.error = "Verbindung nicht moeglich";
        return result;
    }

    String token;
    if (withToken) {
        token = settings_store::apiToken();
        if (token.isEmpty()) {
            esp_http_client_cleanup(client);
            result.error = "Kein Geraete-Token hinterlegt";
            return result;
        }
        const String header = "Bearer " + token;
        esp_http_client_set_header(client, "Authorization", header.c_str());
    }
    esp_http_client_set_header(client, "Accept", "application/json");
    if (!body.isEmpty()) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body.c_str(), body.length());
    }

    const esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        result.status = esp_http_client_get_status_code(client);
        result.body = collector.body;
        result.ok = result.status >= 200 && result.status < 300;
        if (!result.ok) {
            // Klartext für die Anzeige. Der Server liefert absichtlich keine
            // Einzelheiten, also übersetzen wir die üblichen Fälle selbst.
            if (result.status == 401) result.error = "Token abgelehnt";
            else if (result.status == 429) result.error = "Zu viele Anfragen";
            else result.error = "Server meldet " + String(result.status);
        }
    } else {
        // esp_err_to_name liefert technische Kürzel wie ESP_ERR_ESP_TLS_...
        // Für die Anzeige reicht eine verständliche Meldung; die Einzelheit
        // steht nur im seriellen Log.
        Serial.printf("[api] Fehler: %s\n", esp_err_to_name(err));
        result.error = (err == ESP_ERR_HTTP_CONNECT) ? "Pi nicht erreichbar"
                                                     : "Verbindung fehlgeschlagen";
    }

    esp_http_client_cleanup(client);
    // Der Token soll nicht länger als nötig im Speicher stehen.
    if (!token.isEmpty()) {
        memset(&token[0], 0, token.length());
    }
    return result;
}

}  // namespace

Result ping() {
    // Startseite statt /healthz: Caddy beantwortet /healthz bewusst mit 404
    // (der Endpunkt gehört dem Docker-Healthcheck). Für den Verbindungstest
    // zählt ohnehin nur, dass eine TLS-Verbindung zustande kommt.
    Result result = request(buildUrl("/"), HTTP_METHOD_GET, "", false);
    if (result.status > 0) {
        // 302 (Weiterleitung zum Login) ist hier ein Erfolg: TLS steht.
        result.ok = true;
        result.error = "";
    }
    return result;
}

Result get(const String &path) {
    Result result;
    if (!checkPath(path)) {
        result.error = "Ungueltiger Pfad";
        return result;
    }
    return request(buildUrl(path), HTTP_METHOD_GET, "", true);
}

Result postJson(const String &path, const String &json) {
    Result result;
    if (!checkPath(path)) {
        result.error = "Ungueltiger Pfad";
        return result;
    }
    if (json.length() > 2048) {
        result.error = "Anfrage zu gross";
        return result;
    }
    return request(buildUrl(path), HTTP_METHOD_POST, json, true);
}

}  // namespace api_client
