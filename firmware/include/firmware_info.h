#pragma once
// Version dieser Firmware. Vor jeder Veröffentlichung (scripts/release.sh)
// FIRMWARE_BUILD erhöhen: Das Tablet nimmt nur Updates mit HÖHERER
// Build-Nummer an, und die Nummer ist mitsigniert (kein Zurückstufen).
//   FIRMWARE_BUILD    JJJJMMTTNN
//   FIRMWARE_VERSION  Anzeige in newsroom21 und auf der Seite „Gerät“
#define FIRMWARE_VERSION "0.2.1"
#define FIRMWARE_BUILD 2026093002LL

// Welches Abbild zu diesem Gerät passt (Ordner firmware/<modell>/ in newsroom21)
#define FIRMWARE_MODEL "tablet-s3"
