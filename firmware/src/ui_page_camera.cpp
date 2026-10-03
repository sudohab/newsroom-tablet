#include <vector>

#include <esp_heap_caps.h>
#include <esp_jpeg_dec.h>

#include "fonts/ui_fonts.h"
#include "lvgl_port/lvgl_v8_port.h"
#include "tablet_data.h"
#include "ui_pages.h"
#include "ui_theme.h"

// Seite „Kamera".
//
//   links   das letzte Bild der Kamera (384×288, von newsroom21 verkleinert)
//   rechts  Name, Zustand, „Bild holen", „Stream starten/stoppen"
//
// Die Kamera schickt Bilder nur auf Anforderung an newsroom21 (dort nur im
// Arbeitsspeicher). Das Tablet holt das Bild als JPEG, entpackt es einmal in
// einen Puffer im PSRAM und zeigt diesen an – so muss LVGL beim Neuzeichnen
// nichts entpacken. Auch hier wird nichts gespeichert: Beim Verlassen der
// Seite wird das Bild verworfen und ein laufender Stream gestoppt.

namespace ui_pages {
namespace camera {
namespace {

using namespace ui_theme;

constexpr lv_coord_t kWidth = 752;
constexpr lv_coord_t kHeight = 300;
constexpr int kImageW = 384;
constexpr int kImageH = 288;
constexpr lv_coord_t kRight = kImageW + 24;          // 408
constexpr lv_coord_t kRightWidth = kWidth - kRight;  // 344
constexpr size_t kJpegMax = 64 * 1024;
constexpr size_t kPixelBytes = (size_t)kImageW * kImageH * 2;   // RGB565
constexpr uint32_t kIdleRefreshMs = 1500;
constexpr uint32_t kStreamRefreshMs = 300;

enum class Job { None, Snapshot, StreamOn, StreamOff };
volatile Job job = Job::None;

lv_obj_t *page = nullptr;
lv_obj_t *image = nullptr;
lv_obj_t *placeholder = nullptr;
lv_obj_t *labelName = nullptr;
lv_obj_t *labelState = nullptr;
lv_obj_t *labelInfo = nullptr;
lv_obj_t *buttonStream = nullptr;
lv_obj_t *status = nullptr;

std::vector<tablet_data::Camera> cameras;
String cameraId;             // die angezeigte Kamera (die erste eingerichtete)
int shownSeq = 0;
bool streaming = false;
uint32_t lastFetchMs = 0;
bool needsRefresh = true;

// Beim Verlassen der Seite: Stream stoppen (in background(), nicht im Rückruf).
volatile bool stopOnLeave = false;
String stopCameraId;

// Puffer im PSRAM: das JPEG vom Pi und zwei Bilder (eines wird angezeigt, in
// das andere wird entpackt – so sieht man nie ein halbes Bild).
uint8_t *jpegBuffer = nullptr;
uint8_t *pixels[2] = {nullptr, nullptr};
int frontIndex = 0;
lv_img_dsc_t imageDsc;

bool ensureBuffers() {
    if (jpegBuffer == nullptr) {
        jpegBuffer = static_cast<uint8_t *>(heap_caps_malloc(kJpegMax, MALLOC_CAP_SPIRAM));
    }
    for (uint8_t *&buffer : pixels) {
        // Der Dekoder verlangt 16-Byte-Ausrichtung.
        if (buffer == nullptr) buffer = static_cast<uint8_t *>(jpeg_calloc_align(kPixelBytes, 16));
    }
    return jpegBuffer != nullptr && pixels[0] != nullptr && pixels[1] != nullptr;
}

// Entpackt ein JPEG in `out` (RGB565). Nur genau 384×288 wird angenommen –
// alles andere passt nicht in den Puffer und wird verworfen.
bool decode(uint8_t *jpeg, size_t length, uint8_t *out) {
    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
    jpeg_dec_handle_t decoder = nullptr;
    if (jpeg_dec_open(&config, &decoder) != JPEG_ERR_OK) return false;

    jpeg_dec_io_t io = {};
    io.inbuf = jpeg;
    io.inbuf_len = (int)length;
    jpeg_dec_header_info_t info = {};
    bool ok = false;
    int needed = 0;
    if (jpeg_dec_parse_header(decoder, &io, &info) == JPEG_ERR_OK &&
        info.width == kImageW && info.height == kImageH &&
        jpeg_dec_get_outbuf_len(decoder, &needed) == JPEG_ERR_OK &&
        needed > 0 && (size_t)needed <= kPixelBytes) {
        io.outbuf = out;
        ok = jpeg_dec_process(decoder, &io) == JPEG_ERR_OK;
    }
    jpeg_dec_close(decoder);
    return ok;
}

void onSnapshot(lv_event_t *) {
    lv_label_set_text(status, "Fordere Bild an …");
    job = Job::Snapshot;
}

void onStream(lv_event_t *) {
    lv_label_set_text(status, streaming ? "Stoppe …" : "Starte …");
    job = streaming ? Job::StreamOff : Job::StreamOn;
}

void showState(const tablet_data::Camera *cam) {
    lv_obj_t *streamLabel = lv_obj_get_child(buttonStream, 0);
    if (cam == nullptr) {
        lv_label_set_text(labelName, "Kamera");
        lv_label_set_text(labelState, "Nicht eingerichtet");
        lv_obj_set_style_text_color(labelState, lv_color_hex(kTextMuted), 0);
        lv_label_set_text(labelInfo, "Am Pi: secrets.sh, Kamera-Token");
        if (streamLabel != nullptr) lv_label_set_text(streamLabel, "Stream starten");
        return;
    }
    lv_label_set_text(labelName, cam->name.c_str());
    lv_label_set_text(labelState, cam->stream ? "Stream läuft" : (cam->online ? "Verbunden" : "Nicht erreichbar"));
    lv_obj_set_style_text_color(labelState,
        lv_color_hex(cam->stream ? kAccent : (cam->online ? kText : kTextMuted)), 0);
    String info;
    if (cam->stream) info = "endet in " + String(cam->streamSecondsLeft) + " s";
    else if (cam->snapshot) info = "Warte auf das Bild …";
    else if (cam->seq > 0 && cam->age >= 0) info = "Bild von vor " + String(cam->age) + " s";
    else info = "Kein Bild – „Bild holen“";
    lv_label_set_text(labelInfo, info.c_str());
    if (streamLabel != nullptr) lv_label_set_text(streamLabel, cam->stream ? "Stream stoppen" : "Stream starten");
}

lv_obj_t *fixedLabel(const lv_font_t *font, uint32_t color, lv_coord_t x, lv_coord_t y,
                     lv_coord_t w, lv_coord_t h) {
    lv_obj_t *label = lv_label_create(page);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, w, h);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_label_set_text(label, "");
    return label;
}

}  // namespace

lv_obj_t *create(lv_obj_t *parent) {
    page = makeSection(parent, 0, 0, kWidth, kHeight);

    // --- links: Bild ---------------------------------------------------------
    placeholder = lv_obj_create(page);
    lv_obj_set_pos(placeholder, 0, 6);
    lv_obj_set_size(placeholder, kImageW, kImageH);
    lv_obj_set_style_bg_color(placeholder, lv_color_hex(kSurface), 0);
    lv_obj_set_style_bg_opa(placeholder, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(placeholder, 0, 0);
    lv_obj_set_style_radius(placeholder, 0, 0);
    lv_obj_clear_flag(placeholder, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *hint = lv_label_create(placeholder);
    lv_obj_set_style_text_font(hint, &ui_font_18, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(kTextMuted), 0);
    lv_label_set_text(hint, "Kein Bild");
    lv_obj_center(hint);

    image = lv_img_create(page);
    lv_obj_set_pos(image, 0, 6);
    lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);

    // --- rechts: Zustand und Knöpfe -----------------------------------------
    labelName = fixedLabel(&ui_font_30, kText, kRight, 0, kRightWidth, 40);
    labelState = fixedLabel(&ui_font_22, kText, kRight, 46, kRightWidth, 30);
    labelInfo = fixedLabel(&ui_font_18, kTextMuted, kRight, 80, kRightWidth, 24);

    makeButton(page, "Bild holen", onSnapshot, kRightWidth, 50, LV_ALIGN_TOP_LEFT, kRight, 124, kAccent);
    buttonStream = makeButton(page, "Stream starten", onStream, kRightWidth, 50,
                              LV_ALIGN_TOP_LEFT, kRight, 186);
    status = fixedLabel(&ui_font_18, kTextMuted, kRight, 250, kRightWidth, 44);
    lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);

    lv_label_set_text(labelName, "Kamera");
    lv_label_set_text(labelState, "…");
    return page;
}

void activate() {
    needsRefresh = true;
    stopOnLeave = false;
}

void deactivate() {
    // Läuft im LVGL-Rückruf: nur vormerken und das Bild wegnehmen. Der Stream
    // wird in background() gestoppt (das geht über das Netz).
    if (streaming && !cameraId.isEmpty()) {
        stopCameraId = cameraId;
        stopOnLeave = true;
    }
    streaming = false;
    shownSeq = 0;
    lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(placeholder, LV_OBJ_FLAG_HIDDEN);
}

void background() {
    if (!stopOnLeave) return;
    stopOnLeave = false;
    tablet_data::cameraStream(stopCameraId, false);
}

void work() {
    // Knopfdrücke zuerst.
    const Job current = job;
    if (current != Job::None && !cameraId.isEmpty()) {
        job = Job::None;
        String error;
        if (current == Job::Snapshot) error = tablet_data::cameraSnapshot(cameraId);
        else error = tablet_data::cameraStream(cameraId, current == Job::StreamOn);
        lvgl_port_lock(-1);
        lv_label_set_text(status, error.isEmpty() ? "" : ("Fehler: " + error).c_str());
        lvgl_port_unlock();
        needsRefresh = true;
    } else if (current != Job::None) {
        job = Job::None;
    }

    const uint32_t now = millis();
    const uint32_t interval = streaming ? kStreamRefreshMs : kIdleRefreshMs;
    if (!needsRefresh && now - lastFetchMs < interval) return;
    needsRefresh = false;
    lastFetchMs = now;

    const String error = tablet_data::fetchCameras(cameras);
    const tablet_data::Camera *cam = cameras.empty() ? nullptr : &cameras.front();
    cameraId = cam != nullptr ? cam->id : String();
    streaming = cam != nullptr && cam->stream;

    // Neues Bild? Holen und entpacken – ohne LVGL-Sperre.
    bool newImage = false;
    bool noImage = cam == nullptr || cam->seq == 0;
    if (cam != nullptr && cam->seq > 0 && cam->seq != shownSeq && ensureBuffers()) {
        size_t received = 0;
        const String frameError = tablet_data::fetchCameraFrame(cameraId, shownSeq, jpegBuffer,
                                                                kJpegMax, received);
        if (frameError.isEmpty() && received > 0) {
            const int back = 1 - frontIndex;
            if (decode(jpegBuffer, received, pixels[back])) {
                frontIndex = back;
                shownSeq = cam->seq;
                newImage = true;
            } else {
                Serial.println("[kamera] Bild nicht lesbar");
            }
        }
    }

    lvgl_port_lock(-1);
    if (!error.isEmpty()) lv_label_set_text(status, error.c_str());
    showState(error.isEmpty() ? cam : nullptr);
    if (newImage) {
        imageDsc.header.always_zero = 0;
        imageDsc.header.cf = LV_IMG_CF_TRUE_COLOR;
        imageDsc.header.w = kImageW;
        imageDsc.header.h = kImageH;
        imageDsc.data_size = kPixelBytes;
        imageDsc.data = pixels[frontIndex];
        lv_img_set_src(image, &imageDsc);
        lv_obj_clear_flag(image, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(placeholder, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(image);
    } else if (noImage && shownSeq != 0) {
        // Das Bild ist auf dem Pi abgelaufen (fünf Minuten): auch hier weg.
        shownSeq = 0;
        lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(placeholder, LV_OBJ_FLAG_HIDDEN);
    }
    lvgl_port_unlock();
}

}  // namespace camera
}  // namespace ui_pages
