// Die einzelnen Seiten der Oberfläche.
//
// Aufbau: Es gibt **einen** Bildschirm mit drei festen Zonen –
//
//     ┌──────────────────────────────────────────┐
//     │ Kopf: Uhr, Datum, Wecker, Wetter         │  immer sichtbar
//     ├──────────────────────────────────────────┤
//     │ Inhalt: genau eine Seite ist sichtbar    │
//     ├──────────────────────────────────────────┤
//     │ unten links: „‹ Start“ (nur auf Unterseiten)│
//     └──────────────────────────────────────────┘
//
// Die Startseite ist ein Raster aus Kacheln – eine je Seite, mit Symbol,
// Name und einer Kurzinfo (z. B. nächster Termin, laufender Sender).
// Von jeder anderen Seite geht es mit „‹ Start“ (oder einem Tipp auf die
// Uhrzeit) zurück; nach einer einstellbaren Zeit ohne Berührung auch von
// selbst (Seite „Gerät“).
//
// Jede Seite ist ein Container im Inhaltsbereich; beim Wechseln wird der eine
// eingeblendet und der andere versteckt. Das ist schneller und ruhiger als
// mehrere Bildschirme, und Kopf und Menü bleiben stehen, statt bei jedem
// Wechsel neu gezeichnet zu werden.
//
// Wichtig zur Arbeitsteilung:
//   • `create` und `activate` laufen **unter der LVGL-Sperre** und dürfen
//     nichts Langsames tun.
//   • `work` läuft aus der Hauptschleife **ohne** Sperre und darf deshalb
//     Daten vom Pi holen (das dauert unter Umständen Sekunden).
#pragma once

#include <Arduino.h>   // String
#include <lvgl.h>

namespace ui_pages {

struct Page {
    const char *label;                      // Name auf der Kachel der Startseite
    const char *icon;                       // Symbol der Kachel (ui_icons.h), nullptr = keine Kachel
    uint32_t tint;                          // Farbe des Symbols
    lv_obj_t *(*create)(lv_obj_t *parent);  // baut den Inhalt auf
    void (*activate)();                     // beim Öffnen der Seite
    void (*work)();                         // Daten holen, aus der Hauptschleife
    // Beim Verlassen der Seite. Fast alle Seiten brauchen das nicht und
    // lassen es weg (dann nullptr). Nötig wurde es für die Bildschirm-
    // tastatur: Sie hängt am Bildschirm statt an der Seite -- sonst wäre sie
    // abgeschnitten -- und muss deshalb beim Wechseln selbst verschwinden.
    void (*deactivate)() = nullptr;
    // Übergeordnete Seite (deren create-Funktion), nullptr = Startseite.
    // Seiten ohne eigene Kachel (Radio, Podcast, WLAN) erreicht man über eine
    // andere Seite; der Knopf unten links führt dann dorthin zurück
    // („‹ Multimedia“ statt „‹ Start“).
    lv_obj_t *(*parent)(lv_obj_t *parent) = nullptr;
};

// Alle Seiten: zuerst die Startseite, danach in der Reihenfolge der Kacheln.
const Page *all();
int count();

// Die Nummer der Startseite (wird gesucht statt fest eingetragen).
int homeIndex();

// Die Nummer einer Seite anhand ihrer create-Funktion, -1 = gibt es nicht.
int indexOf(lv_obj_t *(*create)(lv_obj_t *parent));

// --- Die einzelnen Seiten ---------------------------------------------------
namespace home {
lv_obj_t *create(lv_obj_t *parent);
void activate();
void work();
}  // namespace home

namespace weather {
lv_obj_t *create(lv_obj_t *parent);
void activate();
void work();
}  // namespace weather

namespace calendar {
lv_obj_t *create(lv_obj_t *parent);
void activate();
void work();
}  // namespace calendar

namespace news {
lv_obj_t *create(lv_obj_t *parent);
void activate();
void work();
}  // namespace news

namespace warnings {
lv_obj_t *create(lv_obj_t *parent);
void activate();
void work();
}  // namespace warnings

namespace calls {
lv_obj_t *create(lv_obj_t *parent);
void activate();
void work();
}  // namespace calls

namespace radio {
lv_obj_t *create(lv_obj_t *parent);
void activate();
void work();
}  // namespace radio

namespace podcasts {
lv_obj_t *create(lv_obj_t *parent);
void activate();
void work();
}  // namespace podcasts

// Radio und Podcast unter einer Kachel (ab 0.9.0)
namespace multimedia {
lv_obj_t *create(lv_obj_t *parent);
void activate();
void work();
}  // namespace multimedia

// Steckdosen ein/aus (ab 0.9.0)
namespace smarthome {
lv_obj_t *create(lv_obj_t *parent);
void activate();
void work();
}  // namespace smarthome

namespace alarms {
lv_obj_t *create(lv_obj_t *parent);
void activate();
void work();
}  // namespace alarms

namespace clocks {
lv_obj_t *create(lv_obj_t *parent);
void activate();
void work();
void deactivate();
}  // namespace clocks

namespace signal {
lv_obj_t *create(lv_obj_t *parent);
void activate();
void work();
}  // namespace signal

namespace timers {
lv_obj_t *create(lv_obj_t *parent);
void activate();
void work();
}  // namespace timers

namespace printers {
lv_obj_t *create(lv_obj_t *parent);
void activate();
void work();
}  // namespace printers

namespace camera {
lv_obj_t *create(lv_obj_t *parent);
void activate();
void work();
void deactivate();
// Aus der Hauptschleife, auch wenn die Seite nicht offen ist: stoppt einen
// Stream, den diese Seite gestartet hat, nachdem man sie verlassen hat.
void background();
}  // namespace camera

namespace wifi {
lv_obj_t *create(lv_obj_t *parent);
void activate();
void work();
}  // namespace wifi

namespace settings {
lv_obj_t *create(lv_obj_t *parent);
void activate();
void work();
}  // namespace settings

// --- Gemeinsame Bausteine der Seiten ---------------------------------------

// Überschrift einer Seite (oben links im Inhaltsbereich).
lv_obj_t *makeTitle(lv_obj_t *parent, const char *text);

// Statuszeile einer Seite (oben rechts): „Lade …", Fehler, Anzahl.
lv_obj_t *makeStatus(lv_obj_t *parent);

// Eine Textzeile in einen scrollbaren Bereich hängen.
lv_obj_t *addLine(lv_obj_t *area, const String &text, const lv_font_t *font,
                  uint32_t color, lv_coord_t width);

// Ein Wettersymbol zum WMO-Code. Gibt einen kurzen Text zurück, der in den
// eingebauten LVGL-Symbolen enthalten ist – eigene Bilder würden Flash kosten
// und müssten für jede Größe vorliegen.
const char *weatherSymbol(int wmo);

}  // namespace ui_pages
