#include "ui_theme.h"

#include "fonts/ui_fonts.h"

namespace ui_theme {
namespace {
void styleButton(lv_obj_t *btn, uint32_t color);
void addButtonLabel(lv_obj_t *btn, const char *text);
}  // namespace

void applyBackground(lv_obj_t *screen) {
    lv_obj_set_style_bg_color(screen, lv_color_hex(kBackgroundTop), 0);
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(kBackgroundBottom), 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *makeCard(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                   lv_coord_t w, lv_coord_t h) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_pos(card, x, y);

    // Milchglas: helle Fläche mit geringer Deckkraft
    lv_obj_set_style_bg_color(card, lv_color_hex(kGlass), 0);
    lv_obj_set_style_bg_opa(card, kGlassOpa, 0);
    // Ein zarter Verlauf innerhalb der Fläche lässt sie plastisch wirken,
    // so wie Licht, das von oben auf eine Scheibe fällt.
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);

    lv_obj_set_style_radius(card, 22, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(kGlass), 0);
    lv_obj_set_style_border_opa(card, kGlassBorderOpa, 0);

    // Weicher Schatten nach unten – hebt die Fläche vom Grund ab.
    lv_obj_set_style_shadow_width(card, 24, 0);
    lv_obj_set_style_shadow_ofs_y(card, 6, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(card, 70, 0);

    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
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
    addButtonLabel(btn, text);
    return btn;
}

// Ein Knopf in einer Leiste: Position bestimmt die Leiste (Flex), deshalb
// wird hier ausdrücklich nicht ausgerichtet. So können sich zwei Knöpfe auch
// dann nicht überlappen, wenn sich Größen oder Beschriftungen ändern.
lv_obj_t *addBarButton(lv_obj_t *bar, const char *text, lv_event_cb_t handler,
                       lv_coord_t w, lv_coord_t h, uint32_t color) {
    lv_obj_t *btn = lv_btn_create(bar);
    lv_obj_set_size(btn, w, h);
    lv_obj_add_event_cb(btn, handler, LV_EVENT_CLICKED, nullptr);
    styleButton(btn, color);
    addButtonLabel(btn, text);
    return btn;
}

namespace {

void styleButton(lv_obj_t *btn, uint32_t color) {
    const bool neutral = (color == 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(neutral ? kGlass : color), 0);
    // Farbige Knöpfe sind kräftiger, neutrale bleiben gläsern.
    lv_obj_set_style_bg_opa(btn, neutral ? kGlassOpa : LV_OPA_80, 0);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(kGlass), 0);
    lv_obj_set_style_border_opa(btn, kGlassBorderOpa, 0);
    lv_obj_set_style_shadow_width(btn, 12, 0);
    lv_obj_set_style_shadow_ofs_y(btn, 3, 0);
    lv_obj_set_style_shadow_opa(btn, 60, 0);

    // Gedrückt: heller und leicht eingesunken – eine Rückmeldung, die man
    // auch ohne Ton bemerkt.
    lv_obj_set_style_bg_opa(btn, neutral ? 90 : LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_translate_y(btn, 2, LV_STATE_PRESSED);
    // Ausgegraut: sichtbar, aber erkennbar nicht benutzbar.
    lv_obj_set_style_bg_opa(btn, 12, LV_STATE_DISABLED);
    lv_obj_set_style_text_opa(btn, LV_OPA_50, LV_STATE_DISABLED);
}

void addButtonLabel(lv_obj_t *btn, const char *text) {
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &ui_font_22, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(kText), 0);
    lv_obj_center(label);
}

}  // namespace

lv_obj_t *makeButtonBar(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                        lv_coord_t w, lv_coord_t h) {
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, w, h);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

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
    lv_obj_t *area = lv_obj_create(parent);
    lv_obj_set_size(area, w, h);
    lv_obj_set_pos(area, x, y);
    lv_obj_set_style_bg_opa(area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(area, 0, 0);
    lv_obj_set_style_pad_all(area, 0, 0);
    lv_obj_set_style_pad_right(area, 10, 0);   // Platz für die Bildlaufleiste

    // Untereinander, mit Luft dazwischen
    lv_obj_set_flex_flow(area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(area, 12, 0);

    // Nur senkrecht scrollen, mit Schwung – und die Liste rastet nicht ein,
    // damit auch ein kurzer Wisch etwas bewegt.
    lv_obj_set_scroll_dir(area, LV_DIR_VER);
    lv_obj_add_flag(area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(area, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(area, lv_color_hex(kGlass), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(area, 80, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(area, 6, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(area, 3, LV_PART_SCROLLBAR);
    return area;
}

}  // namespace ui_theme
