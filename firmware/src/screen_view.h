// Die Newsroom-Ansicht (orbital & Co.), gezeichnet vom Pi.
//
// Der Pi liefert sie als fertiges Bild mit einem Bit je Pixel – 48.000 Byte
// für 800x480. Das Tablet zeigt es unverändert an. So gibt es das Layout nur
// einmal, nämlich dort, wo es auch für das E-Ink-Display entsteht.
//
// Warum 1 Bit und kein PNG? Ein PNG wäre kleiner, bräuchte aber einen Decoder
// im Gerät und Platz für das entpackte Bild. So wird der Puffer genau einmal
// angelegt und danach direkt von LVGL gelesen.
#pragma once

#include <Arduino.h>
#include <lvgl.h>

namespace screen_view {

// Legt den Bildpuffer im PSRAM an und hängt die Bildfläche in `parent`.
// Rückgabe false, wenn kein Speicher da ist – dann bleibt die Ansicht aus und
// das Tablet zeigt nur seine eigene Seite.
// Muss unter der LVGL-Sperre aufgerufen werden.
bool begin(lv_obj_t *parent);

// Holt bei Bedarf ein neues Bild. Regelmäßig aus loop() aufrufen; die
// Anfrage läuft höchstens alle paar Sekunden und nur, wenn die Ansicht
// gerade sichtbar ist.
void loop(bool visible);

// Das LVGL-Bildobjekt zum Einhängen in eine Seite (nullptr, wenn kein
// Speicher da war).
lv_obj_t *image();

// True, sobald mindestens ein vollständiges Bild vom Pi angekommen ist.
bool hasImage();

// Letzter Fehler für die Statuszeile ("" = alles in Ordnung).
String lastError();

}  // namespace screen_view
