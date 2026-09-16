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
Dieses Projekt liefert die Dateien in `server/` und ein Skript, das sie in eine
newsroom21-Arbeitskopie einspielt.

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
| 2 | Server: `app/tablets.py` + `app/tablet_api.py` (Token, Status, Konfiguration, Aktions-Allowlist) inkl. Tests | offen |
| 3 | Firmware: Startseite (Uhr, Weckzeit, Wetter, Snooze, Lautstärke) an der echten API | offen |
| 4 | Firmware: Radio-Seite (Favoriten, Start/Stop, Sleeptimer) | offen |
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

**Offen am Gerät:** Die WLAN-Verbindung zu „Wifi9G" scheitert bisher
(`Verbindung fehlgeschlagen`) – Passwort am Bildschirm neu eingeben. Bei den
Matrix-Uhren war ein Tippfehler im Passwort die Ursache.
