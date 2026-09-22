#include "ui_theme.h"

#include "fonts/ui_fonts.h"

namespace ui_theme {
namespace {

// Gemeinsames Aussehen der Knöpfe. Bewusst ohne Rahmen, Schatten und Verlauf:
// Eine deckende Fläche wird vom Prozessor einfach geschrieben, alles andere
// muss er ausrechnen.
void styleButton(lv_obj_t *btn, uint32_t color) {
    const bool neutral = (color == 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(neutral ? kSurface : color), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_NONE, 0);
    // Runde Ecken bleiben – das ist der eine Zierrat des flachen Stils.
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    // Gedrückt: hellere Fläche. Eine Farbänderung kostet nichts zusätzlich.
    lv_obj_set_style_bg_color(btn, lv_color_hex(neutral ? kSurfacePressed : color),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, neutral ? LV_OPA_COVER : LV_OPA_70, LV_STATE_PRESSED);
    // Ausgegraut: dunkle Fläche, blasse Schrift.
    lv_obj_set_style_bg_color(btn, lv_color_hex(kSurface), LV_STATE_DISABLED);
    lv_obj_set_style_text_opa(btn, LV_OPA_40, LV_STATE_DISABLED);
}

void addButtonLabel(lv_obj_t *btn, const char *text, uint32_t color) {
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &ui_font_22, 0);
    // Auf neutraler Fläche ist die Schrift farbig – so sieht man auch ohne
    // Rahmen, dass es ein Bedienelement ist.
    lv_obj_set_style_text_color(label, lv_color_hex(color == 0 ? kAccent : kText), 0);
    lv_obj_center(label);
}

}  // namespace

void applyBackground(lv_obj_t *screen) {
    // Eine einzige deckende Farbe, kein Verlauf: Ein Verlauf müsste bei jedem
    // freigelegten Stück neu berechnet werden.
    lv_obj_set_style_bg_color(screen, lv_color_hex(kBackground), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *makeSection(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                      lv_coord_t w, lv_coord_t h) {
    lv_obj_t *section = lv_obj_create(parent);
    lv_obj_set_size(section, w, h);
    lv_obj_set_pos(section, x, y);
    // Zeichnet selbst nichts, ordnet nur an – gezeichnet wird allein der Inhalt.
    lv_obj_set_style_bg_opa(section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(section, 0, 0);
    lv_obj_set_style_shadow_width(section, 0, 0);
    lv_obj_set_style_pad_all(section, 0, 0);
    lv_obj_clear_flag(section, LV_OBJ_FLAG_SCROLLABLE);
    return section;
}

lv_obj_t *makeSeparator(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                        lv_coord_t w, lv_coord_t h) {
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_set_size(line, w, h);
    lv_obj_set_pos(line, x, y);
    lv_obj_set_style_bg_color(line, lv_color_hex(kSeparator), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_set_style_shadow_width(line, 0, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    return line;
}

lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                    lv_align_t align, lv_coord_t x, lv_coord_t y,
                    const char *text) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text);
    lv_obj_align(label, align, x, y);
    return label;
}

lv_obj_t *makeButton(lv_obj_t *parent, const char *text, lv_event_cb_t handler,
                     lv_coord_t w, lv_coord_t h,
                     lv_align_t align, lv_coord_t x, lv_coord_t y,
                     uint32_t color) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_align(btn, align, x, y);
    lv_obj_add_event_cb(btn, handler, LV_EVENT_CLICKED, nullptr);
    styleButton(btn, color);
    addButtonLabel(btn, text, color);
    return btn;
}

lv_obj_t *addBarButton(lv_obj_t *bar, const char *text, lv_event_cb_t handler,
                       lv_coord_t w, lv_coord_t h, uint32_t color) {
    // Position bestimmt die Leiste (Flex), deshalb hier kein Ausrichten: So
    // können sich zwei Knöpfe auch dann nicht überlappen, wenn sich Größen
    // oder Beschriftungen ändern.
    lv_obj_t *btn = lv_btn_create(bar);
    lv_obj_set_size(btn, w, h);
    lv_obj_add_event_cb(btn, handler, LV_EVENT_CLICKED, nullptr);
    styleButton(btn, color);
    addButtonLabel(btn, text, color);
    return btn;
}

lv_obj_t *makeButtonBar(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                        lv_coord_t w, lv_coord_t h) {
    lv_obj_t *bar = makeSection(parent, x, y, w, h);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    // Von links auffüllen, senkrecht mittig; zwischen den Knöpfen 10 Pixel.
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 10, 0);
    return bar;
}

lv_obj_t *addBarSpacer(lv_obj_t *bar) {
    lv_obj_t *spacer = lv_obj_create(bar);
    lv_obj_set_height(spacer, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);
    // Nimmt allen Platz, der übrig bleibt.
    lv_obj_set_flex_grow(spacer, 1);
    return spacer;
}

lv_obj_t *makeScrollArea(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                         lv_coord_t w, lv_coord_t h) {
    lv_obj_t *area = makeSection(parent, x, y, w, h);
    lv_obj_set_style_pad_right(area, 10, 0);   // Platz für die Bildlaufleiste

    // Untereinander, mit Luft dazwischen
    lv_obj_set_flex_flow(area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(area, 14, 0);

    lv_obj_set_scroll_dir(area, LV_DIR_VER);
    lv_obj_add_flag(area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(area, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(area, lv_color_hex(kGlass), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(area, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(area, 4, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(area, 2, LV_PART_SCROLLBAR);
    return area;
}

void styleListButton(lv_obj_t *btn) {
    lv_obj_set_style_text_font(btn, &ui_font_22, 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(kText), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(kSurface), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(kSurfacePressed), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_pad_all(btn, 12, 0);
}

}  // namespace ui_theme
