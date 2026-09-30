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
| 5 | Firmware: Podcast-Seite (Abos, Episoden, Start/Pause) | zurückgestellt |
| 6 | Firmware: eigene Einstellungsseite auf dem Gerät | zurückgestellt |
| 7 | newsroom21-Weboberfläche: Tab „Tisch-Display“ mit Probier-Knopf | offen |
| 8 | Handbuch, Deploy-Checkliste, Sicherheits-Durchsicht | offen |

### Etappen des Oberflächen-Umbaus (ab 22.09.2026)

Nachdem das Bild ruhig steht, wird die Oberfläche neu aufgeteilt. Abgestimmt
mit Hannes:

| # | Inhalt | Stand |
|---|---|---|
| 9 | **Pi-Seite:** Wetterdetails, Termine mit Ort + Monatsübersicht, Nachrichten je Quelle, NINA-Warnungen, verpasste Anrufe, Wecker stellen | **fertig** |
| 10 | **Rahmen:** gemeinsame Kopfzeile (Uhr, Datum, Wecker, Wetter) auf allen Seiten + **waagerecht scrollbare Menüleiste** unten | **fertig, auf dem Gerät** |
| 11 | **Startseite:** nächster Termin, wechselnde Schlagzeile (alle 2 min, reihum je Quelle), rechts drei Felder: verpasste Anrufe · laufender Sender · NINA | **fertig, auf dem Gerät** |
| 12 | **Seiten:** Wetter · Termine (Monat + Liste) · Nachrichten · NINA · Anrufe · Radio (mit Lautstärke) · Wecker · WLAN | **fertig, auf dem Gerät** |
| 13 | Anrufbeantworter-Nachrichten (braucht neuen Fritz!Box-Zugriff, TR-064) | später |
| 14 | **Timer-Seite: vier Timer gleichzeitig** | später, Wunsch vom 23.09.2026 |

**Etappe 14 – Timer (vorgemerkt, noch nicht gebaut).** Gewünscht:

* **Vier Timer gleichzeitig**, unabhängig voneinander – z. B. Timer 1 auf
  40 Minuten, Timer 2 auf 10 Minuten.
* Auf der Timer-Seite sind **alle vier gleichzeitig sichtbar** mit ihrer
  Restzeit.
* Je Timer einzeln: **starten, pausieren, stoppen, löschen, wiederholen**.

Offene Fragen, die vor dem Bauen zu klären sind:
* **Wo laufen die Timer – im Tablet oder im Pi?** Im Pi wäre richtig, wenn ein
  abgelaufener Timer hörbar sein soll (dort ist der Lautsprecher) und wenn er
  einen Neustart des Tablets überleben soll. Im Tablet wäre einfacher, aber
  stumm und flüchtig. Empfehlung: im Pi, mit eigener Schnittstelle
  `/api/tablet/timers`; das Tablet zeigt und bedient nur.
* **Was passiert beim Ablauf?** Ton über den Pi, Anzeige auf dem Tablet, oder
  beides? Bis der Ton am Gerät da ist (MCP4725), bliebe nur der Pi.

### Etappe 15 – Geräte-Einstellungen (**fertig, auf dem Gerät**, 23.09.2026)

Eine Seite für das Gerät selbst:

* **Helligkeit einstellen**
* **Nachtmodus**: dimmen und ganz aus
* **Berührung weckt den Bildschirm** wieder auf

⚠ **Wichtige Einschränkung, am Gerät geprüft:** Die Hintergrundbeleuchtung
dieses Boards hängt am IO-Expander CH422G und ist ein **reiner Schalter** –
an oder aus, nichts dazwischen. In der Bibliothek nimmt `setBrightness()`
zwar einen Prozentwert entgegen, macht daraus aber nur „größer als null =
an" (nachgesehen in `esp_panel_backlight_switch_expander.cpp`). Eine echte
Helligkeitsregelung gibt es also nicht.

Was stattdessen geht:
* **Dimmen durch Abdunkeln des Bildes**: eine dunkle Fläche über der
  Oberfläche, stufenlos einstellbar. Das Panel leuchtet gleich hell weiter,
  wirkt aber dunkler – nachts völlig ausreichend und ohne Zusatzteile.
* **Ganz aus**: Beleuchtung abschalten (das kann der Schalter), Berührung
  schaltet sie wieder ein. Der Touchcontroller arbeitet weiter, auch wenn die
  Beleuchtung aus ist.
* Eine echte Regelung bräuchte einen Eingriff in die Hardware (Beleuchtung
  über einen PWM-fähigen Anschluss statt über den Expander).

### Etappe 16 – Nachrichten an die Matrix-Uhren (vorgemerkt, 23.09.2026)

Vom Tablet aus eine Nachricht auf die Laufschrift-Uhren schicken:

* **Vorgefertigte Nachrichten** zum Antippen („Bitte zum Essen kommen" …)
* **Freier Text** über die Bildschirmtastatur
* Empfänger wählbar: **eine bestimmte Uhr oder alle**

Gute Nachricht: Der Weg dorthin existiert schon. newsroom21 kann einer Uhr
einen Text schicken (`POST /api/matrix-clocks/<uhr>/test-text`, siehe
`app/matrix_clocks.py::queue_command`), und die Uhren holen ihn beim nächsten
Abruf ab. Zu bauen ist also:
* eine Tablet-Aktion `clock_message` mit **fester Textlänge und Zeichenprüfung**
  (die Uhren zeigen nur einen begrenzten Zeichensatz),
* die Liste der Uhren für die Empfängerauswahl (`GET /api/tablet/clocks`),
* die Seite mit Schnellauswahl und Tastatur.

Zu klären: Sollen die vorgefertigten Texte **in der Weboberfläche gepflegt**
werden (dann auch auf dem Tablet änderbar ohne neue Firmware) oder fest in
der Firmware stehen? Empfehlung: in newsroom21 pflegen.

**Gestaltung:** flach, an den LVGL-Beispielen orientiert (Schalter, Knöpfe,
waagerechtes Scrollen mit Einrasten).

**Aufbau der Startseite** (800×480):

```
┌──────────────────────────────────────────────────────────┐
│  22:44   Montag, 22.9.          ☁ 14°    ← Kopf, überall │
│          Weckruf 06:30                                   │
├───────────────────────────────────┬──────────────────────┤
│ NÄCHSTER TERMIN                   │ 3 verpasste Anrufe   │
│ Do 18.9. 09:30  Zahnarzt          ├──────────────────────┤
├───────────────────────────────────┤ BR24                 │
│ NACHRICHTEN (Wechsel alle 2 min)  ├──────────────────────┤
│ ZEIT · Meldung eins …             │ alles ruhig          │
├───────────────────────────────────┴──────────────────────┤
│  ‹ Start · Wetter · Termine · News · NINA · Anrufe · …  › │
└──────────────────────────────────────────────────────────┘
```

**Reihenfolge der Schlagzeilen:** reihum über die Quellen – Quelle 1 Meldung 1,
Quelle 2 Meldung 1, Quelle 3 Meldung 1, dann Quelle 1 Meldung 2 und so fort.

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

### 2026-09-16 – Flackern (zweiter Anlauf), Knöpfe, Ton

**Flackern kam und ging.** Der Bounce-Puffer war mit 10 Zeilen zu klein: Er
reicht im Ruhezustand, aber sobald WLAN und TLS gleichzeitig arbeiten, staut
sich der PSRAM-Zugriff und das Panel bekommt seine Daten nicht rechtzeitig –
genau das erklärt, warum es kam und ging. Jetzt 40 Zeilen (zwei Puffer à
800×40×2 Byte = 128 KB internes RAM, von gut 220 KB frei). Danach am Gerät:
124 KB intern frei, TLS-Abrufe laufen weiter fehlerfrei.

**Knöpfe überlappten sich.** Ursache waren von Hand gesetzte Koordinaten: Wird
eine Beschriftung länger oder ein Knopf breiter, schiebt sich der nächste
darunter. Die Bedienleisten sind jetzt **Flex-Reihen** – LVGL berechnet die
Abstände selbst, ein Überlappen ist damit ausgeschlossen. Ein unsichtbarer
Platzhalter (`addBarSpacer`) schiebt die rechte Gruppe an den Rand.

**Ton: Das Board hat keine Audio-Hardware.** Kein Lautsprecher, kein Codec,
kein Verstärker, kein Summer – nachgesehen im Waveshare-Wiki. Belegt sind laut
Boarddefinition die GPIOs 0–5, 7–10, 14, 17, 18, 21, 38–42, 45–48; dazu 19/20
(native USB), 43/44 (serielle Schnittstelle) und 33–37 (Octal-PSRAM). Frei
wären also allenfalls 6, 11, 12, 13, 15 und 16 – wovon ein Teil auf dem Board
für SD-Karte, CAN und RS485 verdrahtet ist. Vor einem Umbau muss der
Schaltplan geprüft werden.

### 2026-09-16 – Flackern, dritter Anlauf

Hannes: „Sobald ich das Display berühre, dreht es völlig ab." Berühren heißt
scrollen, und dabei ändert sich fast die ganze Fläche – der Hinweis zeigte auf
die Zeichenbetriebsart.

1. **Anti-Tearing Modus 3 → 2.** Im Direktmodus muss LVGL geänderte Bereiche in
   *beide* Bildpuffer nachziehen; bei großen Änderungen kommt das nicht
   hinterher. Modus 2 zeichnet jedes Bild einmal vollständig und schaltet es
   als Ganzes um – dort kann das nicht passieren. Kostet 3 × 768 KB PSRAM.
2. **HTTPS-Verbindung offen halten.** Bisher wurde für jede Abfrage eine neue
   TLS-Verbindung aufgebaut: **1350 ms** gemessen, in denen zugleich der
   Speicherbus belastet wird, an dem auch das Panel hängt – bei einer Abfrage
   alle zwei Sekunden also die halbe Zeit. Mit offener Verbindung dauert
   dieselbe Abfrage **20–30 ms**, gemessen am Gerät. Bricht die Verbindung, wird
   sie beim nächsten Versuch neu aufgebaut.

**Offen:** Das Display hängt am USB-Port des PCs. Das Panel zieht mit Backlight
rund 450 mA, dazu Funkspitzen; ein schwacher Port bricht dabei kurz ein, was
genau so aussieht. Gegentest mit einem 5-V/2-A-Netzteil steht aus.

### 2026-09-17 – Das Flackern war eine Neustartschleife

Hannes: „Auch mit Netzteil flackert es heftig." Damit war die Stromversorgung
ausgeschlossen – und der Blick ins serielle Log zeigte den wahren Grund:

```
[I][Panel] Board begin success
[I][LvPort] Initializing LVGL display driver
E esp_core_dump_flash: Core dump write failed
… und von vorn, im Sekundentakt
```

**Das Gerät stürzte beim Start von LVGL ab und startete neu – endlos.** Jeder
Neustart baut das Bild neu auf; genau das sah aus wie heftiges Flackern.

Ursache: zu wenig **interner** RAM. Der ESP32-S3 hat davon nur rund 320 KB, und
ich hatte ihn zweimal hintereinander beschnitten – erst der große Bounce-Puffer
(40 Zeilen = 128 KB), dann die vergrößerten LVGL-Zeichenpuffer (60 Zeilen =
192 KB). Zusammen passte das nicht mehr, die Anlage schlug fehl, das Gerät
stürzte ab.

Jetzt: Bounce-Puffer 30 Zeilen (96 KB), LVGL-Puffer 20 Zeilen (64 KB). Am Gerät
geprüft: **ein** Start in 30 Sekunden statt einer Schleife, 209 KB intern frei
vor dem Start von LVGL, 95 KB danach, Abrufe laufen fehlerfrei.

**Lehre:** Bei Anzeigeproblemen zuerst ins serielle Log sehen, ob das Gerät
überhaupt durchläuft. Zwei Runden Feinabstimmung an Puffergrößen gingen drauf,
weil ich das Sichtbare (Flackern) gedeutet habe, statt das Log zu lesen.
Deshalb meldet das Gerät jetzt bei jedem Start, wie viel interner Speicher vor
dem Start von LVGL frei ist.

### 2026-09-22 – „Hüpft und verschiebt sich nach oben": Bilddrift

Zwei verschiedene Fehler, die sich ähnlich anfühlen:

* **Flimmern** = zu niedrige Bildwiederholrate → Pixeltakt von 16 auf 21 MHz
  (39 → 51 Bilder/s), wie in Waveshares eigenem Beispiel.
* **Drift** = das Bild springt nach oben und bleibt schief. Das RGB-Panel hat
  keinen eigenen Bildspeicher; der ESP32 schiebt die Zeilen im festen Takt
  hinaus. Verliert diese Übertragung **einmal** den Gleichlauf – etwa weil der
  PSRAM kurz nicht schnell genug liefert –, bleibt der Versatz dauerhaft.

Gegenmaßnahme: `esp_lcd_rgb_panel_restart()` nach jedem Bild. Die Funktion
merkt sich den Wunsch nur; der Neuanfang passiert beim nächsten Bildwechsel
und ist deshalb nicht sichtbar. ESP-IDF hat dafür die Einstellung
`LCD_RGB_RESTART_IN_VSYNC`; mit dem vorgefertigten Arduino-Kern lässt sie sich
nicht setzen, deshalb hängt die Firmware sich selbst in den Rückruf „Bild
fertig" und macht genau dasselbe.

### 2026-09-22 – Trennversuch: die Gestaltung war zu teuer

Nach mehreren Runden Raten ein sauberer Versuch: `src/paneltest.cpp` zeigt ein
festes Testbild (Farbbalken plus roter Streifen oben als Lineal) – **ohne**
WLAN, TLS, LVGL und ohne Neuzeichnen. Ergebnis von Hannes: **„Das Testbild ist
ruhig."**

Damit ist das Panel entlastet: Pixeltakt, Austastlücken und Bandbreite reichen.
Die Unruhe kam aus unserer Oberfläche.

**Nicht die Programmiersprache** (Hannes' Vermutung): Es läuft bereits C++
direkt auf dem Chip. Teuer war die Gestaltung. Der ESP32-S3 zeichnet alles mit
dem Hauptprozessor, ohne Grafikbeschleuniger:

* **Durchsichtige Flächen** zwingen ihn, für jedes Pixel den Untergrund zu
  lesen und zu verrechnen.
* **Weiche Schatten** sind eine Weichzeichnung rund um jede Karte – der
  teuerste Einzelposten.
* Und weil die Karten durchsichtig waren, musste beim Weiterspringen der Uhr
  **alles darunter** mitgezeichnet werden, jede Sekunde neu.

Jetzt „Glas-Optik zum kleinen Preis": deckende Karten in einem Ton heller als
der Grund, ein Pixel heller Rand, große Rundungen – **keine** Schatten, keine
Verläufe in den Karten. Der Hintergrundverlauf bleibt: Er wird einmal gezeichnet
und danach von den Karten verdeckt.

Die Testfirmware bleibt im Projekt (`pio run -d firmware -e paneltest -t upload`).
Sie hat in einem Durchgang geklärt, was vier Runden Vermutung nicht geschafft
haben – und beantwortet dieselbe Frage beim nächsten Mal sofort wieder.

### 2026-09-22 – Flaches Design, und zwei weitere Kostentreiber

Hannes wünscht sich den flachen Stil der iPhone-Oberflächen bis 2015:
schlicht, ohne Glas. Das trifft sich gut – flach ist hier auch die schnellste
Lösung.

**Neues Aussehen** (`src/ui_theme.*`): schwarzer Grund, keine Kacheln, keine
Rahmen, keine Schatten, keine Verläufe. Getrennt wird mit feinen Linien und
Abstand. Bedienbares ist farbig (Blau), Abschaltendes rot, Anrufe grün.
Knöpfe sind flache Flächen mit runden Ecken; auf neutraler Fläche ist die
Schrift blau – das ersetzt den Rahmen.

**Zwei Kostentreiber gefunden, die nichts mit dem Aussehen zu tun haben:**

1. **Die Listen wurden bei jeder Änderung neu aufgebaut** – auch wenn sich nur
   die Lautstärke geändert hatte. Das hieß: alle zwei Sekunden den halben
   Bildschirm neu zeichnen. Jetzt zählt `tablet_state` getrennt, ob sich
   *Termine oder Nachrichten* geändert haben (`consumeListsChanged`); nur dann
   werden die Spalten neu gebaut.
2. Der **Farbverlauf im Hintergrund** ist weg. Er musste bei jedem
   freigelegten Stück neu berechnet werden; eine einzige deckende Farbe wird
   einfach geschrieben.

### 2026-09-22 – Die Drift kommt vom Funkbetrieb

Der Stufentest (`src/paneltest.cpp`) schaltet die Verdächtigen nacheinander zu
und zeigt die Stufe als Farbbalken an. Hannes' Beobachtung:

* **Rot** (nur Bild): ruhig
* **Gelb/Grün** (WLAN an, *keine* Abfragen): Bild wandert nach unten
* **Blau** (zusätzlich Abfragen): wandert und flackert

Damit ist der Funkbetrieb selbst die Ursache, nicht die Oberfläche, nicht die
Abfragen und nicht das Panel. Passt zu Espressifs Fehlerliste zum RGB-Display:
Bilddrift entsteht, wenn dem Panel die PSRAM-Bandbreite fehlt.

Zwei Funde in der `sdkconfig` der vorgefertigten Arduino-Bibliotheken:

1. **`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`** – WLAN- und Netzwerkpuffer
   liegen im **PSRAM**, also genau in dem Speicher, aus dem das Panel dauernd
   sein Bild liest. Gegenmittel ohne Umbau: `WiFi.useStaticBuffers(true)` legt
   feste Puffer im internen RAM an.
2. **`CONFIG_LCD_RGB_RESTART_IN_VSYNC=y`** – die Neusynchronisierung, die ich
   von Hand eingebaut hatte, ist **bereits eingeschaltet**. Meine lief also
   doppelt; mit ihr wanderte das Bild sichtbar stärker. Wieder entfernt.

**Objektives Maß gefunden:** Der Stufentest zählt die fertig gezeichneten
Bilder. 39 je Sekunde entsprechen dem eingestellten Pixeltakt und heißen
„gesund"; steigt die Zahl (gemessen 74 bis 138), werden Bilder abgebrochen und
neu begonnen – genau das sieht man als Springen. Damit lässt sich die Unruhe
messen statt schätzen.

**Falls das nicht reicht**, bleibt Espressifs eigentliche Empfehlung:
`CONFIG_SPIRAM_FETCH_INSTRUCTIONS` und `CONFIG_SPIRAM_RODATA` einschalten,
damit der Flash im Betrieb nicht mehr gebraucht wird. Das geht nur mit einem
vollständigen ESP-IDF-Bau (`framework = arduino, espidf`) statt der
vorgefertigten Arduino-Bibliotheken – ein größerer Umbau mit langer erster
Übersetzung.

### 2026-09-22 – Wärme und Speicher gemessen: beides unauffällig

Hannes: „Der Espressif-Chip wird sehr heiß." Gemessen mit dem eingebauten
Temperatursensor (`temperatureRead()`): **45–48 °C**, über Minuten stabil. Das
fühlt sich am Finger heiß an, ist für diesen Baustein aber normal – kritisch
wird es erst jenseits von etwa 80 °C. **Nicht die Ursache.**

Dabei fiel etwas anderes auf: Der freie **interne** Speicher liegt im Betrieb
bei rund 37 KB (beim Start 85 KB). Über 100 Sekunden beobachtet: konstant, also
**kein Leck** – der Unterschied sind die offene TLS-Verbindung und die festen
WLAN-Puffer. Knapp, aber stabil.

Die Firmware meldet Temperatur, Takt und freien Speicher jetzt alle 30 Sekunden
(`[zustand]`) und auf `STATUS`. Solche Fragen sind damit in einer Minute
beantwortet statt in einer Bastelrunde.

### 2026-09-22 – Der Bau auf echtem ESP-IDF (der Durchbruch kam aus dem Backup)

Hannes' Idee, in der gesicherten Werksfirmware nachzusehen, hat die Sache
entschieden. Aus dem Abbild ausgelesen (`esp_app_desc`):

```
Projekt:  waveshare-7inch
Version:  2025.12.1          ← ESPHome-Versionsschema
ESP-IDF:  5.5.1
```

Und in den Zeichenketten: `esphome`, `lvgl`, `esp_lcd_new_rgb_panel`. Die
Werksfirmware ist also **ESPHome mit LVGL auf ESP-IDF 5.5.1** – fast unser
Aufbau, nur auf einem echten ESP-IDF statt der vorgefertigten
Arduino-Bibliotheken. Damit war bewiesen: Das Panel *kann* ruhig laufen, und
der Unterschied liegt in den Systemeinstellungen.

**Umstellung auf `framework = arduino, espidf`** mit `sdkconfig.defaults`.
Entscheidend (nachgeprüft in der erzeugten `sdkconfig`):

| Einstellung | Wirkung |
|---|---|
| `CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y` | Programmcode liegt im PSRAM |
| `CONFIG_SPIRAM_RODATA=y` | Konstanten liegen im PSRAM |
| *(beides zusammen)* | **Der Flash wird im Betrieb nicht mehr gebraucht** – und damit blockiert er den Speicherbus nicht mehr, an dem das Panel hängt |
| `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP` aus | WLAN-Puffer nicht im PSRAM |
| `CONFIG_ESP32S3_DATA_CACHE_64KB`, `..._LINE_64B` | größerer Zwischenspeicher, von Espressif für den Bounce-Betrieb empfohlen |
| `CONFIG_LCD_RGB_ISR_IRAM_SAFE=y` | Bildausgabe läuft auch, wenn der Flash beschäftigt ist |
| `CONFIG_AUTOSTART_ARDUINO=y` | sonst fehlt `app_main` – ohne diesen Schalter kein Einsprungpunkt |

**Stolpersteine auf dem Weg** (alle im Projekt behoben):
* Arduino als IDF-Komponente schleppt Cloud-Dienste mit (ESP Insights,
  RainMaker). PlatformIO erzeugt deren eingebettete Zertifikate nicht
  rechtzeitig; der Bau bricht mit „`https_server.crt.S` not found" ab.
  Abhilfe: einmal `ninja` im Bauordner die Zwischendateien erzeugen lassen.
* `build_src_filter` greift im IDF-Bau nicht mehr – die Testfirmware ist
  jetzt über `#ifdef PANEL_PCLK_MHZ` abgeschirmt.
* `sdkconfig.defaults` wirkt nur beim ersten Bau; danach muss die erzeugte
  `sdkconfig.<env>` gelöscht werden.

**Zwei frühere Notlösungen wieder zurückgebaut**, weil ihr Grund entfallen ist
und sie internen RAM fraßen (nur noch 14 KB frei, die Abfragen scheiterten):
`WiFi.useStaticBuffers(true)` und der 30-Zeilen-Bounce-Puffer (jetzt 10).
Danach: **55 KB frei, Abfragen fehlerfrei, 51 °C, keine Neustarts.**

### 2026-09-23 – Die neue Oberfläche

Neun Seiten, ein Bildschirm: Kopfzeile und Menüleiste stehen immer, dazwischen
wird der Inhalt gewechselt. Das ist schneller und ruhiger, als für jede Seite
einen eigenen Bildschirm zu laden, und die Uhr springt beim Wechseln nicht.

Aufbau im Quelltext:

| Datei | Inhalt |
|---|---|
| `ui.cpp` | Rahmen: Kopfzeile, Inhaltsbereich, scrollbare Menüleiste, Seitenwechsel |
| `ui_pages.h` | die gemeinsame Bauform einer Seite (`create`, `activate`, `work`) |
| `ui_page_home.cpp` | Startseite |
| `ui_pages_info.cpp` | Wetter, Termine, Nachrichten, Warnungen, Anrufe |
| `ui_pages_control.cpp` | Radio, Wecker, WLAN |
| `tablet_data.*` | die Listen vom Pi holen (Termine, News, Warnungen, Anrufe, Wecker) |

**Arbeitsteilung, die sich durchzieht:** `create` und `activate` laufen unter
der LVGL-Sperre und dürfen nichts Langsames tun; `work` läuft aus der
Hauptschleife und darf Daten holen. Ein Knopfdruck merkt sich deshalb nur
einen Wunsch – ausgeführt wird er in `work`. Sonst stünde die Anzeige während
jeder Anfrage still.

**Monatsübersicht:** Welche Tage Termine haben, rechnet der Pi aus. Wochentag
des Monatsersten und Schaltjahr rechnet das Gerät (ein paar Zeilen), statt es
übertragen zu lassen.

**Zwei bewusste Lücken**, beide sichtbar gemacht statt verschwiegen:
* Auf der Anrufseite steht „Anrufbeantworter: folgt" – der braucht einen neuen
  Fritz!Box-Zugriff (Etappe 13).
* Auf der Weckerseite gibt es den Schalter „am Gerät klingeln" bereits, aber
  ausgegraut: Das Board hat keine Tonausgabe.

**Wettersymbole sind Wörter** („bewölkt", „Regen"): Die Schriften dieses
Projekts enthalten ASCII und Latin-1, aber keine Wetterzeichen; LVGLs
eingebaute Symbole kennen weder Sonne noch Wolke. Echte Bildzeichen bräuchten
eine erweiterte Schrift oder gezeichnete Grafiken.

**Baustolperstein dauerhaft behoben:** Die eingebetteten Zertifikate der
Espressif-Zusatzkomponenten erzeugt jetzt `scripts/generate_embeds.py` vor dem
Übersetzen – und zwar beim **Laden** des Skripts, nicht als Vor-Schritt:
PlatformIO prüft die Quelldateien schon beim Aufbau des Abhängigkeitsbaums.

### 2026-09-23 – Streifen am linken Bildrand

Hannes: „Am linken Rand zieht es Streifen, diese flimmern." Drei Fragen dazu
gleich mitbeantwortet:

* **Einstellungen durcheinander?** Nein – nachgesehen in der erzeugten
  `sdkconfig`: Code und Konstanten im PSRAM, großer Cache, WLAN-Puffer
  draußen. Alles steht.
* **Netzteil?** Nicht nötig, das war schon ausgeschlossen.
* **Ursache:** meine eigene Speicherumverteilung beim ESP-IDF-Umbau. Der
  Bounce-Puffer war von 30 auf 10 Zeilen geschrumpft. Er füllt den
  Zeilenanfang vor; läuft er leer, fehlen die ersten Pixel jeder Zeile –
  **genau das sieht man als Streifen am linken Rand**. Mit der reicheren
  Oberfläche trat es zutage.

Aufteilung des knappen internen RAM (rund 320 KB), am Gerät erprobt:

| LVGL-Zeichenpuffer | Bounce-Puffer | Ergebnis |
|---|---|---|
| 60 Zeilen | 30 Zeilen | Speicher reichte nicht → Neustartschleife |
| 20 Zeilen | 10 Zeilen | Streifen am linken Bildrand |
| **10 Zeilen** | **20 Zeilen** | ruhig, 45 KB frei |

Kleinere Zeichenpuffer heißen nur, dass LVGL in mehr Streifen zeichnet – das
kostet etwas Zeit, aber nichts an Ruhe. Der Puffer des Panels ist der
wichtigere von beiden.

### 2026-09-23 – Einstellungsseite gebaut

Neue Seite „Geraet" (`src/ui_page_settings.cpp`) und die Steuerung dahinter
(`src/display_control.*`):

* **Helligkeit für Tag und Nacht** je ein Schieberegler (10–100 %).
* **Nachtmodus** mit Schalter und zwei Stundenwalzen (von/bis). Über
  Mitternacht hinweg richtig gerechnet (22 → 7 Uhr gilt als „nach 22 **oder**
  vor 7"). Ausgeschaltet wird gespeichert, indem Beginn und Ende gleich sind –
  so braucht es keinen zusätzlichen Wert im Speicher.
* **Bildschirm aus nach** nie/1/2/5/10/30/60 Minuten, plus Knopf „Jetzt aus".
* **Berührung weckt auf**, und zwar ohne die Berührung als Bedienung zu
  werten – sonst startete man beim Aufwecken versehentlich einen Sender.

Alles wirkt sofort und wird im Gerät gespeichert; es gibt bewusst keinen
„Speichern"-Knopf.

**Umsetzung des Dimmens:** Eine schwarze Fläche auf LVGLs oberster Ebene, deren
Deckkraft sich mit der eingestellten Helligkeit ändert. Sie liegt über allen
Seiten – auch über künftigen – und nimmt keine Berührungen an. Die
Hintergrundbeleuchtung selbst bleibt an; nur „ganz aus" schaltet sie wirklich
ab, denn mehr kann der Schalter am CH422G nicht.

### 2026-09-23 – Helligkeitsregler wieder entfernt

Hannes: „Die Helligkeit am Tag und Nacht können wir rausnehmen, da es ja ein
Schalter ist – nehmen wir lieber eine Abschaltzeit am Tag auf."

Richtig, und konsequenter als mein Zwischenschritt: Die Regler dimmten das
Bild über eine schwarze Fläche. Das funktionierte, war aber eine Regelung, die
so tat, als könne das Gerät etwas, das es nicht kann – und sie kostete bei
jedem Neuzeichnen zusätzliche Rechenzeit, weil über die ganze Fläche
verrechnet werden musste.

Jetzt stattdessen **zwei Abschaltzeiten**: eine für den Tag, eine für die
Nacht, jeweils nie/1/2/5/10/30/60 Minuten. Der Nachtmodus legt nur noch fest,
**welche** der beiden gerade gilt. Voreinstellung: tagsüber „nie" (ein Wecker,
der von selbst dunkel wird, überrascht), nachts fünf Minuten (damit er nicht
ins Zimmer leuchtet). Eine Berührung weckt ihn – ohne als Bedienung zu zählen.

Auf der Seite steht jetzt auch, warum es keinen Helligkeitsregler gibt. Eine
ehrliche Zeile ist besser als ein Regler, der nichts tut.

### 2026-09-23 – Eigene Boarddefinition wegen der Austastlücken

Das Bild rutschte weiterhin nach oben. Nächster Hebel aus Espressifs Liste:
**größere Austastlücken**. Zwischen zwei sichtbaren Zeilen macht das Panel
eine Pause; in dieser Pause kann der Speicher den Vorratspuffer nachfüllen.
Die Boarddefinition der Bibliothek ist mit 8 Pixeln sehr knapp.

Die Werte ließen sich nicht zur Laufzeit ändern (`getRefreshPanelFullConfig()`
ist nicht zugänglich), deshalb jetzt eine **eigene Boarddefinition**
(`firmware/include/esp_panel_board_custom_conf.h`). Sie wurde **automatisch**
aus der Boarddefinition der Bibliothek erzeugt – alle 81 Werte (Pins,
Controller, Touch, Expander) unverändert übernommen, von Hand geändert nur:

| Wert | vorher | jetzt |
|---|---|---|
| HBP (waagerecht hinten) | 8 | 40 |
| HFP (waagerecht vorne) | 8 | 20 |
| VBP (senkrecht hinten) | 8 | 20 |
| VFP (senkrecht vorne) | 8 | 10 |
| Bounce-Puffer | 10 Zeilen | 20 Zeilen |

Kosten: 36 statt 39 Bilder je Sekunde. Automatisch erzeugt heißt auch: kein
Vertippen bei zwanzig Datenleitungen.

Gleichzeitig das Layout der Einstellungsseite entzerrt – zwei lange
Überschriften nebeneinander hatten sich überschrieben. Jetzt eine gemeinsame
Überschrift „BILDSCHIRM AUS NACH" und darunter die kurzen „AM TAG" / „NACHTS".

### 2026-09-23 — Verrutschen: zwei echte Ursachen gefunden

Die groesseren Austastluecken (Eintrag davor) haben das Verrutschen **nicht**
behoben. Die Suche ging deshalb weiter, diesmal nicht ueber die
Panel-Zeitsteuerung, sondern ueber die Anleitung der Bibliothek selbst:
`docs/envs/use_with_idf.md`, Abschnitt *Solution for screen drift issue*.

**Ursache 1: Die Zeichenschleife lief auf dem falschen Rechenkern.**

Die Anleitung verlangt unter Punkt 2c, dass `lv_timer_handler()` auf demselben
Kern laeuft wie `board->begin()`. Die Vorlage entscheidet das ueber
`ARDUINO_RUNNING_CORE` — ein Name aus `Arduino.h`, das `lvgl_v8_port.cpp` aber
nicht einbindet. In unserem gemischten Bau (Arduino als ESP-IDF-Baustein) war
der Name also nie definiert, und es griff immer der ESP-IDF-Zweig: **Zeichnen
auf Kern 0**, Inbetriebnahme des Bildschirms aber auf **Kern 1**, weil `setup()`
bei uns auf Kern 1 laeuft (`CONFIG_ARDUINO_RUNNING_CORE=1`).

Behoben in `src/lvgl_port/lvgl_v8_port.h`: `LVGL_PORT_TASK_CORE` steht jetzt
fest auf `1`, mit Begruendung im Quelltext. Wer `setup()` verlegt, muss den
Wert mitziehen.

**Ursache 2: Die Bibliothek protokollierte jeden Zeichenvorgang.**

`include/esp_utils_conf.h` stand auf `ESP_UTILS_LOG_LEVEL_DEBUG`. Damit schrieb
die Bibliothek fuer **jeden einzelnen** `drawBitmap`-Aufruf eine Zeile ueber die
serielle Schnittstelle — mehrere hundert je Sekunde, mitten in dem Pfad, der
das Bild an das Panel liefert. Bei 115200 Bit je Sekunde kostet eine solche
Zeile rund 10 Millisekunden, in denen der Vorratspuffer des Panels nicht
nachgefuellt wird. Jetzt `ESP_UTILS_LOG_LEVEL_WARNING`.

Sichtbar wurde das erst beim Mitlesen der seriellen Ausgabe — dieselbe Lehre
wie beim Neustart-Kreislauf: **erst das Protokoll lesen, dann Werte drehen.**

*Geprueft nach dem Aufspielen:* nur noch die `[zustand]`-Zeilen, keine
Neustarts, 51 KB interner Speicher frei, 54 Grad.

*Nicht die Loesung, aber geprueft und verworfen:* `CONFIG_SPIRAM_XIP_FROM_PSRAM`
(von der Anleitung fuer ESP-IDF ab 5.3 empfohlen, wir haben 5.5.1). Ein Blick
in `components/esp_psram/esp32s3/Kconfig.spiram` zeigt: Auf dem ESP32-S3 ist
diese Option nur eine Sammelschaltung, die genau `SPIRAM_FETCH_INSTRUCTIONS`
und `SPIRAM_RODATA` einschaltet — beides haben wir laengst. Kein Unterschied.

### 2026-09-23 — Etappe: Podcast-Seite

Die Seite steht jetzt zwischen „Radio" und „Wecker" in der Menueleiste.

**Aufbau.** Zwei Spalten: links die Abos, rechts die Folgen des ausgewaehlten
Abos. Eine Folgenzeile zeigt Titel und darunter Datum, Dauer und den Stand
(„gehoert" oder „bei 12 Min"). Antippen spielt ab — und zwar dort weiter, wo
zuletzt aufgehoert wurde, denn den Stand fuehrt der Pi. Der Schalter „Von
vorn" uebergeht ihn. Rechts an jeder Zeile ein Umschalter „gehoert"/„offen";
gehoerte Folgen stehen blasser da. Unten „Aus".

**Warum `media_stop` und nicht `radio_stop`.** Die Radioseite hat schon einen
Aus-Knopf, der aber nur den Sender beendet. Fuer den Podcast ist
`tablet_state::stopMedia()` dazugekommen.

**Was die Seite bewusst nicht kann:** abonnieren, Feeds aendern, Folgen
herunterladen oder loeschen. Ein Geraet, das offen auf dem Tisch liegt, soll
nichts dauerhaft veraendern — dieselbe Linie wie beim Wecker (nur Zeit, Tage,
Ein/Aus) und beim Radio (nur Favoriten).

**Gegenstelle gekuerzt** (im Repo newsroom21, Commit `35d39f7`). Die beiden
Endpunkte reichten die Antwort des Moduls unveraendert durch. Jetzt filtern
`_podcast_summary()` und `_episode_summary()`:

| weggelassen | warum |
|---|---|
| `feed_url` | Das Geraet braucht sie nicht (abgespielt wird im Pi). Sie ist die Angabe, die ein gestohlenes Token sonst mit ausliefern wuerde. |
| `last_error`, `last_refresh`, `auto_download` | Innereien des Servers bzw. Einstellungen der Weboberflaeche |
| `storage_used_mb`, `downloading`, `downloaded` | Speicherstand und Downloads des Pi gehen das Display nichts an |
| `summary` | bis 300 Zeichen je Folge; bei 40 Folgen mehr, als das Geraet am Stueck einliest — und angezeigt wird es auf der schmalen Liste ohnehin nicht |

✅ Kennungen (`podcast_id`, `episode_id`) kommen zwar vom Pi, gehen aber als
Teil des Pfades bzw. des JSON-Koerpers zurueck. Sie werden vor jedem Versand
durch `looksLikeId()` geprueft (nur Hexziffern und Bindestriche) — so kann aus
einer unerwarteten Antwort kein veraenderter Pfad werden. Der Server prueft
zusaetzlich mit `schemas.valid_podcast_id` / `valid_episode_id`.

⚠ Die Ruhezeit des Podcast-Moduls (`quiet_time`) wird angezeigt, aber nicht
erzwungen — das macht der Pi, der ein Abspielen waehrend der Ruhezeit mit 409
ablehnt. Das Geraet zeigt die Meldung dann nur an.

*Noch nicht am Geraet erprobt.*

### 2026-09-23 — Folgenzeilen und Etappe 14: Timer-Seite

**Die Schrift in den Folgenzeilen überschrieb sich.** Ursache war derselbe
Fehler wie auf der Einstellungsseite, nur andersherum: Dort war eine
Beschriftung zu breit, hier wurde sie zu **hoch**. Eine LVGL-Beschriftung mit
gesetzter Breite, aber ohne gesetzte Höhe wächst nach unten, sobald der Text
umbricht — ein langer Folgentitel wurde zwei- oder dreizeilig und lief in die
Angabenzeile darunter. `LV_LABEL_LONG_DOT` wirkt erst, wenn die Höhe feststeht.

Also: `lv_obj_set_size()` statt `lv_obj_set_width()`, Zeilenhöhen als
Konstanten (`kTitleLine`, `kInfoLine`), Zeilenhöhe daraus gerechnet.

Nebenbei behoben: Ein Listenknopf lässt seine Beschriftung von Haus aus endlos
durchlaufen. Bei kurzen Sendernamen fällt das nicht auf, bei Podcasttiteln
liefe ständig eine Bewegung — und jede Bewegung heißt neu zeichnen. Jetzt
abschneiden.

**Die Timer-Seite** (Etappe 14) steht zwischen „Wecker" und „WLAN". Vier Plätze
nebeneinander, alle gleichzeitig sichtbar.

Ein Platz hat zwei Leben: **leer** — dann ist die große Zahl eine Dauer, die
man mit „+1" und „+5" aufbaut, und „Start" legt den Timer damit an und startet
ihn in einem Zug. **Belegt** — dann ist die große Zahl die Restzeit, und die
Knöpfe richten sich nach dem Zustand (Pause/Weiter/Stopp/Aus, dazu „Wdh" und
„Weg"). Die Bedienelemente werden einmal angelegt und danach nur ein- und
ausgeblendet; ein Neuaufbau bei jedem Zustandswechsel machte die Anzeige
unruhig.

**Gezählt wird im Pi**, wie besprochen: Dort hängt der Lautsprecher, und ein
Timer übersteht so einen Neustart des Displays. Das Gerät zählt zwischen zwei
Abrufen selbst herunter (nur die vier Zeitanzeigen werden je Sekunde neu
beschriftet) und zieht sich alle zehn Sekunden am Pi gerade.

**Zwei Entscheidungen, die im Gespräch offen waren:**

* *Was beim Ablaufen passiert:* Der Pi klingelt in Schleife (`start_ringing`),
  bis jemand „Aus" drückt — wie bei einem Anruf. Ein eigener Ton lässt sich
  über `timers.sound.file` in der config.yaml einstellen.
* *Was „Wiederholen" heißt:* Nach dem **Abstellen** läuft der Timer sofort
  wieder von vorn. Nicht beim Ablaufen von selbst — sonst liefe der nächste
  Durchgang, während der Ton noch klingelt.

⚠ **Die Wiederhol-Sperre des Pi wurde zur Stolperfalle.** Er lässt je Gerät nur
**eine Aktion in der Sekunde** durch. Auf einer Seite mit vier Timern
nebeneinander tippt ein Mensch schneller, und bekam ein „Bitte kurz warten"
für etwas völlig Harmloses. Die Sperre bleibt (sie ist die Bremse gegen ein
Gerät, das Amok läuft); stattdessen wiederholt `api_client::postJson` eine
abgewiesene Anfrage **einmal** nach 1,1 Sekunden. Das ist gefahrlos, weil eine
429 bedeutet: Der Pi hat abgewiesen, es ist nichts passiert, was sich
verdoppeln könnte. Und es hebelt nichts aus — bei echtem Dauerfeuer ist auch
der zweite Versuch zu früh.

✅ Timer-Kennungen sind reine Hexzeichen (ohne Bindestrich); dafür gibt es eine
eigene, engere Prüfung als `looksLikeId`. Die Dauergrenzen (10 s bis 24 h)
stehen dreifach: im Gerät, im Schema des Servers und im Modul.

*Serverseite im Repo newsroom21, Commit `0f32e2b`. Noch nicht am Gerät
erprobt — der Pi braucht erst das Update.*

### 2026-09-23 — Menüreihenfolge: „Start" in die Mitte

Auf Wunsch von Hannes steht „Start" nicht mehr am Anfang der Leiste, sondern
in der **Mitte**. Beim Einschalten rastet die Leiste dort ein; von dort geht es
nach links wie nach rechts.

    Timer · [Uhren] · Warnungen · News · ► START ◄ · Wetter · Termine ·
    Anrufe · Radio · Podcast · Wecker · WLAN · Geraet

Links liegt, was man im Vorbeigehen braucht, rechts das Nachschlagen und das
Einstellbare. Der Platz für „Uhren" (Nachrichten an die Matrix-Uhren,
Etappe 16) ist als Kommentar schon eingetragen.

Zentriert wird nicht von Hand: Die Leiste stand schon auf
`LV_SCROLL_SNAP_CENTER`, also genügt `lv_obj_scroll_to_view()`. Zwei Dinge
waren nötig:

* **`lv_obj_update_layout(screen)` vor dem ersten `showPage()`** – vorher
  stehen alle Knöpfe noch auf Position null, und es gibt nichts zu zentrieren.
* **Beim ersten Mal ohne Laufbewegung** (`LV_ANIM_OFF`), sonst wandert die
  Leiste beim Einschalten sichtbar an ihren Platz. Erkannt an `activePage < 0`.

Die Startseite wird über `ui_pages::homeIndex()` gesucht statt fest
eingetragen – wer eine Seite einfügt, muss also nichts nachziehen.

⚠ Dabei aufgefallen: `pageObjects[]` und `navButtons[]` in `ui.cpp` waren auf
**12** festgelegt – genau die Zahl der Seiten nach der Timer-Seite. Die
nächste Seite wäre stillschweigend hinten herausgefallen und hätte einen
Speicherfehler ausgelöst. Jetzt `kMaxPages = 16` plus eine Meldung über die
serielle Schnittstelle, falls es doch einmal zu eng wird.

### 2026-09-23 — Zwei Meldungen, die dasselbe bedeuten

Am Gerät kamen „Server meldet 404" (beim Abrufen der Timer) und „Server meldet
400: Ungültige Eingabe: action, repeat, seconds" (beim Anlegen). Beides heißt:
**Der Pi hat das Update noch nicht** – er kennt weder den Endpunkt noch die
Aktionen in seiner Prüfliste.

Die Timer-Seite sagt das jetzt im Klartext („Pi kennt die Timer noch nicht –
Update einspielen") und fragt danach nur noch **jede Minute** statt alle zehn
Sekunden nach. Ein echter Eingabefehler kann hinter der 400 nicht stecken:
Dauer und Kennung prüft das Gerät vorher selbst.

*Gemessen, während das Gerät im 404-Zustand lief:* Speicher konstant bei
47 251 Byte über drei Minuten, 53 °C, keine Neustarts. **Kein Speicherverlust.**
Und weil eine 404 eine gültige HTTP-Antwort ist, bleibt die TLS-Verbindung
bestehen – das vergebliche Anklopfen kostete also auch keinen Handschlag.

### 2026-09-23 — Aufblendfenster und gleiche Aufteilung der Startseite

**Startseite: die linke Spalte wird jetzt halbiert.** Die Trennlinie lag fest
bei 124; ein Termin mit Ortsangabe schob seine Schrift darüber hinweg in die
Nachrichten. Zwei Ursachen, beide dieselbe Sorte wie schon zweimal zuvor:

* Der Titel hatte nur eine **Breite**, keine Höhe – also wuchs er beim
  Umbrechen nach unten.
* Der Ort war eine textbreite Beschriftung (`makeLabel`) und lief nach
  **rechts** über die senkrechte Trennlinie hinaus.

Jetzt hat jede Hälfte ihren festen Platz (`kDivider = kLeftHeight / 2`), und
alle vier Beschriftungen haben eine feste Größe mit `LV_LABEL_LONG_DOT`. Was
nicht hineinpasst, wird abgeschnitten statt in die Nachbarschaft zu laufen.

> **Merksatz, der sich jetzt dreimal bestätigt hat:** Eine LVGL-Beschriftung
> ohne gesetzte Größe ist so groß wie ihr Text – nach rechts UND nach unten.
> Auf einer Seite mit festen Positionen gehört zu jeder Beschriftung eine
> feste Größe.

**Aufblendfenster (`ui_popup.{h,cpp}`).** Anruf und abgelaufener Timer sollen
auf jeder Seite zu sehen sein. Das Fenster hängt deshalb nicht an einer Seite,
sondern direkt am Bildschirm und liegt über allem.

* **Es deckt nicht den ganzen Bildschirm ab**: Die Kopfzeile mit Uhr und Datum
  bleibt sichtbar. Wer nachts geweckt wird, will zuerst wissen, wie spät es ist.
* **Anruf geht vor Timer**: Ein Timer lässt sich später noch abstellen, ein
  Anruf nicht.
* **Der Rahmen ist der einzige im ganzen Gerät.** Beim flachen Stil sonst
  verpönt – hier trennt er das Fenster von der Seite darunter, und seine Farbe
  sagt schon von weitem, worum es geht: rot beim Timer, grün beim Anruf.
* **„Aus" beim Timer schickt `timer_stop`** und lässt das Fenster stehen, bis
  der Pi bestätigt hat. Sonst sähe es aus, als wäre der Ton aus, während er
  noch läuft. Ausgeblendet wird erst, wenn der Zustand meldet, dass nichts
  mehr klingelt.
* **„Weg" beim Anruf** tippt es nur beiseite – bedienen lässt sich ein Anruf
  von hier nicht, das Display hat kein Mikrofon. Ein neuer Anruf hebt das
  Wegtippen wieder auf.

Grundlage ist ein neues Feld im Zustand (`/api/tablet/state`), der ohnehin
alle zwei Sekunden geholt wird: `timer{expired,id,label,duration}` – vier
Felder, nicht die ganze Timerliste. Serverseite: newsroom21 `fb707a5`.

⚠ **Beobachtung zum Bauen:** Nach dem Hinzufügen einer neuen `.cpp` schlägt
der **erste** Durchlauf fehl, der zweite geht durch (zweimal so erlebt,
jeweils rund sechs Minuten). Vermutlich die Erzeugung der eingebetteten
Zertifikate (`scripts/generate_embeds.py`) gegen den noch nicht
neuaufgebauten Abhängigkeitsbaum. Noch nicht untersucht – wer hier Zeit
verliert: einfach ein zweites Mal bauen.

### 2026-09-23 — Anrufbanner: Schrift nach Anlass

Im Aufblendfenster stand beides in der größten Schrift (56). Beim Timer ist
das richtig — dort steht ein kurzes Wort. Bei einem Anruf steht dort ein
**Name**, und in dieser Größe passte kaum der Vorname ins Feld.

Die Schrift richtet sich jetzt nach dem Anlass: **Timer groß und einzeilig,
Anruf mittel (30) und zweizeilig.** Die Höhe des Feldes bleibt gleich (76 px),
nur die Schrift wechselt — so bleibt das Fenster in beiden Fällen gleich
gebaut. `LV_LABEL_LONG_DOT` bricht innerhalb dieser Höhe um und setzt erst
danach Punkte, schneidet also nicht mitten im Namen ab.

*Nach dem Pi-Update geprüft:* keine Fehlermeldung mehr über die serielle
Schnittstelle, 49 °C, Speicher stabil.

### 2026-09-23 — Etappe 16: Nachrichten an die Matrix-Uhren

Die Aufteilung, die Hannes wollte: **gepflegt** werden die Texte in der
newsroom21-Weboberfläche, **geschickt** werden sie vom Tisch-Display.

**In der Weboberfläche** (Einstellungen → Matrix-Uhren → „Vorgefertigte
Nachrichten"): anlegen, im Feld ändern, mit ▲ umsortieren, entfernen.
Höchstens zwölf Texte à 80 Zeichen. Gespeichert im Zweig `presets` von
`newsroom_matrix_clocks.json` — neben den Uhreneinstellungen, weil es keine
Einstellung einer bestimmten Uhr ist, sondern eine gemeinsame Liste. Ein Test
hält fest, dass das Speichern der Texte die Uhreneinstellungen nicht anfasst.

Beim Speichern wird **geputzt statt abgelehnt**: Umbrüche weg (auf einer
Laufschrift erschienen sie als Kästchen), Leerzeichen zusammengefasst,
Doppeltes und Leeres entfernt. Der Server könnte auch die ganze Änderung
zurückweisen — dann müsste man bei zwölf Texten raten, welcher schuld war.
Zurück kommt deshalb, was **wirklich** gespeichert wurde, und die Oberfläche
zeigt dieses Ergebnis und sagt, wenn etwas weggefallen ist.

Eine leere gespeicherte Liste ist etwas anderes als „noch nie gepflegt": Wer
alle Texte löscht, bekommt nicht beim nächsten Aufruf die Voreinstellung
zurück.

**Auf dem Gerät** (Seite „Uhren", links neben „Warnungen"): links die Texte
zum Antippen, rechts die Auswahl der Uhr und ein Feld für eine freie
Nachricht.

* Die Texte werden **bei jedem Öffnen** neu geholt, nicht nur beim ersten —
  sie können in der Weboberfläche geändert worden sein.
* Die Uhrenauswahl ist ein **Knopf zum Weiterschalten**, keine Liste: Bei zwei
  Uhren ist das schneller, und die Beschriftung sagt immer, wohin es geht.
  Nach der letzten Uhr kommt „alle Uhren".
* Für den freien Text blendet LVGL eine **Bildschirmtastatur** ein. Sie hängt
  am Bildschirm, nicht an der Seite — sie ist höher als der Inhaltsbereich und
  wäre sonst abgeschnitten. Deshalb muss sie beim Seitenwechsel selbst
  verschwinden: `ui_pages::Page` hat dafür ein neues, optionales Feld
  `deactivate`, das `ui::showPage()` für die verlassene Seite aufruft. Alle
  anderen Seiten lassen es weg (`nullptr`).
* Die Tastatur behält bewusst LVGLs eigene Schrift: Ihre Tasten für „fertig"
  und „löschen" sind Symbole aus der Montserrat-Schrift, die unsere eigenen
  Schriften (ASCII + Latin-1) nicht enthalten.

✅ **Was das Gerät NICHT kann: die Texte ändern.** Es gibt dafür keine Aktion —
ein Test hält das fest. Von den Uhren gehen nur `id`, `name` und `online` ans
Gerät; Token-Ablauf, Firmwarestand, Signalstärke und Einstellungen bleiben der
angemeldeten Weboberfläche vorbehalten.

✅ Der Text kommt als **Nummer aus der Liste ODER frei getippt** — nie beides
und nie keins (eigener Prüfer im Schema, sonst müsste der Server raten, was
gilt). Freier Text ist hier vertretbar: Er wird nur angezeigt, nirgends
ausgewertet, und die Weboberfläche kann dasselbe längst über `test-text`.
Geprüft wird er trotzdem und durch `clean_preset()` geführt. Im Gerät werden
Anführungszeichen und Rückstriche maskiert und Steuerzeichen entfernt, bevor
der Text in den JSON-Körper geht.

⚠ Die Wiederhol-Sperre gilt für den **ganzen Vorgang**, nicht je Uhr. Sonst
könnte ein Gerät mit „alle" die Sperre der einzelnen Uhren umgehen.

*Serverseite: newsroom21 `997d8c7` (Weboberfläche) und `2da581d` (Tablet).*

### 2026-09-30 — Updates über das Netz, Kern 1 nur fürs Bild

**Verrutschen – die Kernaufteilung.** Nachgesehen in der erzeugten
`sdkconfig`: Die Arduino-Hauptschleife (`CONFIG_ARDUINO_RUNNING_CORE=1`) – und
damit JEDE Netzabfrage samt TLS –, die WLAN-Ereignisse des Arduino-Kerns
(`CONFIG_ARDUINO_EVENT_RUNNING_CORE=1`) und der TCP/IP-Stapel (keine
Kernbindung) liefen auf demselben Kern wie Panel und LVGL. Jetzt:

| | Kern |
|---|---|
| Panel-Unterbrechungen, LVGL (`lv_timer_handler`) | 1 |
| Hauptschleife (Netz, TLS, Seitenlogik), Arduino-Ereignisse, lwIP, WLAN-Treiber | 0 |

Panel und LVGL starten in `main.cpp` in einer eigenen Aufgabe auf Kern 1
(`startDisplay`), weil die Unterbrechungen des RGB-Panels auf dem Kern landen,
der `board->begin()` aufruft, und LVGL dort laufen muss (Lehre vom 23.09.).
Beim Start steht im Protokoll: `[start] Hauptschleife auf Kern 0, Bild auf Kern 1`.

**OTA.** Neue Partitionstabelle (`partitions.csv`): `otadata`, zwei
Programmbereiche zu 3 MB. `nvs` bleibt an derselben Stelle und gleich groß –
WLAN und Token überleben das Umstellen. `src/ota.cpp`: Manifest aus der
Zustandsabfrage, Signaturprüfung (ECDSA P-256, `include/ota_pubkey.h`,
erzeugt von `scripts/make_ota_pubkey.sh`), Download über `esp_http_client`
direkt in den freien Bereich, SHA-256-Vergleich, Rückfall nach 5 Minuten
ohne Kontakt zu newsroom21 (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`,
`verifyRollbackLater()`). Während des Ladens liegt ein Hinweis über allem.
Veröffentlichen: `scripts/release.sh`.

**Zustand an den Pi.** Das Tablet schickte bisher keinen `X-Tablet-Status`;
jetzt: rssi, heap, psram, uptime, fw, ip, fwmodel, build – in newsroom21 zu
sehen im Tab Updates.

**Waschmaschine fertig.** Aufblendfenster hinter Anruf und Timer, Knopf
„Erledigt“ (Aktion `washer_ack`).

Firmware 0.2.0 (Build 2026093001) – einmal per USB aufzuspielen.
