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
