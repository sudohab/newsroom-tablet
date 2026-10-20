#include <vector>

#include "fonts/ui_fonts.h"
#include "lvgl_port/lvgl_v8_port.h"
#include "tablet_data.h"
#include "ui_pages.h"
#include "ui_theme.h"

// Seite „Smart Home“: Steckdosen ein- und ausschalten (Entwurf A, 0.10.0).
//
//   Smart Home        ‹  Seite 1/2  ›                 4 von 17 an
//   ┌────────┬────────┬────────┬────────┬────────┐
//   │Kaffee- │Wasser- │Toaster │Steh-   │Fern-   │   15 Kacheln je Seite
//   │maschine│kocher  │        │lampe   │seher   │   (5 × 3, je 144 × 77)
//   │1240 W  │aus     │aus     │9 W     │87 W    │
//   ├────────┼────────┼────────┼────────┼────────┤   eingeschaltet = ganze
//   │ …      │        │        │        │        │   Kachel blau
//
// Ein Tipp auf die Kachel schaltet um. Die Kachel zeigt den Wunsch sofort;
// klappt es nicht, springt sie beim nächsten Abruf zurück, und oben rechts
// steht warum. Sind es mehr als 15 Steckdosen, blättert man mit ‹ und ›.
//
// Welche Steckdosen erscheinen und in welcher Reihenfolge, bestimmt die
// Weboberfläche des Pi („auf dem Tablet zeigen“). Der Pi prüft bei jedem
// Schaltbefehl selbst, ob die Steckdose freigegeben ist.
//
// Arbeitsteilung wie auf den anderen Seiten: Klick-Rückrufe (LVGL-Sperre)
// merken sich nur den Wunsch; work() spricht ohne Sperre mit dem Pi.

namespace ui_pages {
namespace smarthome {
namespace {

using namespace ui_theme;

constexpr lv_coord_t kWidth = 752;
constexpr lv_coord_t kHeight = 300;
constexpr int kCols = 5;
constexpr int kRows = 3;
constexpr int kPerPage = kCols * kRows;                                      // 15
constexpr lv_coord_t kTop = 52;
constexpr lv_coord_t kGap = 8;
constexpr lv_coord_t kTileW = (kWidth - (kCols - 1) * kGap) / kCols;        // 144
constexpr lv_coord_t kTileH = (kHeight - kTop - (kRows - 1) * kGap) / kRows; // 77
constexpr lv_coord_t kPad = 12;
constexpr uint32_t kRefreshMs = 5000;
constexpr uint32_t kOnValueColor = 0xdbeaff;   // Wattzahl auf blauer Kachel

struct Tile {
    lv_obj_t *box = nullptr;
    lv_obj_t *name = nullptr;
    lv_obj_t *value = nullptr;
};

lv_obj_t *page = nullptr;
lv_obj_t *status = nullptr;
lv_obj_t *hint = nullptr;
lv_obj_t *prevButton = nullptr;
lv_obj_t *nextButton = nullptr;
lv_obj_t *pageLabel = nullptr;
Tile tiles[kPerPage];

// Nur unter der LVGL-Sperre lesen oder ersetzen (Rückruf und work() greifen zu).
std::vector<tablet_data::Plug> plugs;
int currentPage = 0;

// Schaltwunsch aus dem Rückruf, ausgeführt in work(). Ebenfalls unter der Sperre.
bool pending = false;
String pendingId;
bool pendingOn = false;

uint32_t lastFetchMs = 0;
bool needsRefresh = true;

int pageCount() {
    const int count = static_cast<int>(plugs.size());
    return count == 0 ? 1 : (count + kPerPage - 1) / kPerPage;
}

lv_obj_t *fixedLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                     lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h) {
    // Größe setzen: Ohne sie wüchse die Beschriftung über die Kachel hinaus
    // (Merksatz im BAUPLAN). LONG_DOT kürzt mit „…“, auch bei zwei Zeilen.
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, w, h);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_label_set_text(label, "");
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

String valueText(const tablet_data::Plug &plug) {
    if (!plug.online) return "offline";
    if (plug.on == 1) return plug.watts > 0 ? String(plug.watts) + " W" : String("an");
    if (plug.on == 0) return "aus";
    return "?";
}

// Aussehen einer Kachel: an = blaue Fläche, aus = graue Fläche, offline = blass.
void styleTile(Tile &tile, bool on, bool online) {
    lv_obj_set_style_bg_color(tile.box, lv_color_hex(on ? kAccent : kSurface), 0);
    lv_obj_set_style_text_color(tile.name, lv_color_hex(online ? kText : kTextMuted), 0);
    lv_obj_set_style_text_color(tile.value,
                                lv_color_hex(!online ? kAlarm : on ? kOnValueColor : kTextMuted), 0);
}

void showTile(int slot) {
    Tile &tile = tiles[slot];
    const int index = currentPage * kPerPage + slot;
    if (index >= static_cast<int>(plugs.size())) {
        lv_obj_add_flag(tile.box, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const tablet_data::Plug &plug = plugs[index];
    lv_obj_clear_flag(tile.box, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(tile.name, plug.name.c_str());
    lv_label_set_text(tile.value, valueText(plug).c_str());
    styleTile(tile, plug.on == 1, plug.online);
}

void showPaging() {
    const int pages = pageCount();
    if (currentPage >= pages) currentPage = pages - 1;
    if (pages <= 1) {
        lv_obj_add_flag(prevButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(nextButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(pageLabel, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(prevButton, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(nextButton, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(pageLabel, LV_OBJ_FLAG_HIDDEN);
    if (currentPage > 0) lv_obj_clear_state(prevButton, LV_STATE_DISABLED);
    else lv_obj_add_state(prevButton, LV_STATE_DISABLED);
    if (currentPage < pages - 1) lv_obj_clear_state(nextButton, LV_STATE_DISABLED);
    else lv_obj_add_state(nextButton, LV_STATE_DISABLED);
    const String text = "Seite " + String(currentPage + 1) + "/" + String(pages);
    lv_label_set_text(pageLabel, text.c_str());
}

void showAll() {
    showPaging();
    for (int i = 0; i < kPerPage; ++i) showTile(i);
    if (plugs.empty()) lv_obj_clear_flag(hint, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(hint, LV_OBJ_FLAG_HIDDEN);
}

String summary() {
    if (plugs.empty()) return String();
    int on = 0;
    for (const tablet_data::Plug &plug : plugs) {
        if (plug.on == 1) ++on;
    }
    return String(on) + " von " + String(plugs.size()) + " an";
}

// Tipp auf eine Kachel (LVGL-Aufgabe, Sperre gehalten): nur vormerken.
void onTileClicked(lv_event_t *event) {
    const int slot = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
    const int index = currentPage * kPerPage + slot;
    if (pending || slot < 0 || index >= static_cast<int>(plugs.size())) return;
    const tablet_data::Plug &plug = plugs[index];
    if (!plug.online) {
        lv_label_set_text(status, "Steckdose nicht erreichbar");
        return;
    }
    pendingOn = plug.on != 1;
    pendingId = plug.id;
    pending = true;
    // Sofort zeigen, was gewünscht ist – die Antwort kann dauern.
    styleTile(tiles[slot], pendingOn, true);
    lv_label_set_text(tiles[slot].value, pendingOn ? "…" : "aus");
    lv_label_set_text(status, pendingOn ? "Schalte ein …" : "Schalte aus …");
}

void onPrev(lv_event_t *) {
    if (currentPage > 0) --currentPage;
    showAll();
}

void onNext(lv_event_t *) {
    if (currentPage < pageCount() - 1) ++currentPage;
    showAll();
}

void buildTile(int slot) {
    Tile &tile = tiles[slot];
    const lv_coord_t x = (slot % kCols) * (kTileW + kGap);
    const lv_coord_t y = kTop + (slot / kCols) * (kTileH + kGap);

    // Flache Fläche wie die Kacheln der Startseite (ui_page_home.cpp).
    tile.box = lv_obj_create(page);
    lv_obj_set_pos(tile.box, x, y);
    lv_obj_set_size(tile.box, kTileW, kTileH);
    lv_obj_set_style_bg_color(tile.box, lv_color_hex(kSurface), 0);
    lv_obj_set_style_bg_opa(tile.box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(tile.box, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_set_style_radius(tile.box, 12, 0);
    lv_obj_set_style_border_width(tile.box, 0, 0);
    lv_obj_set_style_shadow_width(tile.box, 0, 0);
    lv_obj_set_style_pad_all(tile.box, 0, 0);
    lv_obj_clear_flag(tile.box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(tile.box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tile.box, onTileClicked, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(slot)));

    const lv_coord_t innerW = kTileW - 2 * kPad;
    // Name bis zu zwei Zeilen (2 × 22 px), darunter Watt bzw. an/aus.
    tile.name = fixedLabel(tile.box, &ui_font_18, kText, kPad, 7, innerW, 44);
    tile.value = fixedLabel(tile.box, &ui_font_18, kTextMuted, kPad, kTileH - 7 - 22, innerW, 22);

    lv_obj_add_flag(tile.box, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace

lv_obj_t *create(lv_obj_t *parent) {
    page = makeSection(parent, 0, 0, kWidth, kHeight);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    makeTitle(page, "Smart Home");
    status = makeStatus(page);

    // Blättern – nur sichtbar bei mehr als 15 Steckdosen. Rechts neben der
    // Überschrift (rund 190 px breit), links vom Status.
    prevButton = makeButton(page, "\xE2\x80\xB9", onPrev, 56, 40, LV_ALIGN_TOP_LEFT, 220, 0);
    pageLabel = fixedLabel(page, &ui_font_18, kTextMuted, 284, 9, 100, 24);
    lv_obj_set_style_text_align(pageLabel, LV_TEXT_ALIGN_CENTER, 0);
    nextButton = makeButton(page, "\xE2\x80\xBA", onNext, 56, 40, LV_ALIGN_TOP_LEFT, 392, 0);

    for (int i = 0; i < kPerPage; ++i) buildTile(i);
    hint = makeWrappedLabel(page, &ui_font_22, kTextMuted, 0, kTop + 20, kWidth,
                            // „›“ statt Pfeil: U+2192 fehlt in den Schriften (build_fonts.sh)
                            "Keine Steckdose freigegeben. In der Weboberfläche des Pi: "
                            "Einstellungen › Smart Home › Steckdosen › „Tablet“.");
    lv_obj_add_flag(hint, LV_OBJ_FLAG_HIDDEN);
    showPaging();
    lv_label_set_text(status, "Lade …");
    return page;
}

void activate() { needsRefresh = true; }

void work() {
    // 1. Ein vorgemerkter Schaltwunsch geht vor.
    lvgl_port_lock(-1);
    const bool doSwitch = pending;
    const String id = pendingId;
    const bool wanted = pendingOn;
    lvgl_port_unlock();

    if (doSwitch) {
        const String error = tablet_data::switchPlug(id, wanted);   // ohne Sperre
        lvgl_port_lock(-1);
        pending = false;
        lv_label_set_text(status, error.isEmpty() ? "" : error.c_str());
        lvgl_port_unlock();
        needsRefresh = true;   // gleich den echten Stand holen (bei Fehler: Kachel zurück)
    }

    // 2. Liste regelmäßig neu holen, solange die Seite offen ist.
    const uint32_t now = millis();
    if (!needsRefresh && now - lastFetchMs < kRefreshMs) return;
    needsRefresh = false;
    lastFetchMs = now;

    std::vector<tablet_data::Plug> fresh;
    const String error = tablet_data::fetchPlugs(fresh);           // ohne Sperre

    lvgl_port_lock(-1);
    if (error.isEmpty()) {
        plugs.swap(fresh);
        showAll();
        // Eine Fehlermeldung vom Schalten bleibt bis zum nächsten Abruf stehen.
        const char *current = lv_label_get_text(status);
        if (!doSwitch || current == nullptr || current[0] == '\0') {
            lv_label_set_text(status, summary().c_str());
        }
    } else {
        lv_label_set_text(status, error.c_str());
    }
    lvgl_port_unlock();
}

}  // namespace smarthome
}  // namespace ui_pages
