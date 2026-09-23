#include <vector>

#include "fonts/ui_fonts.h"
#include "lvgl_port/lvgl_v8_port.h"
#include "tablet_data.h"
#include "ui_pages.h"
#include "ui_theme.h"

// Nachrichten an die Matrix-Uhren.
//
// Links die vorgefertigten Texte, rechts die Auswahl der Uhr und das Feld für
// eine freie Nachricht.
//
// Die vorgefertigten Texte werden **in der Weboberfläche gepflegt**, nicht
// hier. Das Gerät darf sie nur schicken. Deshalb braucht ein neuer Text kein
// Aufspielen der Firmware – er steht nach dem Speichern im Browser beim
// nächsten Öffnen dieser Seite schon da.
//
// Für den freien Text blendet LVGL eine Bildschirmtastatur ein. Sie liegt
// über der ganzen Seite, weil sie sonst keinen Platz hätte; geschickt wird
// mit dem Haken der Tastatur.

namespace ui_pages {
namespace clocks {
namespace {

using namespace ui_theme;

constexpr lv_coord_t kWidth = 752;
constexpr lv_coord_t kHeight = 300;
constexpr lv_coord_t kListTop = 50;
constexpr lv_coord_t kListWidth = 430;
constexpr lv_coord_t kRightLeft = 452;
constexpr lv_coord_t kRightWidth = kWidth - kRightLeft;
constexpr lv_coord_t kButtonHeight = 44;

enum class Job { None, Load, SendPreset, SendText };
volatile Job job = Job::None;

lv_obj_t *page = nullptr;
lv_obj_t *status = nullptr;
lv_obj_t *list = nullptr;
lv_obj_t *targetButton = nullptr;
lv_obj_t *textArea = nullptr;
lv_obj_t *keyboard = nullptr;

std::vector<tablet_data::Clock> clocks;
std::vector<String> presets;

// -1 = an alle Uhren. Sonst die Nummer in `clocks`.
int target = -1;
int pendingPreset = -1;
String pendingText;
bool loaded = false;

String targetId() {
    if (target < 0 || target >= static_cast<int>(clocks.size())) return String();
    return clocks[target].id;
}

String targetName() {
    if (clocks.empty()) return "keine Uhr";
    if (target < 0) return "alle Uhren";
    return clocks[target].name;
}

void updateTargetButton() {
    lv_obj_t *label = lv_obj_get_child(targetButton, 0);
    if (label != nullptr) lv_label_set_text(label, targetName().c_str());
}

// Ein Druck schaltet auf die nächste Uhr weiter, nach der letzten kommt
// „alle". Bei zwei Uhren ist das schneller als eine Auswahlliste, und die
// Beschriftung sagt immer, wohin es geht.
void onTarget(lv_event_t *) {
    if (clocks.empty()) return;
    ++target;
    if (target >= static_cast<int>(clocks.size())) target = -1;
    updateTargetButton();
}

void onPreset(lv_event_t *event) {
    const uint32_t index = lv_obj_get_index(lv_event_get_target(event));
    if (index >= presets.size()) return;
    pendingPreset = static_cast<int>(index);
    lv_label_set_text(status, ("Sende an " + targetName() + " …").c_str());
    job = Job::SendPreset;
}

void onKeyboard(lv_event_t *event) {
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (code != LV_EVENT_READY) return;

    pendingText = lv_textarea_get_text(textArea);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    if (pendingText.isEmpty()) return;
    lv_label_set_text(status, ("Sende an " + targetName() + " …").c_str());
    job = Job::SendText;
}

void onTextFocus(lv_event_t *) {
    lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(keyboard);
}

void rebuild() {
    lv_obj_clean(list);
    for (const auto &text : presets) {
        lv_obj_t *btn = lv_list_add_btn(list, nullptr, text.c_str());
        styleListButton(btn);
        // Abschneiden statt endlos durchlaufen – jede Bewegung heißt neu
        // zeichnen.
        lv_obj_t *label = lv_obj_get_child(btn, 0);
        if (label != nullptr) lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_add_event_cb(btn, onPreset, LV_EVENT_CLICKED, nullptr);
    }
    updateTargetButton();
}

}  // namespace

lv_obj_t *create(lv_obj_t *parent) {
    page = makeSection(parent, 0, 0, kWidth, kHeight);
    makeTitle(page, "Uhren");
    status = makeStatus(page);

    list = lv_list_create(page);
    lv_obj_set_size(list, kListWidth, kHeight - kListTop);
    lv_obj_set_pos(list, 0, kListTop);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 8, 0);

    makeSeparator(page, kRightLeft - 12, kListTop, 1, kHeight - kListTop);

    makeLabel(page, &ui_font_18, kTextMuted, LV_ALIGN_TOP_LEFT, kRightLeft, kListTop,
              "SENDEN AN");
    targetButton = makeButton(page, "alle Uhren", onTarget, kRightWidth, kButtonHeight,
                              LV_ALIGN_TOP_LEFT, kRightLeft, kListTop + 26, kAccent);

    makeLabel(page, &ui_font_18, kTextMuted, LV_ALIGN_TOP_LEFT, kRightLeft,
              kListTop + 86, "EIGENER TEXT");
    textArea = lv_textarea_create(page);
    lv_obj_set_size(textArea, kRightWidth, 60);
    lv_obj_set_pos(textArea, kRightLeft, kListTop + 112);
    lv_textarea_set_one_line(textArea, true);
    lv_textarea_set_max_length(textArea, 80);
    lv_textarea_set_placeholder_text(textArea, "Antippen zum Schreiben");
    lv_obj_set_style_text_font(textArea, &ui_font_22, 0);
    lv_obj_set_style_bg_color(textArea, lv_color_hex(kSurface), 0);
    lv_obj_set_style_text_color(textArea, lv_color_hex(kText), 0);
    lv_obj_set_style_border_width(textArea, 0, 0);
    lv_obj_set_style_radius(textArea, 10, 0);
    lv_obj_add_event_cb(textArea, onTextFocus, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(textArea, onTextFocus, LV_EVENT_CLICKED, nullptr);

    makeWrappedLabel(page, &ui_font_18, kTextMuted, kRightLeft, kListTop + 184,
                     kRightWidth, "Die Texte links werden in der Weboberfläche gepflegt.");

    // Die Tastatur hängt am Bildschirm, nicht an der Seite: Sie ist höher als
    // der Inhaltsbereich und würde sonst abgeschnitten.
    keyboard = lv_keyboard_create(lv_obj_get_screen(parent));
    lv_obj_set_size(keyboard, 800, 230);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(keyboard, textArea);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(kBackground), 0);
    lv_obj_set_style_border_width(keyboard, 0, 0);
    lv_obj_add_event_cb(keyboard, onKeyboard, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(keyboard, onKeyboard, LV_EVENT_CANCEL, nullptr);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);

    return page;
}

void activate() {
    // Die Texte werden in der Weboberfläche gepflegt und können sich seit dem
    // letzten Mal geändert haben – deshalb bei jedem Öffnen neu holen, nicht
    // nur beim ersten.
    job = Job::Load;
}

void deactivate() {
    // Die Tastatur hängt am Bildschirm, nicht an dieser Seite – sie würde
    // also über der nächsten stehen bleiben, wenn sie nicht selbst geht.
    if (keyboard != nullptr) lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}

void work() {
    const Job current = job;
    if (current == Job::None) return;
    job = Job::None;

    String message;
    switch (current) {
        case Job::Load: {
            message = tablet_data::fetchClocks(clocks, presets);
            loaded = message.isEmpty();
            if (target >= static_cast<int>(clocks.size())) target = -1;
            lvgl_port_lock(-1);
            rebuild();
            lv_label_set_text(status,
                !message.isEmpty() ? message.c_str()
                : clocks.empty() ? "Keine Uhr eingerichtet"
                : presets.empty() ? "Keine Texte – in der Weboberfläche anlegen"
                : (String(presets.size()) + " Texte · " + String(clocks.size()) + " Uhren").c_str());
            lvgl_port_unlock();
            return;
        }
        case Job::SendPreset:
            message = tablet_data::sendPreset(targetId(), pendingPreset);
            break;
        case Job::SendText:
            message = tablet_data::sendClockText(targetId(), pendingText);
            break;
        case Job::None:
            return;
    }

    lvgl_port_lock(-1);
    if (message.isEmpty()) {
        lv_label_set_text(status, ("Gesendet an " + targetName()).c_str());
        // Nach dem Senden das Feld leeren: Der nächste Text fängt neu an.
        if (current == Job::SendText) lv_textarea_set_text(textArea, "");
    } else {
        lv_label_set_text(status, message.c_str());
    }
    lvgl_port_unlock();
}

}  // namespace clocks
}  // namespace ui_pages
