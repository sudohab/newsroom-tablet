// Nachtmodus und Bildschirm-Abschaltung.
//
// **Wichtig zur Hardware:** Die Hintergrundbeleuchtung dieses Boards hängt am
// IO-Expander CH422G und ist ein **reiner Schalter** – an oder aus, nichts
// dazwischen. Die Bibliothek nimmt zwar einen Prozentwert entgegen, macht
// daraus aber nur „größer als null = an" (nachgesehen im Treiber
// `esp_panel_backlight_switch_expander.cpp`).
//
// Deshalb gibt es hier **keine Helligkeitsregelung** – ein Regler, der nichts
// regelt, wäre irreführend. Gesteuert wird stattdessen, **wann** der
// Bildschirm dunkel ist: getrennte Abschaltzeiten für Tag und Nacht. Der
// Touchcontroller arbeitet weiter, wenn die Beleuchtung aus ist; die erste
// Berührung schaltet sie wieder ein und wird nicht als Bedienung gewertet.
#pragma once

#include <Arduino.h>

// Vorwärtsdeklaration statt Einbinden der Panel-Bibliothek: Dieses Modul
// braucht von ihr nur den Zeiger auf die Beleuchtung.
namespace esp_panel { namespace drivers { class Backlight; } }

namespace display_control {

// Einmal nach dem Start von LVGL aufrufen: legt die Abdunkelungsfläche an und
// übernimmt die Beleuchtung des Boards (aus main.cpp, wo das Board entsteht).
void begin(esp_panel::drivers::Backlight *backlight);

// Regelmäßig aus loop() aufrufen: Nachtmodus, Abschaltzeit, Aufwecken.
void loop();

// --- Nachtmodus -------------------------------------------------------------
// Zwischen diesen Stunden gilt die Nacht-Abschaltzeit. Sind beide gleich, ist
// der Nachtmodus aus und es gilt überall die Tageszeit.
void setNightHours(uint8_t startHour, uint8_t endHour);
uint8_t nightStart();
uint8_t nightEnd();
bool nightActive();

// --- Bildschirm abschalten --------------------------------------------------
// Nach so vielen Minuten ohne Berührung geht die Beleuchtung aus (0 = nie),
// getrennt für Tag und Nacht.
void setDayOffMinutes(uint16_t minutes);
uint16_t dayOffMinutes();

void setNightOffMinutes(uint16_t minutes);
uint16_t nightOffMinutes();

// Von Hand abschalten; die nächste Berührung weckt wieder auf.
void turnOff();
bool isOff();

}  // namespace display_control
