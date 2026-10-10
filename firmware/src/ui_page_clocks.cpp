#include <vector>

#include "fonts/ui_fonts.h"
#include "lvgl_port/lvgl_v8_port.h"
#include "tablet_data.h"
#include "ui_pages.h"
#include "ui_theme.h"

// Nachrichten an die Matrix-Uhren – und, wenn in der Weboberfläche
// freigeschaltet, in die Signal-Familiengruppe.
//
// Links die vorgefertigten Texte, rechts die Auswahl des Ziels und das Feld
// für eine freie Nachricht.
//
// „Familie (Signal)" ist ein weiteres Ziel auf demselben Knopf. Mehr kann das
// Gerät mit Signal nicht: Es gibt keinen Empfänger zur Auswahl, keine Kamera
// und keinen Befehl – der Pi schickt den Text unter seinem eigenen Konto in
// die fest eingestellte Gruppe.
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
tablet_data::SignalTarget signalTarget;

// -1 = an alle Uhren. 0 … n-1 = die Nummer in `clocks`. n = Signal-
// Familiengruppe (nur, wenn `signalTarget.family`).
int target = -1;
int pendingPreset = -1;
String pendingText;
bool pendingSignal = false;
bool loaded = false;

bool isSignalTarget() {
    return signalTarget.family && target == static_cast<int>(clocks.size());
}

String targetId() {
    if (target < 0 || target >= static_cast<int>(clocks.size())) return String();
    return clocks[target].id;
}

String targetName() {
    if (isSignalTarget()) return "Familie (Signal)";
    if (clocks.empty()) return "keine Uhr";
    if (target < 0) return "alle Uhren";
    return clocks[target].name;
}

void updateTargetButton() {
    lv_obj_t *label = lv_obj_get_child(targetButton, 0);
    if (label != nullptr) lv_label_set_text(label, targetName().c_str());
    // Eine Signal-Nachricht darf länger sein als eine Laufschrift.
    if (textArea != nullptr) {
        lv_textarea_set_max_length(textArea, isSignalTarget() ? signalTarget.textMax : 80);
    }
}

// Ein Druck schaltet auf das nächste Ziel weiter: alle Uhren, jede Uhr
// einzeln, zuletzt – falls freigeschaltet – die Signal-Familiengruppe. Bei
// zwei Uhren ist das schneller als eine Auswahlliste, und die Beschriftung
// sagt immer, wohin es geht.
void onTarget(lv_event_t *) {
    const int last = static_cast<int>(clocks.size()) + (signalTarget.family ? 1 : 0) - 1;
    if (last < 0) return;
    ++target;
    if (target > last) target = -1;
    // „alle Uhren" gibt es nur mit Uhren.
    if (target < 0 && clocks.empty()) target = 0;
    updateTargetButton();
}

void onPreset(lv_event_t *event) {
    const uint32_t index = lv_obj_get_index(lv_event_get_target(event));
    if (index >= presets.size()) return;
    pendingPreset = static_cast<int>(index);
    // Das Ziel JETZT festhalten: Gesendet wird in einer anderen Aufgabe, und
    // bis dahin könnte jemand den Knopf weitergeschaltet haben.
    pendingSignal = isSignalTarget();
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
    pendingSignal = isSignalTarget();
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
            message = tablet_data::fetchClocks(clocks, presets, signalTarget);
            loaded = message.isEmpty();
            {
                const int last = static_cast<int>(clocks.size()) + (signalTarget.family ? 1 : 0) - 1;
                if (target > last) target = -1;
                if (target < 0 && clocks.empty() && signalTarget.family) target = 0;
            }
            lvgl_port_lock(-1);
            rebuild();
            lv_label_set_text(status,
                !message.isEmpty() ? message.c_str()
                : (clocks.empty() && !signalTarget.family) ? "Keine Uhr eingerichtet"
                : presets.empty() ? "Keine Texte – in der Weboberfläche anlegen"
                : (String(presets.size()) + " Texte · " + String(clocks.size()) + " Uhren").c_str());
            lvgl_port_unlock();
            return;
        }
        case Job::SendPreset:
            if (pendingSignal) {
                // Signal kennt die Nummern der Liste nicht – der Text selbst geht mit.
                message = (pendingPreset >= 0 && pendingPreset < static_cast<int>(presets.size()))
                    ? tablet_data::sendSignalFamily(presets[pendingPreset])
                    : String("Text nicht vorhanden");
            } else {
                message = tablet_data::sendPreset(targetId(), pendingPreset);
            }
            break;
        case Job::SendText:
            message = pendingSignal ? tablet_data::sendSignalFamily(pendingText)
                                    : tablet_data::sendClockText(targetId(), pendingText);
            break;
        case Job::None:
            return;
    }

    lvgl_port_lock(-1);
    if (message.isEmpty()) {
        lv_label_set_text(status, pendingSignal ? "Gesendet an Familie (Signal)"
                                                : ("Gesendet an " + targetName()).c_str());
        // Nach dem Senden das Feld leeren: Der nächste Text fängt neu an.
        if (current == Job::SendText) lv_textarea_set_text(textArea, "");
    } else {
        lv_label_set_text(status, message.c_str());
    }
    lvgl_port_unlock();
}

}  // namespace clocks
}  // namespace ui_pages
