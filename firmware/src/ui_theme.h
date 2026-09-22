// Gestaltung der Oberfläche: Farben, Flächen, Knöpfe, Listen.
//
// Stil: **flach**, wie die iPhone-Oberflächen bis 2015. Keine Schatten, keine
// Farbverläufe, keine Durchsichtigkeit, keine Verzierungen. Was trennt, trennt
// durch eine feine Linie oder durch Abstand; was wichtig ist, steht größer;
// was bedienbar ist, ist farbig.
//
// Das ist nicht nur Geschmack, sondern hier auch die schnellste Lösung: Der
// ESP32-S3 zeichnet jedes Pixel mit dem Hauptprozessor. Eine deckende Fläche
// wird einfach geschrieben; eine durchsichtige zwingt ihn, den Untergrund zu
// lesen und zu verrechnen, ein weicher Schatten kommt einer Weichzeichnung
// gleich. Flach ist also zugleich ruhig.
//
// Nachweis, dass die Gestaltung die Ursache war: `src/paneltest.cpp` zeigt ein
// festes Testbild ohne LVGL – dort steht das Bild ruhig.
//
// Alles an einem Ort, damit das Aussehen an einer Stelle geändert werden kann
// und nicht in jeder Seite einzeln.
#pragma once

#include <lvgl.h>

namespace ui_theme {

// --- Farben -----------------------------------------------------------------
// Dunkel gehalten: Das Gerät steht auf dem Nachttisch und soll nachts nicht
// blenden. Die Akzentfarben sind die kräftigen Töne des flachen Stils.
constexpr uint32_t kBackground = 0x000000;       // Grund: durchgehend schwarz
constexpr uint32_t kSurface = 0x1c1c1e;          // abgesetzte Fläche (Knöpfe, Felder)
constexpr uint32_t kSurfacePressed = 0x3a3a3c;   // gedrückt
constexpr uint32_t kSeparator = 0x2c2c2e;        // feine Trennlinie
constexpr uint32_t kText = 0xffffff;
constexpr uint32_t kTextMuted = 0x8e8e93;        // Nebentexte, Überschriften
constexpr uint32_t kAccent = 0x0a84ff;           // Blau: bedienbar
constexpr uint32_t kAlarm = 0xff453a;            // Rot: abschalten, Warnung
constexpr uint32_t kCall = 0x30d158;             // Grün: Anruf
constexpr uint32_t kGlass = 0x48484a;            // Bildlaufleisten

// --- Bausteine --------------------------------------------------------------

// Grundfarbe auf eine Seite legen.
void applyBackground(lv_obj_t *screen);

// Ein Bereich der Seite. Flach heißt: keine Kachel mit Rahmen, sondern nur
// eine Fläche ohne eigene Farbe. Getrennt wird mit `makeSeparator`.
lv_obj_t *makeSection(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                      lv_coord_t w, lv_coord_t h);

// Eine feine Trennlinie (waagerecht oder senkrecht).
lv_obj_t *makeSeparator(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                        lv_coord_t w, lv_coord_t h);

// Beschriftung mit Schrift und Farbe.
lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                    lv_align_t align, lv_coord_t x, lv_coord_t y,
                    const char *text = "");

// Knopf. `color` färbt die Fläche (0 = neutrale Fläche, Text farbig).
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

// Einen Listeneintrag (Sender, WLAN-Netz) flach gestalten.
void styleListButton(lv_obj_t *btn);

}  // namespace ui_theme
