#include "screen_view.h"

#include <esp_heap_caps.h>
#include <lvgl.h>

#include "api_client.h"
#include "lvgl_port/lvgl_v8_port.h"

namespace screen_view {
namespace {

constexpr uint16_t kWidth = 800;
constexpr uint16_t kHeight = 480;
constexpr size_t kRowBytes = kWidth / 8;          // 100
constexpr size_t kFrameBytes = kRowBytes * kHeight;  // 48.000
// LVGL erwartet bei einem 1-Bit-Bild zwei Farben vorneweg, je vier Byte.
constexpr size_t kPaletteBytes = 8;

// Wie oft nachgefragt wird, solange die Ansicht sichtbar ist. Die Ansicht
// ändert sich im Minutentakt; alle fünf Sekunden zu fragen reicht, und dank
// der Prüfsumme kommen dabei meistens nur die Kopfzeilen zurück.
constexpr uint32_t kPollMs = 5000;
// Nach einem Fehler langsamer nachfragen.
constexpr uint32_t kErrorPollMs = 20000;

uint8_t *buffer = nullptr;       // Palette + Bilddaten am Stück
lv_img_dsc_t descriptor = {};
lv_obj_t *imageObject = nullptr;
String currentHash;
String error;
uint32_t lastPollMs = 0;
uint32_t pollDelayMs = kPollMs;

}  // namespace

bool begin(lv_obj_t *parent) {
    // Der Puffer gehört in den PSRAM: 48 KB im internen RAM wären ein Drittel
    // des freien Speichers, und LVGL liest ihn nur (kein DMA nötig).
    buffer = static_cast<uint8_t *>(
        heap_caps_malloc(kPaletteBytes + kFrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buffer == nullptr) {
        error = "Kein Speicher fuer die Ansicht";
        Serial.println("[ansicht] PSRAM-Puffer konnte nicht angelegt werden");
        return false;
    }

    // Farbtabelle: Index 0 = weiß, Index 1 = schwarz. Reihenfolge im Speicher
    // ist Blau, Grün, Rot, Deckkraft.
    const uint8_t palette[kPaletteBytes] = {
        0xff, 0xff, 0xff, 0xff,   // 0 = weiß, deckend
        0x00, 0x00, 0x00, 0xff,   // 1 = schwarz, deckend
    };
    memcpy(buffer, palette, kPaletteBytes);
    // Bis zum ersten Bild alles weiß, damit kein Rauschen zu sehen ist.
    memset(buffer + kPaletteBytes, 0x00, kFrameBytes);

    descriptor.header.cf = LV_IMG_CF_INDEXED_1BIT;
    descriptor.header.always_zero = 0;
    descriptor.header.w = kWidth;
    descriptor.header.h = kHeight;
    descriptor.data_size = kPaletteBytes + kFrameBytes;
    descriptor.data = buffer;

    // Aufrufer hält die LVGL-Sperre bereits (ui::begin baut alle Seiten).
    imageObject = lv_img_create(parent);
    lv_img_set_src(imageObject, &descriptor);
    lv_obj_align(imageObject, LV_ALIGN_TOP_LEFT, 0, 0);
    // Bilder sind in LVGL normalerweise nicht antippbar. Hier schon: Ein Tipp
    // auf die Ansicht führt zur Bedienseite.
    lv_obj_add_flag(imageObject, LV_OBJ_FLAG_CLICKABLE);
    return true;
}

void loop(bool visible) {
    if (buffer == nullptr || !visible) return;
    if (millis() - lastPollMs < pollDelayMs) return;
    lastPollMs = millis();

    size_t received = 0;
    // Die Prüfsumme des Bildes mitschicken, das schon angezeigt wird: Ändert
    // sich nichts, antwortet der Pi mit 304 und schickt keine 48 KB.
    const String query = currentHash.isEmpty() ? String() : "hash=" + currentHash;
    const api_client::Result result = api_client::getBinary(
        "/api/tablet/screen", query, buffer + kPaletteBytes, kFrameBytes, received);

    if (result.status == 304) {
        error = "";
        pollDelayMs = kPollMs;
        return;  // unverändert – nichts zu tun
    }
    if (!result.ok) {
        if (error != result.error) Serial.printf("[ansicht] %s\n", result.error.c_str());
        error = result.error;
        pollDelayMs = kErrorPollMs;
        return;
    }
    if (received != kFrameBytes) {
        // Unvollständiges Bild nicht anzeigen: Der alte Inhalt bleibt stehen.
        error = "Bild unvollstaendig";
        pollDelayMs = kErrorPollMs;
        return;
    }

    if (!error.isEmpty()) Serial.println("[ansicht] Bild wieder da");
    error = "";
    pollDelayMs = kPollMs;
    // Der Pi schickt die Prüfsumme im Kopf mit; sie steht in result.etag.
    currentHash = result.etag;
    Serial.printf("[ansicht] neues Bild (%u Byte)\n", (unsigned)received);

    lvgl_port_lock(-1);
    // LVGL hat das Bild eventuell zwischengespeichert – ohne dieses
    // Ungültigmachen bliebe das alte stehen, obwohl der Puffer neu ist.
    lv_img_cache_invalidate_src(&descriptor);
    lv_img_set_src(imageObject, &descriptor);
    lv_obj_invalidate(imageObject);
    lvgl_port_unlock();
}

lv_obj_t *image() { return imageObject; }

bool hasImage() { return !currentHash.isEmpty(); }

String lastError() { return error; }

}  // namespace screen_view
