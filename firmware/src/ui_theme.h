// Gestaltung der Oberfläche: Farben, Glasflächen, Knöpfe.
//
// Gewünscht ist ein moderner, „gläserner" Look. Der erste Entwurf setzte auf
// echte Durchsichtigkeit, Farbverläufe und weiche Schatten – und genau das war
// zu teuer:
//
// Der ESP32-S3 zeichnet alles mit dem Hauptprozessor, ohne Grafikbeschleuniger.
// Eine durchsichtige Fläche zwingt ihn, für **jedes** Pixel den Untergrund zu
// lesen und zu verrechnen; ein weicher Schatten kostet zusätzlich eine
// Weichzeichnung über die ganze Kante. Und weil die Karten durchsichtig waren,
// musste beim Weiterspringen der Uhr nicht nur die Uhr neu gezeichnet werden,
// sondern alles darunter gleich mit. Das Ergebnis war ein unruhiges Bild –
// nachgewiesen mit einer Testfirmware (src/paneltest.cpp), die nur ein festes
// Bild anzeigt: dort steht alles ruhig.
//
// Deshalb jetzt „Glas-Optik zum kleinen Preis":
//   • Karten mit **deckender** Farbe, die etwas heller ist als der Grund –
//     sieht aus wie eine Scheibe, kostet aber kein Verrechnen,
//   • ein feiner heller Rand, der die Kante fängt (das macht den Eindruck aus),
//   • große Rundungen bleiben,
//   • **keine** Schatten und keine Farbverläufe in den Karten.
//
// Alles an einem Ort, damit das Aussehen an einer Stelle geändert werden kann
// und nicht in jeder Seite einzeln.
#pragma once

#include <lvgl.h>

namespace ui_theme {

// --- Farben -----------------------------------------------------------------
constexpr uint32_t kBackgroundTop = 0x141821;    // Grund oben
constexpr uint32_t kBackgroundBottom = 0x0a0c11; // Grund unten
constexpr uint32_t kCard = 0x1e232e;             // Karten: deckend, heller als der Grund
constexpr uint32_t kCardBorder = 0x39404f;       // feiner Rand, der die Kante fängt
constexpr uint32_t kButton = 0x2b3140;           // neutrale Knöpfe
constexpr uint32_t kButtonPressed = 0x3d4557;    // gedrückt: heller
constexpr uint32_t kGlass = 0xffffff;            // nur noch für Bildlaufleisten
constexpr uint32_t kText = 0xf5f7fa;
constexpr uint32_t kTextMuted = 0x98a2b3;
constexpr uint32_t kAccent = 0x3b82f6;           // Blau für Bedienung
constexpr uint32_t kAlarm = 0xef4444;            // Rot für Wecker aus
constexpr uint32_t kCall = 0x22c55e;             // Grün für Anrufe

// Der Hintergrund bleibt ein Farbverlauf: Er wird genau einmal gezeichnet und
// danach von den deckenden Karten verdeckt – er kostet also nichts im Betrieb.

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
