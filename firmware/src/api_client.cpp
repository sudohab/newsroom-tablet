#include "api_client.h"

#include <esp_crt_bundle.h>
#include <esp_http_client.h>

#include "root_ca.h"
#include "settings_store.h"
#include "tablet_config.h"
#include "wifi_manager.h"

namespace api_client {
namespace {

// Nimmt die Antwort entgegen – entweder als Text (JSON) oder direkt in einen
// vorgegebenen Puffer (Bilddaten). Zu viel wird verworfen, nie überschrieben.
struct Collector {
    String body;
    bool truncated = false;
    uint8_t *buffer = nullptr;   // nullptr = Text sammeln
    size_t capacity = 0;
    size_t received = 0;
    String etag;                 // Wert der Kopfzeile X-Screen-Hash
};

// Der Sammler der gerade laufenden Anfrage. Die Verbindung wird wiederverwendet
// (siehe unten); ihr fest eingebautes user_data zeigt deshalb immer auf
// dieselbe Stelle und taugt nicht, um die Antwort zuzuordnen.
Collector *activeCollector = nullptr;

esp_err_t onEvent(esp_http_client_event_t *evt) {
    Collector *collector = activeCollector;
    if (collector == nullptr) return ESP_OK;

    // Die Pruefsumme des Bildes kommt als Kopfzeile. Sie wird beim naechsten
    // Abruf zurueckgeschickt, damit der Pi unveraenderte Bilder nicht erneut
    // uebertraegt.
    if (evt->event_id == HTTP_EVENT_ON_HEADER) {
        if (evt->header_key != nullptr && evt->header_value != nullptr
                && strcasecmp(evt->header_key, "X-Screen-Hash") == 0) {
            String value = evt->header_value;
            if (value.length() <= 64) collector->etag = value;
        }
        return ESP_OK;
    }
    if (evt->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;

    if (collector->buffer != nullptr) {
        if (collector->received + evt->data_len > collector->capacity) {
            // Mehr Daten als erwartet: abbrechen statt über den Puffer hinaus
            // zu schreiben. Das Bild wird dann verworfen.
            collector->truncated = true;
            return ESP_OK;
        }
        memcpy(collector->buffer + collector->received, evt->data, evt->data_len);
        collector->received += evt->data_len;
        return ESP_OK;
    }

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

// Die offen gehaltene Verbindung zum Pi und die Adresse, für die sie gilt.
//
// Warum offen halten? Jeder Neuaufbau kostet einen kompletten
// TLS-Handschlag – am Gerät gemessen rund eine Sekunde, in der zugleich der
// Speicherbus belastet wird, an dem auch das Display hängt. Bei einer Abfrage
// alle zwei Sekunden war das die halbe Zeit. Mit einer bestehenden Verbindung
// dauert dieselbe Abfrage einen Bruchteil davon.
esp_http_client_handle_t sharedClient = nullptr;
String sharedHost;

void closeClient() {
    if (sharedClient != nullptr) {
        esp_http_client_cleanup(sharedClient);
        sharedClient = nullptr;
        sharedHost = "";
    }
}

Result request(const String &url, esp_http_client_method_t method, const String &body,
               bool withToken, uint8_t *buffer = nullptr, size_t capacity = 0,
               size_t *received = nullptr, const String *statusHeader = nullptr) {
    Result result;

    if (wifi_manager::state() != wifi_manager::State::Connected) {
        // Ohne WLAN ist auch die offene Verbindung wertlos.
        closeClient();
        result.error = "Kein WLAN";
        return result;
    }

    Collector collector;
    collector.buffer = buffer;
    collector.capacity = capacity;

    const String host = settings_store::apiHost();
    if (sharedClient != nullptr && sharedHost != host) {
        // Adresse in den Einstellungen geändert: alte Verbindung verwerfen.
        closeClient();
    }

    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = method;
    config.keep_alive_enable = true;
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

    if (sharedClient == nullptr) {
        sharedClient = esp_http_client_init(&config);
        sharedHost = host;
    }
    esp_http_client_handle_t client = sharedClient;
    if (client == nullptr) {
        result.error = "Verbindung nicht moeglich";
        return result;
    }
    // Bei einer wiederverwendeten Verbindung müssen Ziel und Verfahren neu
    // gesetzt werden – die Verbindung selbst bleibt bestehen.
    esp_http_client_set_url(client, url.c_str());
    esp_http_client_set_method(client, method);
    activeCollector = &collector;

    String token;
    if (withToken) {
        token = settings_store::apiToken();
        if (token.isEmpty()) {
            activeCollector = nullptr;
            result.error = "Kein Geraete-Token hinterlegt";
            return result;
        }
        const String header = "Bearer " + token;
        esp_http_client_set_header(client, "Authorization", header.c_str());
    }
    esp_http_client_set_header(client, "Accept", "application/json");
    // Kopfzeilen bleiben an einer wiederverwendeten Verbindung hängen – den
    // Zustand deshalb bei jeder anderen Anfrage ausdrücklich entfernen.
    if (statusHeader != nullptr && statusHeader->length() <= 200) {
        esp_http_client_set_header(client, "X-Tablet-Status", statusHeader->c_str());
    } else {
        esp_http_client_delete_header(client, "X-Tablet-Status");
    }
    if (!body.isEmpty()) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body.c_str(), body.length());
    } else {
        // Sonst hinge der Körper der vorigen Anfrage noch an der Verbindung.
        esp_http_client_set_post_field(client, nullptr, 0);
    }

    const esp_err_t err = esp_http_client_perform(client);
    // Auch bei einem Fehlerrückgabewert kann eine gültige HTTP-Antwort
    // vorliegen: Bei 401 versucht esp_http_client selbst, sich anzumelden,
    // findet kein unterstütztes Verfahren (wir benutzen einen Bearer-Token)
    // und meldet ESP_ERR_NOT_SUPPORTED. Der Statuscode steht trotzdem bereit –
    // ohne diese Zeile stünde am Bildschirm eine nichtssagende Fehlermeldung
    // statt "Token abgelehnt".
    const int status = esp_http_client_get_status_code(client);
    if (err == ESP_OK || status > 0) {
        result.status = status;
        result.body = collector.body;
        if (received != nullptr) *received = collector.received;
        result.etag = collector.etag;
        result.ok = result.status >= 200 && result.status < 300;
        // Ein abgeschnittenes Bild ist unbrauchbar – lieber als Fehler melden
        // als eine halbe Anzeige zu zeichnen.
        if (result.ok && collector.truncated) {
            result.ok = false;
            result.error = "Antwort zu gross";
        }
        // 304 heisst "unveraendert" und ist ein Erfolg, kein Fehler - es
        // waere irrefuehrend, das als Stoerung ins Log zu schreiben.
        if (!result.ok && result.status != 304) {
            // Die Antwort des Servers gekuerzt ins Log. Der Text ist fuer
            // Menschen gedacht ("Sitzung abgelaufen", "Sender nicht
            // gefunden") und enthaelt nie Zugangsdaten - ohne ihn raet man
            // bei einer Stoerung nur herum.
            String excerpt = collector.body;
            if (excerpt.length() > 160) excerpt.remove(160);
            Serial.printf("[api] Status %d: %s\n", result.status, excerpt.c_str());
            // Klartext für die Anzeige. Der Server liefert absichtlich keine
            // Einzelheiten, also übersetzen wir die üblichen Fälle selbst.
            if (result.status == 304) result.error = "";  // unveraendert, kein Fehler
            else if (result.status == 401) result.error = "Token abgelehnt";
            else if (result.status == 429) result.error = "Zu viele Anfragen";
            else result.error = "Server meldet " + String(result.status);
        }
    } else {
        // esp_err_to_name liefert technische Kürzel wie ESP_ERR_ESP_TLS_...
        // Für die Anzeige reicht eine verständliche Meldung; die Einzelheit
        // steht nur im seriellen Log.
        Serial.printf("[api] Fehler: %s\n", esp_err_to_name(err));
        if (err == ESP_ERR_HTTP_CONNECT)      result.error = "Pi nicht erreichbar";
        else if (err == ESP_ERR_HTTP_EAGAIN)  result.error = "Pi antwortet nicht rechtzeitig";
        else                                  result.error = "Verbindung fehlgeschlagen";
    }

    activeCollector = nullptr;
    // Nach einem echten Verbindungsfehler ist die offene Verbindung nicht mehr
    // zu gebrauchen – beim nächsten Versuch wird sie neu aufgebaut. Nach einer
    // gültigen HTTP-Antwort bleibt sie bestehen.
    if (err != ESP_OK && status <= 0) closeClient();
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

Result getWithStatus(const String &path, const String &status) {
    Result result;
    if (!checkPath(path)) {
        result.error = "Ungueltiger Pfad";
        return result;
    }
    return request(buildUrl(path), HTTP_METHOD_GET, "", true, nullptr, 0, nullptr, &status);
}

void closeConnection() { closeClient(); }

Result getBinary(const String &path, const String &query,
                 uint8_t *buffer, size_t capacity, size_t &received) {
    received = 0;
    Result result;
    if (!checkPath(path)) {
        result.error = "Ungueltiger Pfad";
        return result;
    }
    // Der Anhang wird selbst gebaut, nie aus einer Server-Antwort übernommen.
    for (size_t i = 0; i < query.length(); ++i) {
        const char c = query[i];
        const bool ok = isAlphaNumeric(c) || c == '=' || c == '&' || c == '-' || c == '_';
        if (!ok) {
            result.error = "Ungueltige Abfrage";
            return result;
        }
    }
    String url = buildUrl(path);
    if (!query.isEmpty()) url += "?" + query;
    return request(url, HTTP_METHOD_GET, "", true, buffer, capacity, &received);
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
    const String url = buildUrl(path);
    result = request(url, HTTP_METHOD_POST, json, true);

    // Einmal wiederholen, wenn der Pi abwinkt.
    //
    // Der Pi laesst je Geraet nur eine Aktion in der Sekunde durch -- eine
    // grobe Bremse gegen ein Geraet, das Amok laeuft. Auf einer Seite mit
    // vielen Knoepfen (vier Timer nebeneinander) tippt ein Mensch aber
    // schneller als das, und dann bekaeme er ein "Bitte kurz warten" fuer
    // etwas, das voellig in Ordnung ist.
    //
    // Sicher ist das, weil eine 429 bedeutet: Der Pi hat die Anfrage
    // ABGEWIESEN, es ist also nichts passiert, was sich verdoppeln koennte.
    // Und es hebelt die Bremse nicht aus, sondern wartet sie ab: Bei
    // Dauerfeuer ist auch der zweite Versuch zu frueh, und der zaehlt dann.
    if (result.status == 429) {
        delay(1100);
        result = request(url, HTTP_METHOD_POST, json, true);
    }
    return result;
}

}  // namespace api_client
