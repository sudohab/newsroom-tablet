#include <vector>
#include "fonts/ui_fonts.h"
#include "lvgl_port/lvgl_v8_port.h"
#include "tablet_data.h"
#include "ui_pages.h"
#include "ui_theme.h"

// Ein gemeinsamer Store am Pi. Die Revision wird beim Antippen festgehalten:
// Eine inzwischen am Handy geänderte Zeile darf der Touch nicht überschreiben.
// Netzwerkaufrufe ausschließlich in work(), niemals im LVGL-Ereignisthread.
namespace ui_pages { namespace shopping { namespace {
using namespace ui_theme;
lv_obj_t *page, *status, *list, *textArea, *keyboard, *addButton, *targetButton;
tablet_data::ShoppingList current;
tablet_data::ShoppingItem editing, pendingItem;
tablet_data::ShoppingAction pendingAction = tablet_data::ShoppingAction::Add;
String pendingText, pendingTarget;
String selectedTarget;
bool busy = false, mutate = false, reload = true;
uint32_t lastPoll = 0;

void updateTarget() {
    String name = "Kein Signal-Ziel";
    if (!current.targets.empty()) {
        auto found = current.targets.begin();
        for (auto it = current.targets.begin(); it != current.targets.end(); ++it) if (it->id == selectedTarget) found = it;
        selectedTarget = found->id; name = found->label;
    } else selectedTarget = "";
    lv_label_set_text(lv_obj_get_child(targetButton, 0), name.c_str());
}
void schedule(tablet_data::ShoppingAction action, const tablet_data::ShoppingItem &item = {}) {
    if (busy) return;
    busy = true; mutate = true; pendingAction = action; pendingItem = item;
    pendingText = lv_textarea_get_text(textArea); pendingText.trim();
    pendingTarget = selectedTarget;
    lv_obj_add_state(textArea, LV_STATE_DISABLED);
    lv_label_set_text(status, action == tablet_data::ShoppingAction::Send ? "Sende …" : "Speichere …");
}
void add(lv_event_t *) {
    schedule(editing.id.isEmpty() ? tablet_data::ShoppingAction::Add : tablet_data::ShoppingAction::Edit, editing);
}
void cancel(lv_event_t *) {
    if (busy) return;
    editing = {}; lv_textarea_set_text(textArea, "");
    lv_label_set_text(lv_obj_get_child(addButton, 0), "Hinzufügen");
}
void key(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_READY) { lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN); add(nullptr); }
    else if (lv_event_get_code(event) == LV_EVENT_CANCEL) lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}
void focus(lv_event_t *) { if (!busy) { lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(keyboard); } }
void target(lv_event_t *) {
    if (busy || current.targets.empty()) return;
    size_t index = 0;
    for (size_t i = 0; i < current.targets.size(); ++i) if (current.targets[i].id == selectedTarget) index = (i + 1) % current.targets.size();
    selectedTarget = current.targets[index].id; updateTarget();
}
void send(lv_event_t *) { if (!selectedTarget.isEmpty()) schedule(tablet_data::ShoppingAction::Send); }
void check(lv_event_t *event) {
    size_t i = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    if (i < current.items.size()) schedule(tablet_data::ShoppingAction::Check, current.items[i]);
}
void remove(lv_event_t *event) {
    size_t i = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    if (i < current.items.size()) schedule(tablet_data::ShoppingAction::Delete, current.items[i]);
}
void edit(lv_event_t *event) {
    if (busy) return;
    size_t i = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    if (i >= current.items.size()) return;
    editing = current.items[i]; lv_textarea_set_text(textArea, editing.text.c_str());
    lv_label_set_text(lv_obj_get_child(addButton, 0), "Speichern"); focus(nullptr);
}
void render() {
    lv_obj_clean(list);
    for (size_t i = 0; i < current.items.size(); ++i) {
        const auto &item = current.items[i];
        auto row = lv_obj_create(list);
        lv_obj_set_size(row, 715, 64); lv_obj_set_style_pad_all(row, 6, 0);
        lv_obj_set_style_border_width(row, 0, 0); lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        auto label = lv_label_create(row);
        lv_obj_set_width(label, 400); lv_obj_set_style_text_font(label, &ui_font_18, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(item.checked ? kTextMuted : kText), 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP); lv_label_set_text(label, item.text.c_str());
        auto b = makeButton(row, item.checked ? "Offen" : "OK", check, 80, 44, LV_ALIGN_TOP_LEFT, 410, 0, kAccent);
        lv_obj_remove_event_cb(b, check); lv_obj_add_event_cb(b, check, LV_EVENT_CLICKED, reinterpret_cast<void *>(i));
        b = makeButton(row, "Ändern", edit, 90, 44, LV_ALIGN_TOP_LEFT, 495, 0, kAccent);
        lv_obj_remove_event_cb(b, edit); lv_obj_add_event_cb(b, edit, LV_EVENT_CLICKED, reinterpret_cast<void *>(i));
        b = makeButton(row, "Weg", remove, 75, 44, LV_ALIGN_TOP_LEFT, 590, 0, kTextMuted);
        lv_obj_remove_event_cb(b, remove); lv_obj_add_event_cb(b, remove, LV_EVENT_CLICKED, reinterpret_cast<void *>(i));
    }
    updateTarget();
}
} // namespace
lv_obj_t *create(lv_obj_t *parent) {
    page = makeSection(parent, 0, 0, 752, 300); makeTitle(page, "Einkaufsliste"); status = makeStatus(page);
    textArea = lv_textarea_create(page); lv_obj_set_size(textArea, 510, 44); lv_obj_set_pos(textArea, 0, 46);
    lv_textarea_set_one_line(textArea, true); lv_textarea_set_max_length(textArea, 80);
    lv_textarea_set_placeholder_text(textArea, "Artikel und Menge"); lv_obj_set_style_text_font(textArea, &ui_font_22, 0);
    lv_obj_add_event_cb(textArea, focus, LV_EVENT_FOCUSED, nullptr); lv_obj_add_event_cb(textArea, focus, LV_EVENT_CLICKED, nullptr);
    addButton = makeButton(page, "Hinzufügen", add, 130, 44, LV_ALIGN_TOP_LEFT, 520, 46, kAccent);
    makeButton(page, "Zurück", cancel, 90, 44, LV_ALIGN_TOP_LEFT, 660, 46, kTextMuted);
    list = lv_obj_create(page); lv_obj_set_size(list, 752, 145); lv_obj_set_pos(list, 0, 100);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_border_width(list, 0, 0); lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    targetButton = makeButton(page, "Kein Signal-Ziel", target, 480, 44, LV_ALIGN_TOP_LEFT, 0, 253, kAccent);
    makeButton(page, "Per Signal senden", send, 255, 44, LV_ALIGN_TOP_LEFT, 495, 253, kAccent);
    keyboard = lv_keyboard_create(lv_obj_get_screen(parent)); lv_obj_set_size(keyboard, 800, 230);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0); lv_keyboard_set_textarea(keyboard, textArea);
    lv_obj_add_event_cb(keyboard, key, LV_EVENT_READY, nullptr); lv_obj_add_event_cb(keyboard, key, LV_EVENT_CANCEL, nullptr);
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN); return page;
}
void activate() { reload = true; }
void deactivate() { if (keyboard) lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN); }
void work() {
    lvgl_port_lock(-1);
    const bool writing = mutate;
    const auto action = pendingAction; const auto item = pendingItem;
    const String text = pendingText, destination = pendingTarget;
    if (!writing && !reload && millis() - lastPoll < 10000) { lvgl_port_unlock(); return; }
    mutate = false; reload = false; lastPoll = millis(); lvgl_port_unlock();
    String error;
    if (writing) error = tablet_data::changeShopping(action, item, text, !item.checked, destination);
    tablet_data::ShoppingList next;
    const String fetchError = tablet_data::fetchShopping(next);
    lvgl_port_lock(-1);
    if (fetchError.isEmpty()) { current = std::move(next); render(); }
    if (writing) {
        busy = false; lv_obj_clear_state(textArea, LV_STATE_DISABLED);
        if (error.isEmpty() && (action == tablet_data::ShoppingAction::Add || action == tablet_data::ShoppingAction::Edit)) cancel(nullptr);
    }
    String message = !error.isEmpty() ? error : !fetchError.isEmpty() ? fetchError
        : writing ? (action == tablet_data::ShoppingAction::Send ? String("Liste gesendet") : String("Gespeichert"))
        : String(current.count) + " offen";
    lv_label_set_text(status, message.c_str()); lvgl_port_unlock();
}
} } // namespace ui_pages::shopping
