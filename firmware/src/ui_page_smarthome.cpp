#include <vector>

#include "fonts/ui_fonts.h"
#include "lvgl_port/lvgl_v8_port.h"
#include "tablet_data.h"
#include "ui_pages.h"
#include "ui_theme.h"

// Seite „Smart Home“ (ab 0.9.0): Steckdosen ein- und ausschalten.
//
//   ┌──────────────────────────────┬──────────────────────────────┐
//   │ Kaffeemaschine          (●─) │ Waschküche              (─○) │
//   │ 1240 W                       │ aus                          │
//   ├──────────────────────────────┼──────────────────────────────┤
//   │ …  bis zu acht Karten (2 Spalten × 4 Zeilen)                │
//
// Ein Tipp auf die ganze Karte schaltet um – die Karte ist eine größere
// Trefferfläche als der kleine Schalter. Der Schalter zeigt den Wunsch
// sofort; klappt es nicht, springt er zurück und oben rechts steht warum.
//
// Welche Steckdosen erscheinen, bestimmt die Weboberfläche des Pi
// („auf dem Tablet zeigen“). Der Pi prüft das bei jedem Schaltbefehl selbst –
// das Tablet kann nichts schalten, was dort nicht freigegeben ist.
//
// Arbeitsteilung wie auf den anderen Seiten: Der Klick-Rückruf (LVGL-Sperre)
// merkt sich nur den Wunsch; work() schickt ihn ohne Sperre an den Pi.

namespace ui_pages {
namespace smarthome {
namespace {

using namespace ui_theme;

constexpr lv_coord_t kWidth = 752;
constexpr lv_coord_t kHeight = 300;
constexpr int kCols = 2;
constexpr int kRows = 4;
constexpr int kCards = kCols * kRows;
constexpr lv_coord_t kTop = 52;
constexpr lv_coord_t kGap = 8;
constexpr lv_coord_t kCardW = (kWidth - (kCols - 1) * kGap) / kCols;              // 372
constexpr lv_coord_t kCardH = (kHeight - kTop - (kRows - 1) * kGap) / kRows;      // 56
constexpr lv_coord_t kSwitchW = 60;
constexpr lv_coord_t kSwitchH = 32;
constexpr uint32_t kRefreshMs = 5000;

struct Card {
    lv_obj_t *box = nullptr;
    lv_obj_t *name = nullptr;
    lv_obj_t *detail = nullptr;
    lv_obj_t *toggle = nullptr;
};

lv_obj_t *page = nullptr;
lv_obj_t *status = nullptr;
lv_obj_t *hint = nullptr;
Card cards[kCards];

// Nur unter der LVGL-Sperre lesen oder ersetzen (Rückruf und work() greifen zu).
std::vector<tablet_data::Plug> plugs;

// Schaltwunsch aus dem Rückruf, ausgeführt in work(). Ebenfalls unter der Sperre.
bool pending = false;
String pendingId;
bool pendingOn = false;

uint32_t lastFetchMs = 0;
bool needsRefresh = true;

lv_obj_t *fixedLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                     lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h) {
    // Größe setzen: Ohne sie wüchse die Beschriftung über die Karte hinaus
    // (Merksatz im BAUPLAN).
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

String detailText(const tablet_data::Plug &plug) {
    if (!plug.online) return "nicht erreichbar";
    if (plug.on == 1) return plug.watts >= 0 ? String(plug.watts) + " W" : String("an");
    if (plug.on == 0) return "aus";
    return "Zustand unbekannt";
}

void showCard(int index) {
    Card &card = cards[index];
    if (index >= static_cast<int>(plugs.size())) {
        lv_obj_add_flag(card.box, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const tablet_data::Plug &plug = plugs[index];
    lv_obj_clear_flag(card.box, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(card.name, plug.name.c_str());
    lv_obj_set_style_text_color(card.name, lv_color_hex(plug.online ? kText : kTextMuted), 0);
    lv_label_set_text(card.detail, detailText(plug).c_str());
    lv_obj_set_style_text_color(card.detail,
                                lv_color_hex(!plug.online ? kAlarm
                                             : plug.on == 1 ? kAccent : kTextMuted), 0);
    if (plug.on == 1) lv_obj_add_state(card.toggle, LV_STATE_CHECKED);
    else lv_obj_clear_state(card.toggle, LV_STATE_CHECKED);
    if (plug.online) lv_obj_clear_state(card.toggle, LV_STATE_DISABLED);
    else lv_obj_add_state(card.toggle, LV_STATE_DISABLED);
}

void showAll() {
    for (int i = 0; i < kCards; ++i) showCard(i);
    if (plugs.empty()) lv_obj_clear_flag(hint, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(hint, LV_OBJ_FLAG_HIDDEN);
}

String summary() {
    int on = 0;
    for (const tablet_data::Plug &plug : plugs) {
        if (plug.on == 1) ++on;
    }
    if (plugs.empty()) return String();
    return String(on) + " von " + String(plugs.size()) + " an";
}

// Tipp auf eine Karte (LVGL-Aufgabe, Sperre gehalten): nur vormerken.
void onCardClicked(lv_event_t *event) {
    const int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
    if (pending || index < 0 || index >= static_cast<int>(plugs.size())) return;
    const tablet_data::Plug &plug = plugs[index];
    if (!plug.online) {
        lv_label_set_text(status, "Steckdose nicht erreichbar");
        return;
    }
    pendingOn = plug.on != 1;
    pendingId = plug.id;
    pending = true;
    // Sofort sichtbar machen, was gewünscht ist – die Antwort kann dauern.
    if (pendingOn) lv_obj_add_state(cards[index].toggle, LV_STATE_CHECKED);
    else lv_obj_clear_state(cards[index].toggle, LV_STATE_CHECKED);
    lv_label_set_text(status, pendingOn ? "Schalte ein …" : "Schalte aus …");
}

void buildCard(int index) {
    Card &card = cards[index];
    const lv_coord_t x = (index % kCols) * (kCardW + kGap);
    const lv_coord_t y = kTop + (index / kCols) * (kCardH + kGap);

    // Flache Fläche wie ein Knopf (ui_theme), aber mit eigenem Inhalt.
    card.box = lv_obj_create(page);
    lv_obj_set_pos(card.box, x, y);
    lv_obj_set_size(card.box, kCardW, kCardH);
    lv_obj_set_style_bg_color(card.box, lv_color_hex(kSurface), 0);
    lv_obj_set_style_bg_opa(card.box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card.box, lv_color_hex(kSurfacePressed), LV_STATE_PRESSED);
    lv_obj_set_style_radius(card.box, 12, 0);
    lv_obj_set_style_border_width(card.box, 0, 0);
    lv_obj_set_style_shadow_width(card.box, 0, 0);
    lv_obj_set_style_pad_all(card.box, 0, 0);
    lv_obj_clear_flag(card.box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card.box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card.box, onCardClicked, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(index)));

    const lv_coord_t textW = kCardW - kSwitchW - 40;
    card.name = fixedLabel(card.box, &ui_font_22, kText, 16, 4, textW, 26);
    card.detail = fixedLabel(card.box, &ui_font_18, kTextMuted, 16, 31, textW, 22);

    // Der Schalter zeigt nur an; getippt wird auf die Karte.
    card.toggle = lv_switch_create(card.box);
    lv_obj_set_size(card.toggle, kSwitchW, kSwitchH);
    lv_obj_set_pos(card.toggle, kCardW - kSwitchW - 16, (kCardH - kSwitchH) / 2);
    lv_obj_set_style_bg_color(card.toggle, lv_color_hex(kSurfacePressed), 0);
    lv_obj_set_style_bg_color(card.toggle, lv_color_hex(kAccent),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_clear_flag(card.toggle, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_flag(card.box, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace

lv_obj_t *create(lv_obj_t *parent) {
    page = makeSection(parent, 0, 0, kWidth, kHeight);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    makeTitle(page, "Smart Home");
    status = makeStatus(page);
    for (int i = 0; i < kCards; ++i) buildCard(i);
    hint = makeWrappedLabel(page, &ui_font_22, kTextMuted, 0, kTop + 20, kWidth,
                            // „›“ statt Pfeil: U+2192 fehlt in den Schriften (build_fonts.sh)
                            "Keine Steckdose freigegeben. In der Weboberfläche des Pi: "
                            "Einstellungen › Smart Home › Steckdosen › „Tablet“.");
    lv_obj_add_flag(hint, LV_OBJ_FLAG_HIDDEN);
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
        needsRefresh = true;   // gleich den echten Stand holen (bei Fehler: Schalter zurück)
        lastFetchMs = 0;
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
        // Eine Fehlermeldung vom Schalten bleibt stehen, bis neu geschaltet wird.
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
