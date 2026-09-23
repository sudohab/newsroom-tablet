// Helligkeit, Nachtmodus und Bildschirm-Abschaltung.
//
// **Wichtig zur Hardware:** Die Hintergrundbeleuchtung dieses Boards hängt am
// IO-Expander CH422G und ist ein **reiner Schalter** – an oder aus, nichts
// dazwischen. Die Bibliothek nimmt zwar einen Prozentwert entgegen, macht
// daraus aber nur „größer als null = an" (nachgesehen im Treiber
// `esp_panel_backlight_switch_expander.cpp`).
//
// Deshalb zwei Wege:
//   • **Dimmen** geschieht über eine dunkle Fläche, die über der Oberfläche
//     liegt. Das Panel leuchtet gleich hell weiter, wirkt aber dunkler – für
//     den Nachttisch völlig ausreichend und ohne Zusatzteile.
//   • **Ganz aus** schaltet die Beleuchtung wirklich ab. Der Touchcontroller
//     arbeitet weiter; die erste Berührung schaltet sie wieder ein und wird
//     nicht als Bedienung gewertet.
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

// --- Helligkeit -------------------------------------------------------------
// 10 bis 100. Darunter wird nicht gegangen: Ein schwarzer Bildschirm wirkt
// defekt – wer ihn dunkel will, schaltet ihn aus.
void setDayLevel(uint8_t percent);
uint8_t dayLevel();

void setNightLevel(uint8_t percent);
uint8_t nightLevel();

// --- Nachtmodus -------------------------------------------------------------
// Zwischen diesen Stunden gilt die Nachthelligkeit. Sind beide gleich, ist der
// Nachtmodus aus.
void setNightHours(uint8_t startHour, uint8_t endHour);
uint8_t nightStart();
uint8_t nightEnd();
bool nightActive();

// --- Bildschirm abschalten --------------------------------------------------
// Nach so vielen Minuten ohne Berührung geht die Beleuchtung aus (0 = nie).
void setOffAfterMinutes(uint16_t minutes);
uint16_t offAfterMinutes();

// Von Hand abschalten; die nächste Berührung weckt wieder auf.
void turnOff();
bool isOff();

}  // namespace display_control
