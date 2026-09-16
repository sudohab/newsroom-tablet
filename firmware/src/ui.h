// Oberfläche des Tablets (LVGL).
//
// Etappe 1: Startseite mit Uhr und Verbindungsstatus sowie eine Seite zum
// Einrichten des WLAN. Radio-, Podcast- und Einstellungsseite kommen in
// späteren Etappen dazu.
//
// Wichtig zum Zusammenspiel mit LVGL: Die Bibliothek läuft in einer eigenen
// FreeRTOS-Aufgabe (siehe lvgl_port). Jeder Zugriff auf LVGL-Objekte von
// außerhalb dieser Aufgabe muss zwischen lvgl_port_lock()/unlock() stehen.
// Netzwerkanfragen dürfen NICHT innerhalb dieser Sperre laufen – sie dauern
// bis zu 8 Sekunden und würden die Anzeige so lange einfrieren. Deshalb
// merken sich die Schaltflächen nur einen Wunsch, den ui::tick() danach in
// Ruhe ausführt.
#pragma once

#include <Arduino.h>

namespace ui {

// Baut alle Seiten auf. Muss nach dem Start von LVGL aufgerufen werden.
void begin();

// Regelmäßig aus loop() aufrufen: aktualisiert Uhr und Status und führt
// angeforderte Netzwerkanfragen aus.
void tick();

}  // namespace ui
