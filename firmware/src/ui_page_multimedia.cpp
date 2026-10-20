#include "fonts/ui_fonts.h"
#include "lvgl_port/lvgl_v8_port.h"
#include "tablet_state.h"
#include "ui.h"
#include "ui_icons.h"
#include "ui_pages.h"
#include "ui_theme.h"

// Seite „Multimedia“ (ab 0.9.0): Radio und Podcast unter einer Kachel.
//
//   ┌───────────────────────────┐ ┌───────────────────────────┐
//   │ (Symbol)                  │ │ (Symbol)                  │
//   │ Radio                     │ │ Podcast                   │
//   │ BR24 · läuft              │ │ weiterhören               │
//   └───────────────────────────┘ └───────────────────────────┘
//   Läuft: BR24                                     [ Stopp ]
//
// Ein Tipp auf eine der beiden Flächen öffnet die bisherige Radio- bzw.
// Podcast-Seite; deren Knopf unten links führt mit „‹ Multimedia“ hierher
// zurück. Darunter steht, was gerade läuft, mit einem Knopf zum Anhalten –
// das spart den Weg in die Unterseite.
//
// Alles kommt aus dem laufenden Zustand (tablet_state, alle 2 s vom Pi);
// diese Seite holt selbst nichts. Nur „Stopp“ geht ans Netz – in work().

namespace ui_pages {
namespace multimedia {
namespace {

using namespace ui_theme;

constexpr lv_coord_t kWidth = 752;
constexpr lv_coord_t kHeight = 300;
constexpr lv_coord_t kTop = 56;
constexpr lv_coord_t kGap = 16;
constexpr lv_coord_t kTileW = (kWidth - kGap) / 2;     // 368
constexpr lv_coord_t kTileH = 150;
constexpr lv_coord_t kPad = 20;

lv_obj_t *page = nullptr;
lv_obj_t *status = nullptr;
lv_obj_t *radioInfo = nullptr;
lv_obj_t *podcastInfo = nullptr;
lv_obj_t *nowPlaying = nullptr;
lv_obj_t *stopButton = nullptr;

// Zuletzt angezeigter Stand – neu gezeichnet wird nur bei einer Änderung.
String shownState;
String shownKind;
String shownTitle;
bool firstShow = true;

// „Stopp“ gedrückt: in work() ausführen (Netz, ohne LVGL-Sperre).
volatile bool stopRequested = false;

lv_obj_t *fixedLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                     lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, const char *text) {
    // Feste Größe – siehe Merksatz im BAUPLAN.
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, w, h);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_label_set_text(label, text);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

void onTileClicked(lv_event_t *event) {
    auto create = reinterpret_cast<lv_obj_t *(*)(lv_obj_t *)>(lv_event_get_user_data(event));
    const int index = indexOf(create);
    if (index >= 0) ui::openPage(index);
}

void onStopClicked(lv_event_t *) {
    stopRequested = true;
    lv_label_set_text(status, "Halte an …");
}

// Eine große Fläche mit Symbol, Name und Kurzinfo; Rückgabe: die Kurzinfo.
lv_obj_t *buildTile(lv_coord_t x, const char *icon, const char *name,
                    lv_obj_t *(*target)(lv_obj_t *)) {
    lv_obj_t *btn = lv_btn_create(page);
    lv_obj_set_pos(btn, x, kTop);
    lv_obj_set_size(btn, kTileW, kTileH);
    lv_obj_set_style_bg_color(btn, lv_color_hex(kSurface), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(kSurfacePressed), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, onTileClicked, LV_EVENT_CLICKED, reinterpret_cast<void *>(target));

    const lv_coord_t innerW = kTileW - 2 * kPad;
    fixedLabel(btn, &ui_icons_32, kAccent, kPad, 18, 40, 36, icon);
    fixedLabel(btn, &ui_font_30, kText, kPad, 62, innerW, 38, name);
    return fixedLabel(btn, &ui_font_18, kTextMuted, kPad, 108, innerW, 24, "");
}

// Kurzinfos und „Läuft:“-Zeile aus dem laufenden Zustand. Unter der Sperre.
void showState(bool force) {
    const tablet_state::Snapshot &state = tablet_state::current();
    if (!force && !firstShow && state.mediaState == shownState && state.mediaKind == shownKind
        && state.mediaTitle == shownTitle) {
        return;
    }
    firstShow = false;
    shownState = state.mediaState;
    shownKind = state.mediaKind;
    shownTitle = state.mediaTitle;

    const bool playing = state.mediaState == "playing";
    const bool radio = playing && state.mediaKind == "radio";
    const bool podcast = playing && state.mediaKind == "podcast";
    lv_label_set_text(radioInfo, radio && !state.mediaTitle.isEmpty() ? state.mediaTitle.c_str()
                                                                       : "Sender wählen");
    lv_obj_set_style_text_color(radioInfo, lv_color_hex(radio ? kAccent : kTextMuted), 0);
    lv_label_set_text(podcastInfo, podcast && !state.mediaTitle.isEmpty() ? state.mediaTitle.c_str()
                                                                           : "weiterhören");
    lv_obj_set_style_text_color(podcastInfo, lv_color_hex(podcast ? kAccent : kTextMuted), 0);

    if (playing) {
        const String text = "Läuft: " + (state.mediaTitle.isEmpty() ? String("…") : state.mediaTitle);
        lv_label_set_text(nowPlaying, text.c_str());
        lv_obj_clear_flag(stopButton, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(nowPlaying, "Es läuft nichts.");
        lv_obj_add_flag(stopButton, LV_OBJ_FLAG_HIDDEN);
    }
}

}  // namespace

lv_obj_t *create(lv_obj_t *parent) {
    page = makeSection(parent, 0, 0, kWidth, kHeight);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    makeTitle(page, "Multimedia");
    status = makeStatus(page);

    radioInfo = buildTile(0, ui_icons::kRadio, "Radio", radio::create);
    podcastInfo = buildTile(kTileW + kGap, ui_icons::kPodcast, "Podcast", podcasts::create);

    nowPlaying = fixedLabel(page, &ui_font_22, kText, 0, kTop + kTileH + 34, kWidth - 200, 30, "");
    stopButton = makeButton(page, "Stopp", onStopClicked, 160, 50, LV_ALIGN_TOP_RIGHT, 0,
                            kTop + kTileH + 24, kAlarm);
    showState(true);
    return page;
}

void activate() {
    lvgl_port_lock(-1);
    lv_label_set_text(status, "");
    showState(true);
    lvgl_port_unlock();
}

void work() {
    if (stopRequested) {
        stopRequested = false;
        const String error = tablet_state::stopMedia();   // ohne Sperre, bis zu 8 s
        lvgl_port_lock(-1);
        lv_label_set_text(status, error.c_str());
        lvgl_port_unlock();
    }
    // Billig: zeichnet nur, wenn sich Zustand, Art oder Titel geändert haben.
    lvgl_port_lock(-1);
    showState(false);
    lvgl_port_unlock();
}

}  // namespace multimedia
}  // namespace ui_pages
