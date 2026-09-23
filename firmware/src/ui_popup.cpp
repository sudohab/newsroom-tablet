#include "ui_popup.h"

#include <lvgl.h>

#include "fonts/ui_fonts.h"
#include "lvgl_port/lvgl_v8_port.h"
#include "tablet_data.h"
#include "tablet_state.h"
#include "ui_theme.h"

namespace ui_popup {
namespace {

using namespace ui_theme;

// Das Fenster deckt nicht den ganzen Bildschirm ab: Die Kopfzeile mit Uhr und
// Datum bleibt sichtbar. Wer nachts geweckt wird, will zuerst wissen, wie spät
// es ist.
constexpr lv_coord_t kWidth = 560;
constexpr lv_coord_t kHeight = 260;

enum class Kind { None, Call, Timer };
Kind shown = Kind::None;

lv_obj_t *box = nullptr;
lv_obj_t *labelKind = nullptr;
lv_obj_t *labelMain = nullptr;
lv_obj_t *labelDetail = nullptr;
lv_obj_t *buttonOff = nullptr;

// Der Timer, der gerade gezeigt wird – zum Abstellen.
String shownTimerId;
String pendingStopId;
volatile bool stopRequested = false;

// Ein Anruf wird nur angezeigt, nicht bedient: Das Display hat kein Mikrofon,
// und den Hörer nimmt man am Telefon ab. Deshalb hat dieses Fenster nur einen
// „Weg"-Knopf, der es wegtippt, bis der nächste Anruf kommt.
String dismissedCall;

void hide() {
    lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);
    shown = Kind::None;
    shownTimerId = "";
}

void onOff(lv_event_t *) {
    if (shown == Kind::Timer && !shownTimerId.isEmpty()) {
        pendingStopId = shownTimerId;
        stopRequested = true;
        // Das Fenster bleibt stehen, bis der Pi bestätigt hat – sonst sähe es
        // aus, als wäre der Ton aus, während er noch läuft.
        lv_label_set_text(labelDetail, "Stelle ab …");
        return;
    }
    dismissedCall = tablet_state::current().callText;
    hide();
}

// „05:00" – dieselbe Schreibweise wie auf der Timer-Seite.
String clockText(int seconds) {
    if (seconds < 0) seconds = 0;
    char buffer[16];
    if (seconds >= 3600) {
        snprintf(buffer, sizeof(buffer), "%d:%02d:%02d",
                 seconds / 3600, (seconds % 3600) / 60, seconds % 60);
    } else {
        snprintf(buffer, sizeof(buffer), "%02d:%02d", seconds / 60, seconds % 60);
    }
    return String(buffer);
}

void show(Kind kind, const char *caption, const String &main, const String &detail,
          uint32_t color, const char *buttonText) {
    lv_obj_set_style_border_color(box, lv_color_hex(color), 0);
    lv_obj_set_style_text_color(labelKind, lv_color_hex(color), 0);
    lv_label_set_text(labelKind, caption);
    lv_label_set_text(labelMain, main.c_str());
    lv_label_set_text(labelDetail, detail.c_str());

    lv_obj_t *label = lv_obj_get_child(buttonOff, 0);
    if (label != nullptr) lv_label_set_text(label, buttonText);

    lv_obj_clear_flag(box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(box);
    shown = kind;
}

}  // namespace

void begin(lv_obj_t *screen) {
    box = lv_obj_create(screen);
    lv_obj_set_size(box, kWidth, kHeight);
    lv_obj_align(box, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_bg_color(box, lv_color_hex(kSurface), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 16, 0);
    // Der einzige Rahmen im ganzen Gerät. Hier ist er richtig: Er trennt das
    // Fenster von der Seite darunter, und seine Farbe sagt schon von weitem,
    // worum es geht – rot beim Timer, grün beim Anruf.
    lv_obj_set_style_border_width(box, 3, 0);
    lv_obj_set_style_shadow_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 24, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    labelKind = makeLabel(box, &ui_font_18, kAlarm, LV_ALIGN_TOP_LEFT, 0, 0, "");

    labelMain = lv_label_create(box);
    lv_obj_set_style_text_font(labelMain, &ui_font_56, 0);
    lv_obj_set_style_text_color(labelMain, lv_color_hex(kText), 0);
    lv_obj_set_pos(labelMain, 0, 28);
    lv_obj_set_size(labelMain, kWidth - 48, 64);
    lv_label_set_long_mode(labelMain, LV_LABEL_LONG_DOT);
    lv_label_set_text(labelMain, "");

    labelDetail = lv_label_create(box);
    lv_obj_set_style_text_font(labelDetail, &ui_font_22, 0);
    lv_obj_set_style_text_color(labelDetail, lv_color_hex(kTextMuted), 0);
    lv_obj_set_pos(labelDetail, 0, 100);
    lv_obj_set_size(labelDetail, kWidth - 48, 56);
    lv_label_set_long_mode(labelDetail, LV_LABEL_LONG_DOT);
    lv_label_set_text(labelDetail, "");

    buttonOff = makeButton(box, "Aus", onOff, kWidth - 48, 50,
                           LV_ALIGN_BOTTOM_MID, 0, 0, kAlarm);

    lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);
}

void update() {
    const tablet_state::Snapshot &state = tablet_state::current();

    // Ein neuer Anruf hebt ein weggetipptes Fenster wieder auf.
    if (state.callText != dismissedCall) dismissedCall = "";

    // Anruf geht vor: Ein Timer lässt sich später noch abstellen, ein Anruf
    // nicht.
    if (!state.callText.isEmpty() && state.callText != dismissedCall) {
        if (shown != Kind::Call) {
            show(Kind::Call, "ANRUF", state.callText, "", kCall, "Weg");
        }
        return;
    }

    if (state.timerExpired > 0) {
        if (shown != Kind::Timer || shownTimerId != state.timerId) {
            shownTimerId = state.timerId;
            String detail = "Timer über " + clockText(state.timerDuration);
            if (state.timerExpired > 1) {
                detail += "  ·  noch " + String(state.timerExpired - 1) + " weitere";
            }
            show(Kind::Timer, "TIMER ABGELAUFEN",
                 state.timerLabel.isEmpty() ? String("Fertig") : state.timerLabel,
                 detail, kAlarm, "Aus");
        }
        return;
    }

    // Kein Anlass mehr – der Pi hat bestätigt, dass nichts mehr klingelt.
    if (shown != Kind::None) hide();
}

void work() {
    if (!stopRequested) return;
    stopRequested = false;

    const String error = tablet_data::stopTimer(pendingStopId);
    if (!error.isEmpty()) {
        lvgl_port_lock(-1);
        lv_label_set_text(labelDetail, error.c_str());
        lvgl_port_unlock();
    }
    // Bei Erfolg wird nichts weiter getan: Die nächste Zustandsabfrage meldet
    // „kein Timer klingelt mehr", und `update()` blendet das Fenster aus.
}

}  // namespace ui_popup
