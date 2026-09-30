// Firmware-Updates über das Netz – siehe ota.h
#include "ota.h"

#include <esp_http_client.h>
#include <esp_ota_ops.h>
#include <mbedtls/base64.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>

#include "firmware_info.h"
#include "lvgl_port/lvgl_v8_port.h"
#include "ota_pubkey.h"
#include "root_ca.h"
#include "settings_store.h"
#include "tablet_config.h"

// Die Kernbibliothek fragt beim Start, ob die Firmware selbst entscheidet,
// wann sie als gut gilt. Ja: erst nach der ersten erfolgreichen Abfrage bei
// newsroom21 (confirmWorking). Sonst würde jede neue Firmware sofort als gut
// markiert – und der Rückfall griffe nie.
extern "C" bool verifyRollbackLater() { return true; }

namespace ota {
namespace {

constexpr uint32_t kProbationMs = 5UL * 60UL * 1000UL;
bool onProbation = false;
volatile bool downloading = false;
bool attempted = false;
uint32_t lastAttemptMs = 0;
lv_obj_t *notice = nullptr;

String hex(const unsigned char *data, size_t len) {
    static const char digits[] = "0123456789abcdef";
    String out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out += digits[data[i] >> 4];
        out += digits[data[i] & 0x0f];
    }
    return out;
}

bool validSha(const String &sha) {
    if (sha.length() != 64) return false;
    for (char c : sha)
        if (!isdigit((unsigned char)c) && (c < 'a' || c > 'f')) return false;
    return true;
}

bool verifySignature(const String &message, const String &signatureB64) {
    unsigned char signature[80];
    size_t signatureLen = 0;
    if (mbedtls_base64_decode(signature, sizeof(signature), &signatureLen,
                              reinterpret_cast<const unsigned char *>(signatureB64.c_str()),
                              signatureB64.length()) != 0) {
        return false;
    }
    unsigned char hash[32];
    if (mbedtls_sha256(reinterpret_cast<const unsigned char *>(message.c_str()), message.length(),
                       hash, 0) != 0) {
        return false;
    }
    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
    int rc = mbedtls_pk_parse_public_key(&key, reinterpret_cast<const unsigned char *>(kOtaPublicKey),
                                         strlen(kOtaPublicKey) + 1);
    if (rc == 0) rc = mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, hash, sizeof(hash), signature, signatureLen);
    mbedtls_pk_free(&key);
    return rc == 0;
}

// Ruhiger Hinweis über allem, solange geladen wird – eine Oberfläche, die
// weiter auf Berührungen reagiert, würde hier nur verwirren.
void showNotice(const char *text) {
    lvgl_port_lock(-1);
    if (notice == nullptr) {
        notice = lv_obj_create(lv_layer_top());
        lv_obj_set_size(notice, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(notice, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(notice, LV_OPA_80, 0);
        lv_obj_set_style_border_width(notice, 0, 0);
        lv_obj_t *label = lv_label_create(notice);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(label);
    }
    lv_label_set_text(lv_obj_get_child(notice, 0), text);
    lv_obj_clear_flag(notice, LV_OBJ_FLAG_HIDDEN);
    lvgl_port_unlock();
}

void hideNotice() {
    lvgl_port_lock(-1);
    if (notice != nullptr) lv_obj_add_flag(notice, LV_OBJ_FLAG_HIDDEN);
    lvgl_port_unlock();
}

// Lädt das Abbild in den freien Programmbereich. true = geschrieben, SHA-256
// stimmt, Umschalten vorgemerkt.
bool download(size_t size, const String &sha) {
    const esp_partition_t *target = esp_ota_get_next_update_partition(nullptr);
    if (target == nullptr || size > target->size) {
        Serial.println("[ota] kein passender Programmbereich frei");
        return false;
    }

    const String url = "https://" + settings_store::apiHost() + ":" + String(cfg::kApiPort) +
                       "/api/tablet/firmware";
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 20000;
    config.cert_pem = kNewsroomRootCa;          // nur unsere eigene CA
    config.crt_bundle_attach = nullptr;
    config.skip_cert_common_name_check = true;  // wie api_client: Pi per IP-Adresse
    config.disable_auto_redirect = true;
    config.buffer_size = 4096;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) return false;

    String token = settings_store::apiToken();
    const String header = "Bearer " + token;
    esp_http_client_set_header(client, "Authorization", header.c_str());
    memset(&token[0], 0, token.length());

    bool ok = false;
    esp_ota_handle_t ota = 0;
    bool otaStarted = false;
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    static uint8_t buffer[4096];

    do {
        if (esp_http_client_open(client, 0) != ESP_OK) break;
        const int64_t length = esp_http_client_fetch_headers(client);
        const int status = esp_http_client_get_status_code(client);
        if (status != 200 || length != (int64_t)size) {
            Serial.printf("[ota] Laden abgelehnt: Status %d, Größe %lld statt %u\n", status,
                          (long long)length, (unsigned)size);
            break;
        }
        if (esp_ota_begin(target, size, &ota) != ESP_OK) break;
        otaStarted = true;
        size_t written = 0;
        int lastPercent = -10;
        while (written < size) {
            const int got = esp_http_client_read(client, reinterpret_cast<char *>(buffer),
                                                 min(sizeof(buffer), size - written));
            if (got <= 0) break;
            mbedtls_sha256_update(&ctx, buffer, got);
            if (esp_ota_write(ota, buffer, got) != ESP_OK) break;
            written += got;
            const int percent = (int)(written * 100 / size);
            if (percent / 10 != lastPercent / 10) {
                lastPercent = percent;
                Serial.printf("[ota] %d %%\n", percent);
                char text[48];
                snprintf(text, sizeof(text), "Update wird geladen … %d %%", percent);
                showNotice(text);
            }
        }
        if (written != size) {
            Serial.println("[ota] Laden abgebrochen");
            break;
        }
        unsigned char digest[32];
        mbedtls_sha256_finish(&ctx, digest);
        if (hex(digest, sizeof(digest)) != sha) {
            Serial.println("[ota] SHA-256 stimmt nicht – Abbild verworfen");
            break;
        }
        // esp_ota_end prüft zusätzlich, ob das Abbild ein gültiges
        // ESP32-S3-Programm ist.
        otaStarted = false;
        if (esp_ota_end(ota) != ESP_OK) {
            Serial.println("[ota] Abbild ungültig");
            break;
        }
        if (esp_ota_set_boot_partition(target) != ESP_OK) break;
        ok = true;
    } while (false);

    if (otaStarted) esp_ota_abort(ota);
    mbedtls_sha256_free(&ctx);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

}  // namespace

void begin() {
    esp_ota_img_states_t state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running != nullptr && esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        onProbation = true;
        Serial.println("[ota] neue Firmware auf Bewährung – gilt als gut, sobald newsroom21 antwortet");
    }
}

void confirmWorking() {
    if (!onProbation) return;
    onProbation = false;
    esp_ota_mark_app_valid_cancel_rollback();
    Serial.println("[ota] newsroom21 erreicht – neue Firmware bestätigt");
}

void loop() {
    if (onProbation && millis() > kProbationMs) {
        Serial.println("[ota] newsroom21 seit 5 Minuten nicht erreicht – zurück zur alten Firmware");
        delay(100);
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
}

bool busy() { return downloading; }

void handleOffer(const Offer &offer) {
    // Nicht bei jeder Abfrage erneut versuchen, falls etwas schiefging.
    if (attempted && millis() - lastAttemptMs < 120000UL) return;
    if (onProbation) return;   // erst bestätigen, dann das nächste Update
    attempted = true;
    lastAttemptMs = millis();

    if (offer.model != FIRMWARE_MODEL) {
        Serial.printf("[ota] Angebot für anderes Modell (%s) – ignoriert\n", offer.model.c_str());
        return;
    }
    if (offer.build <= FIRMWARE_BUILD) {
        Serial.println("[ota] Angebot ist nicht neuer – ignoriert");
        return;
    }
    if (offer.size <= 0 || offer.size > 6LL * 1024 * 1024 || !validSha(offer.sha256) ||
        offer.signature.isEmpty() || offer.signature.length() > 200) {
        Serial.println("[ota] Angebot unvollständig oder zu groß – ignoriert");
        return;
    }
    const String message = "newsroom21-fw|" + offer.model + "|" + String(offer.build) + "|" +
                           String(offer.size) + "|" + offer.sha256;
    if (!verifySignature(message, offer.signature)) {
        Serial.println("[ota] SIGNATUR UNGÜLTIG – Update abgelehnt");
        return;
    }
    Serial.printf("[ota] Signatur gültig – lade %s (Build %lld, %lld Bytes)\n",
                  offer.label.c_str(), offer.build, offer.size);
    downloading = true;
    showNotice("Update wird geladen …");
    const bool ok = download((size_t)offer.size, offer.sha256);
    downloading = false;
    if (!ok) {
        hideNotice();
        return;
    }
    showNotice("Update installiert – Neustart");
    Serial.println("[ota] fertig – Neustart mit der neuen Firmware");
    delay(800);
    ESP.restart();
}

}  // namespace ota
