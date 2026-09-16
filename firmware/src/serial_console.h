// Einrichtung über die USB-Leitung.
//
// Wozu? Der Geräte-Token darf nicht über den Touchscreen eingetippt werden
// (43 Zeichen Zufall) und schon gar nicht durch einen Chat oder eine Datei
// wandern. Er wird deshalb am Rechner verdeckt eingegeben und direkt in den
// NVS des Tablets geschrieben – siehe scripts/provision.py.
//
// Sicherheitsüberlegung: Wer diese Konsole benutzen kann, hat das Gerät per
// USB-Kabel in der Hand. Wer das hat, könnte den Flash ohnehin auslesen und
// beschreiben. Die Konsole öffnet also keinen neuen Weg – aber sie gibt
// bewusst nichts preis: Sie zeigt weder Token noch WLAN-Passwort an, sondern
// nur, OB etwas hinterlegt ist.
//
// Befehle (eine Zeile, mit Zeilenumbruch abschließen):
//   STATUS            zeigt Firmware, WLAN-Netz (ohne Passwort), Pi-Adresse,
//                     ob ein Token hinterlegt ist
//   TOKEN <wert>      Geräte-Token speichern
//   HOST <adresse>    Adresse von newsroom21 setzen (IP oder Name)
//   FORGET-TOKEN      Token löschen
//   FORGET-WIFI       WLAN-Zugang löschen
//   HELP              diese Liste
#pragma once

namespace serial_console {

// Regelmäßig aus loop() aufrufen. Liest höchstens eine Zeile je Aufruf und
// blockiert nie – die Oberfläche soll währenddessen bedienbar bleiben.
void loop();

}  // namespace serial_console
