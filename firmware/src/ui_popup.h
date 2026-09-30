// Aufblendfenster über allen Seiten.
//
// Manche Dinge dulden keinen Seitenwechsel: Wenn das Telefon klingelt oder
// ein Timer abläuft, soll das Display es zeigen – gleich, welche Seite gerade
// offen ist. Dazu kommt „Waschmaschine fertig“ mit dem Knopf „Erledigt“. Deshalb hängt dieses Fenster nicht an einer Seite, sondern
// direkt am Bildschirm, und liegt über allem.
//
// Es gibt genau EIN Fenster. Kommt ein zweiter Anlass, während einer schon
// steht, ersetzt er ihn. Anrufe gehen dabei vor: Ein Timer lässt sich später
// noch abstellen, ein Anruf nicht.
//
// Arbeitsteilung wie überall: Der Knopf merkt sich nur den Wunsch, geschickt
// wird er aus der Hauptschleife.
#pragma once

#include <Arduino.h>
#include <lvgl.h>

namespace ui_popup {

// Einmal beim Aufbau der Oberfläche aufrufen (unter der LVGL-Sperre).
// `screen` ist der Bildschirm, an dem das Fenster hängt – bewusst übergeben
// und nicht über `lv_scr_act()` geholt: Beim Aufbau ist der Bildschirm noch
// gar nicht geladen.
void begin(lv_obj_t *screen);

// Aus ui::tick() heraus: blendet ein und aus, je nach Zustand vom Pi.
// Muss unter der LVGL-Sperre laufen.
void update();

// Aus der Hauptschleife, OHNE Sperre: führt ein Abstellen aus.
void work();

}  // namespace ui_popup
