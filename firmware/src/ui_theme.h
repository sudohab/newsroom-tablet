// Gestaltung der Oberfläche: Farben, Glasflächen, Knöpfe.
//
// Gewünscht ist ein moderner, „gläserner" Look. LVGL 8 kann den Hintergrund
// nicht wirklich weichzeichnen (dafür fehlt dem Mikrocontroller die Leistung),
// aber der Eindruck entsteht auch so:
//
//   • ein dunkler Farbverlauf als Grund,
//   • Flächen in Weiß mit sehr geringer Deckkraft darüber,
//   • ein hauchdünner, hellerer Rand, der die Kante fängt,
//   • großzügige Rundungen und ein weicher Schatten.
//
// Alles an einem Ort, damit das Aussehen an einer Stelle geändert werden kann
// und nicht in jeder Seite einzeln.
#pragma once

#include <lvgl.h>

namespace ui_theme {

// --- Farben -----------------------------------------------------------------
constexpr uint32_t kBackgroundTop = 0x141821;    // Grund oben
constexpr uint32_t kBackgroundBottom = 0x0a0c11; // Grund unten
constexpr uint32_t kGlass = 0xffffff;            // Glasfläche (mit Deckkraft)
constexpr uint32_t kText = 0xf5f7fa;
constexpr uint32_t kTextMuted = 0x98a2b3;
constexpr uint32_t kAccent = 0x3b82f6;           // Blau für Bedienung
constexpr uint32_t kAlarm = 0xef4444;            // Rot für Wecker aus
constexpr uint32_t kCall = 0x22c55e;             // Grün für Anrufe

// Deckkraft der Glasflächen. Höher wirkt wie Milchglas, niedriger wie eine
// kaum sichtbare Scheibe.
constexpr lv_opa_t kGlassOpa = 28;
constexpr lv_opa_t kGlassBorderOpa = 60;

// --- Bausteine --------------------------------------------------------------

// Hintergrund mit Farbverlauf auf eine Seite legen.
void applyBackground(lv_obj_t *screen);

// Eine Glasfläche (Karte). Gibt das Objekt zurück, in das der Inhalt kommt.
lv_obj_t *makeCard(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                   lv_coord_t w, lv_coord_t h);

// Beschriftung mit Schrift und Farbe.
lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                    lv_align_t align, lv_coord_t x, lv_coord_t y,
                    const char *text = "");

// Knopf im Glas-Stil. `color` färbt ihn ein (0 = neutrales Glas).
lv_obj_t *makeButton(lv_obj_t *parent, const char *text, lv_event_cb_t handler,
                     lv_coord_t w, lv_coord_t h,
                     lv_align_t align, lv_coord_t x, lv_coord_t y,
                     uint32_t color = 0);

// Ein Knopf innerhalb einer Leiste (siehe makeButtonBar).
lv_obj_t *addBarButton(lv_obj_t *bar, const char *text, lv_event_cb_t handler,
                       lv_coord_t w, lv_coord_t h, uint32_t color = 0);

// Eine waagerechte Leiste, in der Knöpfe nebeneinander liegen. Der Abstand
// wird von LVGL berechnet, nicht von Hand gesetzt – so können sich zwei
// Knöpfe nicht überlappen, auch wenn sich Beschriftungen oder Größen ändern.
lv_obj_t *makeButtonBar(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                        lv_coord_t w, lv_coord_t h);

// Ein unsichtbarer Platzhalter, der den restlichen Platz in einer Leiste
// einnimmt: Was danach kommt, rutscht an den rechten Rand.
lv_obj_t *addBarSpacer(lv_obj_t *bar);

// Ein scrollbarer Bereich für Listen (Termine, Nachrichten): senkrecht
// scrollbar, mit dezenter Bildlaufleiste, Inhalte untereinander.
lv_obj_t *makeScrollArea(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                         lv_coord_t w, lv_coord_t h);

}  // namespace ui_theme
