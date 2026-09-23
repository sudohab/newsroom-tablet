#include <vector>

#include "fonts/ui_fonts.h"
#include "lvgl_port/lvgl_v8_port.h"
#include "tablet_data.h"
#include "ui_pages.h"
#include "ui_theme.h"

// Die Timer-Seite: vier Kurzzeitwecker nebeneinander, alle gleichzeitig
// sichtbar und einzeln bedienbar.
//
// Gezählt wird im Pi, nicht hier. Das Gerät holt sich Restzeit und Zustand und
// zählt zwischen zwei Abrufen selbst herunter – so steht die Anzeige im
// Sekundentakt, ohne dass im Sekundentakt gefragt wird. Alle zehn Sekunden
// und nach jedem Knopfdruck zieht sie sich am Pi wieder gerade.
//
// Ein Platz hat zwei Leben:
//
//   • **leer** – dann ist die große Zahl eine Dauer, die man mit „+1" und
//     „+5" aufbaut. „Start" legt den Timer damit an und startet ihn in einem
//     Zug.
//   • **belegt** – dann ist die große Zahl die Restzeit, und die Knöpfe
//     richten sich nach dem Zustand (läuft / pausiert / abgelaufen).
//
// „Wdh" (Wiederholen) heißt: Nach dem Abstellen läuft der Timer sofort wieder
// von vorn. Es startet ihn nicht beim Ablaufen von selbst neu – sonst liefe
// der nächste Durchgang, während der Ton noch klingelt.

namespace ui_pages {
namespace timers {
namespace {

using namespace ui_theme;

constexpr lv_coord_t kWidth = 752;
constexpr lv_coord_t kHeight = 300;
constexpr int kSlots = 4;
constexpr lv_coord_t kGap = 8;
constexpr lv_coord_t kSlotWidth = (kWidth - (kSlots - 1) * kGap) / kSlots;   // 182
constexpr lv_coord_t kSlotTop = 48;
constexpr lv_coord_t kButtonHeight = 40;
constexpr lv_coord_t kHalfWidth = (kSlotWidth - kGap) / 2;

// Alle zehn Sekunden beim Pi nachfragen. Dazwischen zählt das Gerät selbst.
constexpr uint32_t kRefreshMs = 10000;

enum class Job { None, Load, Create, Start, Pause, Stop, Delete, Repeat };
volatile Job job = Job::None;

lv_obj_t *page = nullptr;
lv_obj_t *status = nullptr;

// Ein Platz auf dem Bildschirm. Die Bedienelemente werden EINMAL angelegt und
// danach nur noch ein- und ausgeblendet – ein Neuaufbau bei jedem Zustands-
// wechsel würde die Anzeige unruhig machen.
struct Slot {
    lv_obj_t *box = nullptr;
    lv_obj_t *heading = nullptr;
    lv_obj_t *time = nullptr;
    lv_obj_t *addBar = nullptr;      // [+1] [+5]   (nur bei leerem Platz)
    lv_obj_t *primary = nullptr;     // Start / Pause / Weiter / Aus
    lv_obj_t *secondary = nullptr;   // Stopp / Zurück
    lv_obj_t *bottomBar = nullptr;   // [Wdh] [Löschen]
    lv_obj_t *repeatButton = nullptr;
    lv_obj_t *deleteButton = nullptr;
};
Slot slots[kSlots];

std::vector<tablet_data::Timer> timers;
int maxTimers = kSlots;

// Dauer, die an einem leeren Platz zusammengeklickt wurde (Sekunden).
int pending[kSlots] = {0, 0, 0, 0};

int jobSlot = -1;
uint32_t lastFetchMs = 0;
uint32_t lastCountMs = 0;
bool loaded = false;

// --- kleine Helfer ----------------------------------------------------------

// Der Timer, der auf diesem Platz liegt – oder nullptr, wenn der Platz frei
// ist. Die Reihenfolge der Liste bestimmt die Plätze.
const tablet_data::Timer *timerAt(int slot) {
    if (slot < 0 || slot >= static_cast<int>(timers.size())) return nullptr;
    return &timers[slot];
}

// „05:00" oder „1:05:00" – Stunden nur, wenn es welche gibt.
String clockText(int seconds) {
    if (seconds < 0) seconds = 0;
    const int hours = seconds / 3600;
    const int minutes = (seconds % 3600) / 60;
    const int secs = seconds % 60;
    char buffer[16];
    if (hours > 0) {
        snprintf(buffer, sizeof(buffer), "%d:%02d:%02d", hours, minutes, secs);
    } else {
        snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes, secs);
    }
    return String(buffer);
}

void setButton(lv_obj_t *btn, const char *text, bool visible, uint32_t color) {
    if (btn == nullptr) return;
    if (!visible) {
        lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color == 0 ? kSurface : color), 0);
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    if (label != nullptr) {
        lv_label_set_text(label, text);
        lv_obj_set_style_text_color(label, lv_color_hex(color == 0 ? kAccent : kText), 0);
    }
}

// --- Knopfdrücke ------------------------------------------------------------
//
// Jeder Knopf kennt seinen Platz über die Nutzerdaten. Ausgeführt wird der
// Wunsch in `work()`, außerhalb der LVGL-Sperre.

int slotOf(lv_event_t *event) {
    return static_cast<int>(reinterpret_cast<intptr_t>(
        lv_obj_get_user_data(lv_event_get_target(event))));
}

void redraw();   // weiter unten

void onAdd(lv_event_t *event, int seconds) {
    const int slot = slotOf(event);
    if (slot < 0 || slot >= kSlots || timerAt(slot) != nullptr) return;
    pending[slot] = min(pending[slot] + seconds, 24 * 3600);
    redraw();
}
void onAdd1(lv_event_t *event) { onAdd(event, 60); }
void onAdd5(lv_event_t *event) { onAdd(event, 300); }

void onPrimary(lv_event_t *event) {
    const int slot = slotOf(event);
    if (slot < 0 || slot >= kSlots) return;
    jobSlot = slot;

    const tablet_data::Timer *timer = timerAt(slot);
    if (timer == nullptr) {
        if (pending[slot] < 10) return;   // unter zehn Sekunden nimmt der Pi nichts
        job = Job::Create;
    } else if (timer->state == "running") {
        job = Job::Pause;
    } else if (timer->state == "expired") {
        job = Job::Stop;                  // „Aus" – stellt den Ton ab
    } else {
        job = Job::Start;                 // idle oder pausiert
    }
}

void onSecondary(lv_event_t *event) {
    const int slot = slotOf(event);
    if (slot < 0 || slot >= kSlots) return;
    jobSlot = slot;

    if (timerAt(slot) == nullptr) {
        pending[slot] = 0;                // „Zurück" am leeren Platz
        redraw();
        return;
    }
    job = Job::Stop;
}

void onDelete(lv_event_t *event) {
    const int slot = slotOf(event);
    if (slot < 0 || slot >= kSlots || timerAt(slot) == nullptr) return;
    jobSlot = slot;
    job = Job::Delete;
}

void onRepeat(lv_event_t *event) {
    const int slot = slotOf(event);
    if (slot < 0 || slot >= kSlots || timerAt(slot) == nullptr) return;
    jobSlot = slot;
    job = Job::Repeat;
}

// --- Anzeige ----------------------------------------------------------------

// Nur die vier Zeitanzeigen nachziehen. Das passiert jede Sekunde, deshalb
// bleibt es bewusst bei vier Beschriftungen – alles andere ändert sich nur
// nach einem Knopfdruck.
void redrawClocks() {
    for (int i = 0; i < kSlots; ++i) {
        const tablet_data::Timer *timer = timerAt(i);
        const int seconds = (timer != nullptr) ? timer->remaining : pending[i];
        lv_label_set_text(slots[i].time, clockText(seconds).c_str());

        uint32_t color = kText;
        if (timer == nullptr) {
            color = (pending[i] > 0) ? kText : kTextMuted;
        } else if (timer->state == "expired") {
            color = kAlarm;
        } else if (timer->state == "paused") {
            color = kTextMuted;
        }
        lv_obj_set_style_text_color(slots[i].time, lv_color_hex(color), 0);
    }
}

// Den ganzen Platz neu beschriften – nach jedem Zustandswechsel.
void redraw() {
    for (int i = 0; i < kSlots; ++i) {
        Slot &slot = slots[i];
        const tablet_data::Timer *timer = timerAt(i);

        String heading = "Timer " + String(i + 1);
        if (timer != nullptr && !timer->label.isEmpty()) heading = timer->label;
        if (timer == nullptr) heading += "  frei";
        else if (timer->state == "running") heading += "  läuft";
        else if (timer->state == "paused") heading += "  Pause";
        else if (timer->state == "expired") heading += "  fertig";
        lv_label_set_text(slot.heading, heading.c_str());

        const bool empty = (timer == nullptr);
        // Die Plus-Knöpfe gehören zum leeren Platz: Dort baut man die Dauer
        // auf. Bei einem angelegten Timer steht die Dauer fest.
        if (empty) lv_obj_clear_flag(slot.addBar, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(slot.addBar, LV_OBJ_FLAG_HIDDEN);

        if (empty) {
            setButton(slot.primary, "Start", pending[i] >= 10, kAccent);
            setButton(slot.secondary, "Zurück", pending[i] > 0, 0);
        } else if (timer->state == "running") {
            setButton(slot.primary, "Pause", true, 0);
            setButton(slot.secondary, "Stopp", true, 0);
        } else if (timer->state == "paused") {
            setButton(slot.primary, "Weiter", true, kAccent);
            setButton(slot.secondary, "Stopp", true, 0);
        } else if (timer->state == "expired") {
            setButton(slot.primary, "Aus", true, kAlarm);
            setButton(slot.secondary, "Stopp", false, 0);
        } else {
            setButton(slot.primary, "Start", true, kAccent);
            setButton(slot.secondary, "Stopp", false, 0);
        }

        if (empty) {
            lv_obj_add_flag(slot.bottomBar, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(slot.bottomBar, LV_OBJ_FLAG_HIDDEN);
            setButton(slot.repeatButton, "Wdh", true, timer->repeat ? kAccent : 0);
            setButton(slot.deleteButton, "Weg", true, 0);
        }
    }
    redrawClocks();
}

lv_obj_t *addSlotButton(lv_obj_t *parent, const char *text, lv_event_cb_t handler,
                        int slot, lv_coord_t w, lv_coord_t y) {
    lv_obj_t *btn = makeButton(parent, text, handler, w, kButtonHeight,
                               LV_ALIGN_TOP_LEFT, 0, y);
    lv_obj_set_user_data(btn, reinterpret_cast<void *>(static_cast<intptr_t>(slot)));
    return btn;
}

}  // namespace

lv_obj_t *create(lv_obj_t *parent) {
    page = makeSection(parent, 0, 0, kWidth, kHeight);
    makeTitle(page, "Timer");
    status = makeStatus(page);

    for (int i = 0; i < kSlots; ++i) {
        Slot &slot = slots[i];
        const lv_coord_t x = i * (kSlotWidth + kGap);
        slot.box = makeSection(page, x, kSlotTop, kSlotWidth, kHeight - kSlotTop);
        if (i > 0) makeSeparator(page, x - kGap / 2, kSlotTop, 1, kHeight - kSlotTop);

        slot.heading = makeLabel(slot.box, &ui_font_18, kTextMuted,
                                 LV_ALIGN_TOP_LEFT, 0, 0, "");
        slot.time = makeLabel(slot.box, &ui_font_30, kTextMuted,
                              LV_ALIGN_TOP_LEFT, 0, 24, "00:00");

        slot.addBar = makeButtonBar(slot.box, 0, 66, kSlotWidth, kButtonHeight);
        lv_obj_t *plus1 = addBarButton(slot.addBar, "+1", onAdd1, kHalfWidth, kButtonHeight);
        lv_obj_t *plus5 = addBarButton(slot.addBar, "+5", onAdd5, kHalfWidth, kButtonHeight);
        for (lv_obj_t *btn : {plus1, plus5}) {
            lv_obj_set_user_data(btn, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        }

        slot.primary = addSlotButton(slot.box, "Start", onPrimary, i, kSlotWidth, 112);
        slot.secondary = addSlotButton(slot.box, "Stopp", onSecondary, i, kSlotWidth, 158);

        slot.bottomBar = makeButtonBar(slot.box, 0, 204, kSlotWidth, kButtonHeight);
        slot.repeatButton = addBarButton(slot.bottomBar, "Wdh", onRepeat,
                                         kHalfWidth, kButtonHeight);
        slot.deleteButton = addBarButton(slot.bottomBar, "Weg", onDelete,
                                         kHalfWidth, kButtonHeight);
        lv_obj_set_user_data(slot.repeatButton,
                             reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        lv_obj_set_user_data(slot.deleteButton,
                             reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    }

    redraw();
    return page;
}

void activate() {
    if (!loaded) job = Job::Load;
}

void work() {
    const uint32_t now = millis();

    // Jede Sekunde eins herunterzählen – ohne den Pi zu fragen.
    if (loaded && now - lastCountMs >= 1000) {
        lastCountMs = now;
        bool anyRunning = false;
        for (auto &timer : timers) {
            if (timer.state != "running") continue;
            anyRunning = true;
            if (timer.remaining > 0) timer.remaining--;
        }
        if (anyRunning) {
            lvgl_port_lock(-1);
            redrawClocks();
            lvgl_port_unlock();
        }
    }

    // Regelmäßig nachziehen: Ein Timer kann auch von der Weboberfläche aus
    // gestellt worden sein, und beim Ablaufen wechselt der Zustand im Pi.
    if (job == Job::None && loaded && now - lastFetchMs >= kRefreshMs) {
        job = Job::Load;
    }

    const Job current = job;
    if (current == Job::None) return;
    job = Job::None;

    const tablet_data::Timer *timer = timerAt(jobSlot);
    const String id = (timer != nullptr) ? timer->id : String();

    // Alles ausser Laden und Anlegen braucht einen Timer auf diesem Platz.
    // Er kann zwischen Knopfdruck und Ausfuehrung verschwunden sein -- etwa
    // weil die Weboberflaeche ihn geloescht hat.
    if (timer == nullptr && current != Job::Load && current != Job::Create) {
        job = Job::Load;
        return;
    }

    String message;
    bool reload = true;
    switch (current) {
        case Job::Create:
            // Anlegen und starten in einem Zug: Auf dem Gerät ist „Start" ein
            // Druck, auch wenn der Pi zwei Schritte kennt.
            message = tablet_data::createTimer(pending[jobSlot], String(), false);
            if (message.isEmpty()) {
                pending[jobSlot] = 0;
                // Der neue Timer steht am Ende der Liste – also erst holen,
                // dann starten.
                message = tablet_data::fetchTimers(timers, maxTimers);
                const tablet_data::Timer *fresh = timers.empty() ? nullptr : &timers.back();
                if (message.isEmpty() && fresh != nullptr) {
                    message = tablet_data::startTimer(fresh->id);
                }
            }
            break;
        case Job::Start:  message = tablet_data::startTimer(id); break;
        case Job::Pause:  message = tablet_data::pauseTimer(id); break;
        case Job::Stop:   message = tablet_data::stopTimer(id); break;
        case Job::Delete: message = tablet_data::deleteTimer(id); break;
        case Job::Repeat: message = tablet_data::setTimerRepeat(id, !timer->repeat); break;
        case Job::Load:   reload = false; message = tablet_data::fetchTimers(timers, maxTimers); break;
        case Job::None:   return;
    }

    if (reload && message.isEmpty()) {
        message = tablet_data::fetchTimers(timers, maxTimers);
    }
    if (current == Job::Load || message.isEmpty()) {
        loaded = true;
        lastFetchMs = millis();
    }

    lvgl_port_lock(-1);
    redraw();
    lv_label_set_text(status, message.isEmpty() ? "" : message.c_str());
    lvgl_port_unlock();
}

}  // namespace timers
}  // namespace ui_pages
