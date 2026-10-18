#include <WiFi.h>

#include <vector>

#include "fonts/ui_fonts.h"
#include "lvgl_port/lvgl_v8_port.h"
#include "tablet_data.h"
#include "tablet_state.h"
#include "ui.h"
#include "ui_pages.h"
#include "ui_theme.h"

// Startseite: ein Raster aus Kacheln, eine je Seite.
//
//     ┌────────┬────────┬────────┬────────┬────────┐
//     │ Symbol │        │        │        │    (2) │  ← roter Zähler, z. B.
//     │ News   │ Wetter │ Warnun…│ Termine│ Signal │    verpasste Anrufe
//     │ Spiegel│ Regen …│ keine  │ Do 9:30│ …      │  ← Kurzinfo, grau
//     ├────────┼────────┼────────┼────────┼────────┤
//     │ …      │        │        │        │        │
//
// Welche Kacheln es gibt und in welcher Reihenfolge, steht in
// ui_pages_common.cpp (kPages) – hier wird nur gezeichnet. Ein Tipp auf eine
// Kachel öffnet die Seite; zurück geht es mit „‹ Start“ (ui.cpp).
//
// Die Kurzinfos ersetzen die frühere Startseite (nächster Termin,
// Schlagzeile, Anrufe/Radio/Warnungen): Was dort stand, steht jetzt auf der
// passenden Kachel. Termine und Schlagzeilen werden dafür weiterhin selten
// vom Pi geholt; alles andere kommt aus dem laufenden Zustand
// (tablet_state), der ohnehin alle zwei Sekunden abgefragt wird.

namespace ui_pages {
namespace home {
namespace {

using namespace ui_theme;

// --- Maße -------------------------------------------------------------------
constexpr lv_coord_t kWidth = 752;                  // Inhaltsbreite (800 - 2 x 24)
constexpr lv_coord_t kHeight = 480 - 102 - 12;      // ganze Höhe unter dem Kopf
constexpr int kCols = 5;
constexpr int kRows = 3;
constexpr int kMaxTiles = kCols * kRows;
constexpr lv_coord_t kGap = 12;
constexpr lv_coord_t kTileW = (kWidth - (kCols - 1) * kGap) / kCols;    // 140
constexpr lv_coord_t kTileH = (kHeight - (kRows - 1) * kGap) / kRows;   // 114
constexpr lv_coord_t kPad = 12;                     // Innenabstand der Kachel
constexpr lv_coord_t kBadge = 26;                   // Durchmesser des Zählers

// --- Abrufzeiten (unverändert von der früheren Startseite) ------------------
constexpr uint32_t kHeadlineIntervalMs = 120000;   // Quelle alle zwei Minuten wechseln
constexpr uint32_t kNewsRefreshMs = 600000;        // Listen alle zehn Minuten neu
// Ohne Schlagzeilen erst nach einer halben Minute wieder fragen – sonst
// fragte das Tablet nach einem Fehlschlag in jedem Durchlauf der
// Hauptschleife (gefunden 28.09.2026: 429 von Caddy, Schleife hielt sich
// selbst am Laufen).
constexpr uint32_t kNewsRetryMs = 30000;

// Eine Kachel: Knopf mit Symbol, Name, Kurzinfo und optionalem Zähler.
struct Tile {
    int page = -1;              // Nummer in ui_pages::all()
    lv_obj_t *button = nullptr;
    lv_obj_t *info = nullptr;
    lv_obj_t *badge = nullptr;
    lv_obj_t *badgeLabel = nullptr;
};

lv_obj_t *page = nullptr;
Tile tiles[kMaxTiles];
int tileCount = 0;

std::vector<tablet_data::Event> events;
std::vector<tablet_data::Headline> headlines;
size_t headlineIndex = 0;
uint32_t lastHeadlineMs = 0;
uint32_t lastNewsFetchMs = 0;
uint32_t lastCalendarFetchMs = 0;
bool needsRefresh = true;

// Text nur setzen, wenn er sich ändert. lv_label_set_text zeichnet sonst
// bei jedem Aufruf neu – und die Kacheln werden in jedem Durchlauf der
// Hauptschleife aufgefrischt.
void setText(lv_obj_t *label, const char *text) {
    if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

void setColor(lv_obj_t *label, uint32_t color) {
    const lv_color_t want = lv_color_hex(color);
    if (lv_obj_get_style_text_color(label, 0).full != want.full) {
        lv_obj_set_style_text_color(label, want, 0);
    }
}

void onTileClicked(lv_event_t *event) {
    const int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
    ui::openPage(index);
}

// Eine Beschriftung mit fester Größe – ohne Größe wüchse sie mit ihrem Text
// nach rechts und unten aus der Kachel (siehe Merksatz im BAUPLAN).
lv_obj_t *fixedLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                     lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, const char *text) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, w, h);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_label_set_text(label, text);
    return label;
}

void buildTile(Tile &tile, int pageIndex, int slot) {
    const Page &target = all()[pageIndex];
    const lv_coord_t x = (slot % kCols) * (kTileW + kGap);
    const lv_coord_t y = (slot / kCols) * (kTileH + kGap);

    lv_obj_t *btn = lv_btn_create(page);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, kTileW, kTileH);
    // Flach wie der Rest: deckende Fläche, kein Schatten, kein Rahmen.
    lv_obj_set_style_bg_color(btn, lv_color_hex(kSurface), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(kSurfacePressed), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, onTileClicked, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(pageIndex)));

    const lv_coord_t innerW = kTileW - 2 * kPad;
    fixedLabel(btn, &ui_icons_32, target.tint, kPad, 10, 40, 36, target.icon);
    fixedLabel(btn, &ui_font_22, kText, kPad, 54, innerW, 28, target.label);
    tile.info = fixedLabel(btn, &ui_font_18, kTextMuted, kPad, 84, innerW, 22, "");

    // Roter Zähler oben rechts (verpasste Anrufe, Warnungen, klingelnde
    // Timer). Er ist nur Anzeige: nicht anklickbar, damit der Tipp auf ihn
    // beim Knopf ankommt.
    tile.badge = lv_obj_create(btn);
    lv_obj_set_size(tile.badge, kBadge, kBadge);
    lv_obj_set_pos(tile.badge, kTileW - kPad - kBadge, 10);
    lv_obj_set_style_radius(tile.badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(tile.badge, lv_color_hex(kAlarm), 0);
    lv_obj_set_style_bg_opa(tile.badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tile.badge, 0, 0);
    lv_obj_set_style_pad_all(tile.badge, 0, 0);
    lv_obj_clear_flag(tile.badge, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(tile.badge, LV_OBJ_FLAG_SCROLLABLE);
    tile.badgeLabel = lv_label_create(tile.badge);
    lv_obj_set_style_text_font(tile.badgeLabel, &ui_font_18, 0);
    lv_obj_set_style_text_color(tile.badgeLabel, lv_color_hex(kText), 0);
    lv_label_set_text(tile.badgeLabel, "");
    lv_obj_center(tile.badgeLabel);
    lv_obj_add_flag(tile.badge, LV_OBJ_FLAG_HIDDEN);

    tile.page = pageIndex;
    tile.button = btn;
}

// Kurzinfo einer Kachel: Text, Farbe und Zählerstand (0 = kein Zähler).
struct Info {
    String text;
    uint32_t color = kTextMuted;
    int badge = 0;
};

String printerText(const tablet_state::Snapshot &state) {
    if (state.printerState == "printing" || state.printerState == "paused") {
        String text = state.printerProgress >= 0 ? String(state.printerProgress) + " %"
                                                 : String("läuft");
        if (state.printerState == "paused") text += " Pause";
        return text;
    }
    if (state.printerState == "complete") return "fertig";
    return "kein Druck";
}

Info infoFor(const Page &target) {
    const tablet_state::Snapshot &state = tablet_state::current();
    Info info;
    // Die Seiten werden an ihrer create-Funktion erkannt – die ist eindeutig
    // und ändert sich nicht, wenn jemand eine Beschriftung umbenennt.
    if (target.create == news::create) {
        if (headlines.empty()) {
            info.text = "Schlagzeilen";
        } else {
            info.text = headlines[headlineIndex % headlines.size()].source;
            info.color = kText;
        }
    } else if (target.create == weather::create) {
        if (state.hasWeather && state.rainProbability >= 0) {
            info.text = "Regen " + String(state.rainProbability) + " %";
        } else {
            info.text = state.hasWeather ? state.weatherText : String("–");
        }
    } else if (target.create == warnings::create) {
        if (state.warningCount == 0) {
            info.text = "keine";
        } else {
            info.text = state.warningHeadline.isEmpty() ? String(state.warningCount) + " aktiv"
                                                        : state.warningHeadline;
            info.color = kAlarm;
            info.badge = state.warningCount;
        }
    } else if (target.create == calendar::create) {
        if (events.empty()) {
            info.text = "keine";
        } else {
            const tablet_data::Event &next = events.front();
            info.text = next.time.isEmpty() ? next.when : next.when + " " + next.time;
            info.color = kText;
        }
    } else if (target.create == signal::create) {
        info.text = "Nachrichten";
    } else if (target.create == calls::create) {
        if (state.missedCallCount > 0) {
            info.text = "verpasst";
            info.color = kAlarm;
            info.badge = state.missedCallCount;
        } else {
            info.text = "keine verpasst";
        }
    } else if (target.create == clocks::create) {
        info.text = "Text senden";
    } else if (target.create == timers::create) {
        if (state.timerExpired > 0) {
            info.text = "klingelt";
            info.color = kAlarm;
            info.badge = state.timerExpired;
        } else {
            info.text = "stellen";
        }
    } else if (target.create == camera::create) {
        info.text = "Bild ansehen";
    } else if (target.create == printers::create) {
        info.text = printerText(state);
        if (state.printerState == "printing" || state.printerState == "paused") info.color = kAccent;
    } else if (target.create == radio::create) {
        const bool playing = state.mediaState == "playing" && state.mediaKind == "radio";
        info.text = playing && !state.mediaTitle.isEmpty() ? state.mediaTitle : String("aus");
        if (playing) info.color = kAccent;
    } else if (target.create == podcasts::create) {
        const bool playing = state.mediaState == "playing" && state.mediaKind == "podcast";
        info.text = playing && !state.mediaTitle.isEmpty() ? state.mediaTitle : String("weiterhören");
        if (playing) info.color = kAccent;
    } else if (target.create == alarms::create) {
        if (state.alarmActive) {
            info.text = "klingelt";
            info.color = kAlarm;
        } else {
            info.text = state.nextAlarm.isEmpty() ? String("aus") : state.nextAlarm;
            if (!state.nextAlarm.isEmpty()) info.color = kText;
        }
    } else if (target.create == wifi::create) {
        const bool connected = WiFi.status() == WL_CONNECTED;
        info.text = connected ? "verbunden" : "getrennt";
        if (!connected) info.color = kAlarm;
    } else if (target.create == settings::create) {
        info.text = "Anzeige";
    }
    return info;
}

void refreshTiles() {
    for (int i = 0; i < tileCount; ++i) {
        Tile &tile = tiles[i];
        const Info info = infoFor(all()[tile.page]);
        setText(tile.info, info.text.c_str());
        setColor(tile.info, info.color);
        if (info.badge > 0) {
            // Zweistellig reicht: Mehr als 9 zeigt „9+“, sonst passt es nicht in den Kreis.
            setText(tile.badgeLabel, info.badge > 9 ? "9+" : String(info.badge).c_str());
            if (lv_obj_has_flag(tile.badge, LV_OBJ_FLAG_HIDDEN)) lv_obj_clear_flag(tile.badge, LV_OBJ_FLAG_HIDDEN);
        } else if (!lv_obj_has_flag(tile.badge, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(tile.badge, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

}  // namespace

lv_obj_t *create(lv_obj_t *parent) {
    page = makeSection(parent, 0, 0, kWidth, kHeight);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    // Für jede Seite mit Symbol eine Kachel, in der Reihenfolge von kPages.
    tileCount = 0;
    for (int i = 0; i < count() && tileCount < kMaxTiles; ++i) {
        if (all()[i].icon == nullptr) continue;   // die Startseite selbst
        buildTile(tiles[tileCount], i, tileCount);
        ++tileCount;
    }
    if (tileCount == kMaxTiles && count() - 1 > kMaxTiles) {
        Serial.println("[home] mehr Seiten als Kacheln - Raster vergroessern!");
    }
    return page;
}

void activate() {
    // Beim Öffnen sofort auffrischen; geholt wird erst in work().
    needsRefresh = true;
    refreshTiles();
}

void work() {
    const uint32_t now = millis();

    // Termine und Schlagzeilen selten holen – sie ändern sich langsam.
    const uint32_t newsInterval = headlines.empty() ? kNewsRetryMs : kNewsRefreshMs;
    const bool fetchNews = needsRefresh || now - lastNewsFetchMs > newsInterval;
    const bool fetchCalendar = needsRefresh || now - lastCalendarFetchMs > kNewsRefreshMs;
    needsRefresh = false;

    // Netzabfragen OHNE LVGL-Sperre (sie dauern bis zu acht Sekunden).
    if (fetchCalendar) {
        lastCalendarFetchMs = now;
        tablet_data::Month month;
        tablet_data::fetchCalendar(events, month);
    }
    if (fetchNews) {
        lastNewsFetchMs = now;
        const String error = tablet_data::fetchNews(headlines);
        if (!error.isEmpty()) headlines.clear();
        if (headlineIndex >= headlines.size()) headlineIndex = 0;
        lastHeadlineMs = now;
    }
    // Alle zwei Minuten die nächste Quelle auf der News-Kachel.
    if (now - lastHeadlineMs > kHeadlineIntervalMs && !headlines.empty()) {
        lastHeadlineMs = now;
        headlineIndex = (headlineIndex + 1) % headlines.size();
    }

    // Die Kurzinfos hängen am laufenden Zustand und sind billig; gezeichnet
    // wird nur, was sich geändert hat (setText/setColor).
    lvgl_port_lock(-1);
    refreshTiles();
    lvgl_port_unlock();
}

}  // namespace home
}  // namespace ui_pages
