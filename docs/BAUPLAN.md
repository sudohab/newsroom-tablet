# Bauplan – newsroom-tablet

Tisch-Tablet für newsroom21 auf einem **Waveshare ESP32-S3-Touch-LCD-7**.
Dieses Dokument ist der Plan *und* das Änderungsprotokoll: Was gebaut wurde und
warum, steht unten in „Änderungsprotokoll“.

---

## 1. Zweck

Ein 7-Zoll-Touchdisplay steht auf dem Tisch/Nachttisch und bedient den Wecker
newsroom21 (läuft im Heimnetz auf dem Pi 5, `192.168.178.113`):

* **Info:** Uhrzeit, nächster Weckruf, Wetter, Nachrichten, Anrufer der Fritz!Box
* **Bedienung:** Snooze/Wecker aus, Lautstärke, **Radio** (Favoriten, Start/Stop,
  Sleeptimer), **Podcast** (Abos, Episoden, Start/Pause)
* **Einstellungen:** eigene Seite auf dem Gerät, aufgebaut wie die Weboberfläche

Das Tablet ist ein **Client**. Es speichert keine Daten außer seiner eigenen
Konfiguration und rechnet nichts selbst aus – die Wahrheit steht immer im Pi.

---

## 2. Hardware (geprüft, nicht geraten)

| Teil | Wert |
|---|---|
| Modul | ESP32-S3-WROOM-1 **N16R8** – 16 MB Flash, 8 MB PSRAM (per `esptool` bestätigt: ESP32-S3 QFN56 rev v0.2, PSRAM 8 MB, MAC `80:b5:4e:ce:e6:d8`) |
| Panel | 7", IPS, 800×480, **ST7262**, RGB-Parallel-Interface |
| RGB-Daten | R: 40, 41, 42, 2, 1 · G: 0, 39, 45, 48, 47, 21 · B: 10, 14, 17, 18, 38 |
| RGB-Steuerung | HSYNC 46, VSYNC 3, PCLK 7, DE 5 |
| Touch | **GT911**, I²C SDA 8 / SCL 9, INT 4, Reset über den Expander |
| IO-Expander | **CH422G** (I²C) – EXIO1 Touch-Reset, EXIO2 Backlight, EXIO4 SD-CS, EXIO5 USB-Umschaltung |
| Strom | USB-C 5 V, im Betrieb ca. 450 mA |
| Serielle Schnittstelle | CH343 (`1a86:55d3`) → `/dev/ttyACM0` |

**Wichtig zur Stromversorgung:** Das Panel zieht mit Backlight deutlich mehr als
ein schwacher USB-Port liefert. Ein wackeliges Netzteil zeigt sich als
Flackern, Neustarts oder „WLAN verbindet nicht“. Für den Dauerbetrieb ein
5 V/2 A-Netzteil verwenden, kein Hub-Port.

---

## 3. Softwarestand

* PlatformIO 6.1.18 (vorhanden), Plattform **pioarduino espressif32 55.03.31**
  (= Arduino-Core 3.x, ESP-IDF 5.x) – bereits installiert
* Bibliotheken: `ESP32_Display_Panel` (bringt die fertige Boarddefinition
  „Waveshare:ESP32-S3-Touch-LCD-7“ mit, damit die Pins nicht von Hand
  gepflegt werden müssen), `lvgl` 8.4
* Versionen werden in `firmware/platformio.ini` **fest gepinnt** (claude.md §4)

---

## 4. Aufbau im Überblick

```
   ESP32-S3-Touch-LCD-7                       Pi 5 (192.168.178.113)
   ┌───────────────────────┐                  ┌──────────────────────────┐
   │ LVGL-Oberfläche       │                  │ Caddy (TLS, einziger     │
   │  Start · Radio ·      │   HTTPS + Token  │ Eingang, eigene Root-CA) │
   │  Podcast · Einstell.  │ ───────────────► │   └── newsroom21-App     │
   │                       │ ◄─────────────── │        /api/tablet/*     │
   │ WLAN-Zugang + Token   │   JSON           │        (eigener Token)   │
   │ nur im NVS (Flash)    │                  │   Alarm · Audio · Radio  │
   └───────────────────────┘                  │   · Podcast · Wetter     │
                                              └──────────────────────────┘
```

### Warum die Server-Seite in newsroom21 liegt und nicht in einem eigenen Container

claude.md §7 verlangt „ein Container pro Anwendung“. Ein eigener Container für
das Tablet bräuchte aber Zugriff auf Wecker, Audio, Radio und Podcast – die
laufen als Module **in** newsroom21. Ein Zwischen-Container müsste sich dort mit
dem Web-Passwort anmelden und dieses dauerhaft speichern. Das wäre ein zweiter
Ort mit vollen Rechten, also **weniger** sicher.

Deshalb: Die Tablet-Schnittstelle ist ein Flask-Blueprint **innerhalb** der
newsroom21-App (wie `clock_api.py` für die Matrix-Uhren). Sie bekommt keine
eigene Netzwerkfläche – erreichbar bleibt nur der Reverse Proxy (claude.md §8).
Entschieden am 16.09.2026: Die Dateien liegen **direkt im Repo newsroom21**,
nicht als Kopie hier. Zwei Kopien derselben Datei driften auseinander; so
prüft die dortige Testsuite die Schnittstelle mit, und der vorhandene Updater
bringt sie auf den Pi. In diesem Projekt bleibt die Firmware.

Gegenstelle in newsroom21 (Commit `3ab0681`):
`app/tablet_api.py`, `app/tablets.py`, `app/device_tokens.py`,
`app/ratelimit.py`, `tests/test_tablet_api.py`, `compose.tablet.yaml`.

---

## 5. Sicherheitskonzept

| Schutzziel | Umsetzung |
|---|---|
| Kein Secret im Code/Git | WLAN-Zugang und Geräte-Token stehen **nur im NVS** des ESP: das WLAN wird am Touchscreen eingegeben, der Token per USB eingespielt. Das Gerät macht dafür ausdrücklich **keinen offenen Einrichtungs-Hotspot** auf (anders als die Matrix-Uhren, die kein Display haben). Auf dem Server liegt nur der Hash als Secret-Datei (`tablet_token`), verwaltet mit `scripts/secrets.sh`. |
| Token-Speicherung | Server speichert **nur den SHA-256-Hash** eines 32-Byte-Zufallstokens plus Ablaufdatum – wie bei den Matrix-Uhren. Vergleich zeitkonstant über alle Einträge. |
| Kleinstmögliche Rechte (AuthZ) | Der Tablet-Token öffnet **ausschließlich** `/api/tablet/*`. Kein Dashboard, kein Login, keine System-Endpunkte. Aktionen sind eine **feste Allowlist** (Snooze, Wecker aus, Lautstärke, Radio/Podcast starten und stoppen, Sleeptimer). Ausdrücklich **nicht** erlaubt: Passwörter, Home-Assistant-/Fritz!Box-Zugangsdaten, Systemupdate, Neustart, Feeds löschen. |
| Input-Validierung | Jede Eingabe vom Tablet wird per Pydantic-`StrictModel` geprüft (`extra="forbid"`), bevor sie ein Modul erreicht. |
| Missbrauch/Überlast | `_rate_limit` je Aktion (wie bei Radio/Podcast in der Weboberfläche); Statusabfrage mit `Cache-Control: no-store`. |
| Transport | HTTPS über Caddy. Der ESP prüft die Kette gegen die **eingebaute newsroom21-Root-CA**. Weil der Pi per IP angesprochen wird, ist nur der Namensabgleich abgeschaltet – die CA-Prüfung bleibt aktiv (gleiche Lösung wie bei den Uhren). |
| Fehlerbehandlung | Server antwortet generisch (kein Stacktrace, keine Pfade). Das Tablet zeigt „Keine Verbindung“ und die letzte bekannte Zeit weiter. |
| Sparsames Logging | Token, WLAN-Passwort und Anrufernummern werden nie geloggt. |

**Bewusste Einschränkung:** Wer das Tablet physisch in der Hand hat, kann den
Flash auslesen und käme an Token und WLAN-Passwort. Ein Tischgerät in der
Wohnung ist dieses Risiko wert; dafür ist der Token eng begrenzt (siehe oben),
läuft ab und lässt sich in der Weboberfläche einzeln zurückziehen.

---

## 6. Etappen

| # | Inhalt | Stand |
|---|---|---|
| 1 | Firmware-Grundgerüst: Panel, Touch, Backlight, WLAN-Einrichtung, TLS-Test gegen den Pi, Uhr auf dem Schirm | **fertig, auf dem Gerät** |
| 2 | Server: `app/tablets.py` + `app/tablet_api.py` (Token, Status, Konfiguration, Aktions-Allowlist) inkl. Tests | **fertig im Repo newsroom21** |
| 3 | Firmware: Startseite (Uhr, Weckzeit, Wetter, Snooze, Lautstärke) an der echten API | **fertig, auf dem Gerät** |
| 3b | Newsroom-Ansicht (orbital) als Bild vom Pi | Firmware fertig, Server-Teil noch nicht auf dem Pi |
| 4 | Firmware: Radio-Seite (Favoriten, Start/Stop, Sleeptimer) | **fertig, auf dem Gerät** |
| 5 | Firmware: Podcast-Seite (Abos, Episoden, Start/Pause) | offen |
| 6 | Firmware: eigene Einstellungsseite auf dem Gerät | offen |
| 7 | newsroom21-Weboberfläche: Tab „Tisch-Display“ mit Probier-Knopf | offen |
| 8 | Handbuch, Deploy-Checkliste, Sicherheits-Durchsicht | offen |

Vor Etappe 1 wird die **Werksfirmware gesichert** (`scripts/backup_flash.sh`),
damit das Gerät jederzeit in den Auslieferungszustand zurück kann.

---

## Änderungsprotokoll

### 2026-09-16 – Etappe 1

Werksfirmware gesichert (`backup/werksfirmware-2026-09-16.bin`, 16 MB, Prüfsumme
daneben), danach eigene Firmware gebaut und geflasht. Am Gerät bestätigt:
Panel, GT911-Touch, CH422G-Expander und LVGL starten, 7,6 MB PSRAM frei.

Am Gerät gelernte Stolpersteine (alle in den Dateien kommentiert):

* **PSRAM:** `board_build.psram_type = opi` wirkt bei Arduino-Core 3.x nicht.
  Richtig ist `board_build.arduino.memory_type = qio_opi`. Vorher meldete der
  Start „PSRAM chip is not connected" und der Bildpuffer ließ sich nicht
  anlegen („no mem for frame buffer") – das Display blieb dunkel.
* **Serielle Ausgabe:** Mit `ARDUINO_USB_CDC_ON_BOOT=1` schreibt `Serial` auf
  die native USB-C-Buchse, während die Systemmeldungen über UART0/CH343 laufen;
  am angeschlossenen Port sah man nur die halbe Ausgabe. Jetzt `=0`.
* **Flash auslesen:** 921600 Baud brach bei 49 % mit „Corrupt data" ab,
  460800 Baud lief über die vollen 16 MB durch.
* **Verbindungstest:** `/healthz` ist als Testziel ungeeignet – Caddy
  beantwortet es bewusst mit 404 (es gehört dem Docker-Healthcheck).
  Der Test ruft stattdessen die Startseite auf; jede gültige Antwort beweist,
  dass TLS und Zertifikatskette stimmen.
* **`lv_conf.h` und die Boarddefinitionen** liegen in `firmware/include/` und
  müssen per `-Iinclude` auch beim Übersetzen der Bibliotheken im Suchpfad
  stehen, sonst findet LVGL seine Konfiguration nicht.
* **Anti-Tearing** ist bewusst noch aus (LVGL zeichnet in zwei kleinen Puffern
  im internen RAM). Wenn beim Wischen Streifen sichtbar werden, ist der
  nächste Schritt `LVGL_PORT_AVOID_TEARING_MODE = 3` mit Bounce-Buffer.

**WLAN, am Gerät geklärt:** Die Verbindung steht (IP 192.168.178.116). Drei
Punkte, die dabei herauskamen:

* **5 GHz gibt es für dieses Gerät nicht.** Der ESP32-S3 funkt ausschließlich
  auf 2,4 GHz; ein 5-GHz-Netz taucht in der Netzliste gar nicht erst auf. Was
  in der Liste steht, ist also immer 2,4 GHz – die Frage „welches ist welches"
  stellt sich am Tablet nicht.
* **Ständiges Neuverbinden:** Nach dem erfolgreichen Verbinden ging die
  Verbindung sofort wieder verloren und wurde neu aufgebaut – im Sekundentakt.
  Ursache war die Logik hier: Schon ein einziger Statusabruf ungleich
  „verbunden" löste einen kompletten Neuaufbau aus. Jetzt gilt die Verbindung
  erst nach **5 Sekunden** ohne Lebenszeichen als verloren; kurze Aussetzer
  (z. B. ein Kanalwechsel des Routers) überbrückt das Gerät.
* **Länderkennung DE** gesetzt (`esp_wifi_set_country_code("DE", true)`): In
  der weltweiten Voreinstellung tut sich das Funkmodul mit den Kanälen 12 und
  13 schwer, die eine Fritz!Box automatisch wählen kann.
* **Abbruchgründe werden jetzt übersetzt:** Ein Ereignis-Rückruf schreibt den
  Grundcode des Funkmoduls als Klartext ins Log und auf den Bildschirm
  („Passwort falsch (kein Schlüsseltausch)", „Netz nicht gefunden", „Signal zu
  schwach") – sonst sieht man nur „hat nicht geklappt".

### 2026-09-16 – Etappe 2 (im Repo newsroom21)

Schnittstelle `/api/tablet/*` gebaut, 21 neue Tests, gesamte Testsuite grün
(512 Tests). Entscheidungen:

* **Radio nur aus Favoriten.** Das Tablet kann keine freie Adresse zum
  Abspielen schicken – sonst könnte ein gestohlener Token den Pi dazu bringen,
  beliebige Adressen im Netz abzurufen. Wer einen neuen Sender aufnehmen will,
  tut das in der Weboberfläche.
* **Der Zustand enthält nur Anzeigedaten.** Kein Telefonbuch, kein
  Anrufverlauf, keine Weckerliste, keine Zugangsdaten – nur, was auf den
  Bildschirm kommt.
* **Jede Aktion nimmt genau ihre Felder an.** Ein `snooze` mit `minutes`
  wird abgewiesen, ein `radio_play` ohne Sender ebenso. Ein Test hält Schema
  und Blueprint zusammen, damit beide nicht auseinanderlaufen.
* **Zwei Module herausgelöst:** `app/device_tokens.py` (Token-Prüfung,
  zeitkonstant, nur Hashes gespeichert) und `app/ratelimit.py`. Die Sperre lag
  bisher in `routes.py` – die Tablet-Schnittstelle hätte dafür das komplette
  Routen-Modul samt Login laden müssen. `app/matrix_clocks.py` blieb bewusst
  unangetastet, damit die laufenden Uhren durch eine Aufräumaktion nicht
  ausfallen.

**Damit es auf dem Pi läuft** (macht Hannes, SSH vom PC geht nicht):

1. newsroom21 aktualisieren (Updater oder `git bundle` wie gehabt)
2. `sudo scripts/secrets.sh` → „9) Tablet-Token erzeugen" → Token notieren
3. In `/opt/newsroom21/.env` `compose.tablet.yaml` an `COMPOSE_FILE` anhängen
4. `sudo docker compose up -d`

### 2026-09-16 – Etappe 3b: die orbitale Ansicht

Hannes wollte die orbitale Ansicht aus newsroom21 auch auf dem Tablet. Sie
wird **nicht** nachgebaut: `app/display/renderer.py` zeichnet sie bereits für
das E-Ink – in genau 800×480, der Auflösung des Tablets. Ein zweiter Nachbau
in der Firmware würde nach der ersten Änderung anders aussehen als das
Original.

* **Server:** `GET /api/tablet/screen` liefert das Bild mit **einem Bit je
  Pixel** (48.000 Byte, feste Größe). Gemessen sind nur 466 von 384.000
  Pixeln Graustufen (Kantenglättung), Schwarzweiß verliert also nichts. Ein
  PNG wäre kleiner, bräuchte aber einen Decoder und Platz für das entpackte
  Bild im Gerät.
* **Sparsam:** Das Tablet schickt die Prüfsumme des Bildes mit, das es schon
  zeigt; unverändert antwortet der Pi mit `304` ohne Daten. Gezeichnet wird
  höchstens alle fünf Sekunden, je Design einmal für alle Geräte.
* **Firmware:** Der Puffer liegt im PSRAM (48 KB wären ein Drittel des freien
  internen Speichers) und wird von LVGL direkt als 1-Bit-Bild gelesen – keine
  Umwandlung, keine zweite Kopie.
* **Bedienung:** Die Ansicht ist die Startseite. Ein Tipp darauf führt zur
  Bedienseite, der Knopf „Ansicht" wieder zurück.
* Welche Ansicht ein Gerät zeigt, ist eine Einstellung je Tablet
  (`screen_theme`: orbital, mission_control, classic, off).

**Am Gerät geprüft:** Firmware läuft, fragt den Endpunkt ab – und bekommt
`404`, weil der Server-Teil noch nicht auf dem Pi ist. Nach dem nächsten
Bundle sollte das Bild erscheinen.

### 2026-09-16 – Neue Hauptansicht, Flackern behoben

**Flackern:** Die LVGL-Portierung stand auf „ohne Anti-Tearing": LVGL zeichnete
in zwei schmale Streifenpuffer im internen RAM und schob sie in das laufende
Bild – sichtbar als Flackern und Reißen. Jetzt Modus 3 (Doppelpuffer im PSRAM
+ LVGL-Direktmodus) mit Bounce-Puffer im internen RAM, wie Espressif es für
RGB-Panels auf dem ESP32-S3 empfiehlt. Kostet rund 830 KB PSRAM (von 8 MB) und
gibt sogar internen Speicher frei: 222 KB frei statt 158 KB.

**Orbitale Ansicht abgelehnt.** Hannes gefällt sie nicht; stattdessen eine
eigene Ansicht mit Uhrzeit, Anrufen, Wetter, Nachrichten und Terminen.
Abgestimmte Aufteilung: Kopfzeile quer (Uhr, Datum, Weckruf, Wetter), darunter
zwei gleich große Spalten – links Termine, rechts Nachrichten –, unten die
Bedienleiste. Ein Anruf legt sich als **großes Banner** über die Mitte.

Die vom Pi gezeichnete Ansicht bleibt erhalten (Knopf „Ansicht"), ist aber
nicht mehr die Startseite. Sie kostet nichts, solange man sie nicht ansieht:
Das Bild wird nur geholt, während die Seite sichtbar ist.

Dafür liefert `/api/tablet/state` jetzt auch die nächsten Termine und zu jeder
Schlagzeile die Quelle – weiterhin nur Anzeigedaten: kein Kalendername, keine
Beschreibung, keine Links.

### 2026-09-16 – Glas-Optik, Umlaute, scrollbare Spalten

Rückmeldung von Hannes: Ansicht vom Pi gefällt nicht (gelöscht), Knöpfe zu
groß, Umlaute fehlen, modernes Aussehen gewünscht, Listen sollen scrollen.

* **Umlaute.** Die in LVGL eingebauten Montserrat-Schriften enthalten nur
  ASCII – „ä", „ö", „ü", „ß" erschienen als leere Kästchen. Jetzt vier selbst
  erzeugte Schriften (18/22/30/56 px) mit ASCII **und** dem kompletten
  Latin-1-Bereich, also allen Umlauten und dem Grad-Zeichen. Erzeugt mit
  `scripts/build_fonts.sh` (lv_font_conv über npx); die fertigen `.c`-Dateien
  liegen im Git, Node.js ist also nur zum Ändern nötig.
* **Glas-Optik.** Dunkler Farbverlauf als Grund, darüber Flächen in Weiß mit
  geringer Deckkraft, hauchdünner heller Rand, große Rundungen, weicher
  Schatten. LVGL 8 kann den Hintergrund nicht echt weichzeichnen – dafür fehlt
  dem Mikrocontroller die Leistung –, aber der Eindruck entsteht auch so.
  Alles in `src/ui_theme.*`, damit das Aussehen an einer Stelle liegt.
* **Kleinere Knöpfe:** 54 px hoch statt 90, die Bedienleiste braucht jetzt ein
  Fünftel der Höhe statt eines Drittels.
* **Termine und Nachrichten scrollen** senkrecht, mit dezenter Bildlaufleiste.
* Die vom Pi gezeichnete Ansicht wurde **gelöscht**, auch serverseitig
  (newsroom21-Commit `371fe8a`). Weniger Code ist besser als eine
  abgeschaltete Funktion, die mitgepflegt werden muss.
