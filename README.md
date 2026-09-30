# newsroom-tablet

Tisch-Tablet für den Wecker **newsroom21**: ein Waveshare ESP32-S3-Touch-LCD-7
(7", 800×480, Touch), das Uhrzeit, Weckzeit, Wetter und Anrufe anzeigt und
Wecker, **Radio** und **Podcast** bedient – mit einer eigenen
Einstellungsseite auf dem Gerät, aufgebaut wie die Weboberfläche.

* Plan, Hardwaredaten und Sicherheitskonzept: [docs/BAUPLAN.md](docs/BAUPLAN.md)
* Bedienung und Installation: dieses Dokument (wächst mit den Etappen)

**Stand (30.09.2026):** Firmware 0.2.0 – Updates über das Netz (Tab „Updates“ in
newsroom21), Bild allein auf Kern 1. Ältere Etappen siehe BAUPLAN. Etappe 1:
Anzeige, Touch, WLAN-Einrichtung
am Bildschirm, Uhrzeit, verschlüsselter Verbindungstest zum Pi.

---

## 1. Überblick

```
Tablet (ESP32-S3)  ──HTTPS + Geräte-Token──►  Caddy  ──►  newsroom21 (Pi 5)
```

Das Tablet rechnet nichts selbst aus. Es zeigt an, was newsroom21 liefert, und
schickt Bedienwünsche zurück. Fällt das WLAN aus, bleibt die zuletzt bekannte
Uhrzeit stehen und es erscheint ein Hinweis.

## 2. Aufbau des Projekts

| Ordner | Inhalt |
|---|---|
| `firmware/` | PlatformIO-Projekt für den ESP32-S3 (Etappe 1 fertig) |
| `firmware/include/` | Konfiguration: `tablet_config.h`, `lv_conf.h`, Boarddefinition, Root-CA |
| `firmware/src/` | Quelltext: Einstellungen, WLAN, HTTPS-Client, Oberfläche |
| `server/` | Schnittstelle für newsroom21 (ab Etappe 2) |
| `scripts/` | Werkzeuge, u. a. Sicherung der Werksfirmware |
| `backup/` | Sicherung der Waveshare-Werksfirmware (nicht in Git) |

## 3. Bauen und Flashen

Voraussetzung: PlatformIO (liegt unter `~/.platformio`), Display per USB-C am
Rechner (meldet sich als `/dev/ttyACM0`).

```bash
# Übersetzen
~/.platformio/penv/bin/pio run -d firmware

# Auf das Gerät schreiben
~/.platformio/penv/bin/pio run -d firmware -t upload

# Meldungen mitlesen
~/.platformio/penv/bin/pio device monitor -b 115200 -p /dev/ttyACM0
```

Vor dem ersten Flashen die Werksfirmware sichern:

```bash
scripts/backup_flash.sh /dev/ttyACM0
```

Zurück auf die Werksfirmware:

```bash
esptool --port /dev/ttyACM0 write_flash 0x0 backup/werksfirmware-<datum>.bin
```

## 4. Erste Einrichtung am Gerät

1. Display an ein **5-V-Netzteil mit mindestens 2 A** anschließen (ein
   schwacher USB-Port führt zu Flackern und Neustarts).
2. Auf der Startseite **„WLAN einrichten"** antippen.
3. Netz aus der Liste wählen, Passwort über die Bildschirmtastatur eingeben,
   **„Verbinden"**. Der Zugang wird im Gerät gespeichert (NVS), nicht im
   Quelltext.
4. Zurück auf der Startseite **„Verbindung testen"** antippen. Erwartete
   Antwort: *„Pi erreichbar, Zertifikat geprüft"*.

### Geräte-Token einspielen

Der Token macht das Tablet gegenüber newsroom21 zum bekannten Gerät. Er wird
**nie abgetippt und nie weitergeschickt**, sondern verdeckt eingegeben und
direkt in den Flash geschrieben:

```bash
# 1. Auf dem Pi erzeugen (nur einmal sichtbar!)
sudo /opt/newsroom21/scripts/secrets.sh      # → 9) Tablet-Token erzeugen

# 2. Am PC ins Tablet schreiben (Display per USB angeschlossen)
~/.platformio/penv/bin/python scripts/provision.py

# Nachsehen, was hinterlegt ist (zeigt nie den Token selbst)
~/.platformio/penv/bin/python scripts/provision.py --status
```

Weitere Möglichkeiten: `--host 192.168.178.113` setzt die Adresse des Pi,
`--forget-token` löscht den Token wieder, `--port` wählt eine andere
Schnittstelle als `/dev/ttyACM0`.

Dieselben Befehle gehen auch von Hand im seriellen Monitor: `HELP` zeigt die
Liste (`STATUS`, `TOKEN <wert>`, `HOST <adresse>`, `FORGET-TOKEN`,
`FORGET-WIFI`).

## 5. Funktionen der Firmware (Etappe 1)

| Datei | Aufgabe |
|---|---|
| `src/main.cpp` | Startreihenfolge: Einstellungen → Panel/Touch → LVGL → Oberfläche → WLAN → Uhrzeit |
| `src/settings_store.*` | Gespeicherte Werte im NVS: WLAN, Pi-Adresse, Geräte-Token, Helligkeit. Prüft jede Eingabe auf Länge und erlaubte Zeichen |
| `src/wifi_manager.*` | Verbinden, Netzsuche, erneuter Versuch mit wachsendem Abstand. **Kein eigener Hotspot** |
| `src/api_client.*` | HTTPS zu newsroom21: prüft die Kette gegen die eigene Root-CA, folgt keinen Weiterleitungen, begrenzt die Antwortgröße |
| `src/ui.*` | Startseite (Uhr, Status, Testknopf) und WLAN-Seite (Liste, Tastatur) |
| `src/serial_console.*` | Einrichtung per USB: Token und Pi-Adresse setzen. Zeigt nie Token oder WLAN-Passwort an, sondern nur, **ob** etwas hinterlegt ist |
| `src/ui_theme.*` | Aussehen an einer Stelle: Farben, Flächen, Knöpfe, scrollbare Listen |
| `src/display_control.*` | Helligkeit (Abdunkeln), Nachtmodus, Bildschirm aus, Aufwecken per Berührung |
| `src/tablet_state.*` | Zustand vom Pi holen, Aktionen schicken (Wecker, Lautstärke, Radio) |
| `src/fonts/` | Schriften **mit Umlauten** (LVGLs eingebaute können nur ASCII), erzeugt von `scripts/build_fonts.sh` |
| `src/lvgl_port/` | Offizielle LVGL-Anbindung von Espressif (unverändert übernommen) |

## 6. Sicherheitshinweise

**✅ Berücksichtigt**

* **Keine Geheimnisse im Code oder in Git:** WLAN-Passwort und Geräte-Token
  stehen ausschließlich im NVS des Geräts. Im Log erscheint nur der Netzname,
  nie das Passwort; das Passwort wird im Arbeitsspeicher überschrieben, sobald
  es übergeben wurde.
* **Echte Zertifikatsprüfung:** Die Verbindung zum Pi wird gegen die
  mitgelieferte newsroom21-Root-CA geprüft. Abgeschaltet ist nur der
  Namensabgleich, weil der Pi über seine IP-Adresse angesprochen wird – ein
  pauschales „alles akzeptieren" gibt es nicht.
* **Keine offenen Dienste auf dem Gerät:** kein Webserver, kein OTA, kein
  Einrichtungs-Hotspot. Das Tablet ist reiner Client; Updates laufen über USB.
* **Eingaben werden geprüft:** Netzname, Passwort, Pi-Adresse und Token werden
  auf Länge und erlaubte Zeichen geprüft, bevor sie gespeichert werden.
  Anfragepfade müssen mit `/api/tablet/` beginnen.
* **Begrenzte Antworten:** höchstens 16 KB je Antwort, 8 s Zeitlimit, keine
  automatischen Weiterleitungen (sonst könnte der Token an eine fremde Adresse
  wandern).

**⚠️ Noch kritisch**

* Wer das Gerät in die Hand bekommt, kann den Flash auslesen und käme an
  WLAN-Passwort und Token. Gegenmittel: Der Token gilt nur für
  `/api/tablet/*`, läuft ab und ist in der Weboberfläche einzeln zurückziehbar
  (ab Etappe 7). Flash-Verschlüsselung ist bewusst nicht aktiviert – sie macht
  das Gerät unwiederbringlich unbrauchbar, wenn der Schlüssel verloren geht.
* Die Root-CA steckt fest in der Firmware. Wird die CA auf dem Pi neu erzeugt,
  muss das Tablet neu geflasht werden.
* Der Geräte-Token und die Tablet-Schnittstelle entstehen erst in Etappe 2.
  Bis dahin kann das Tablet nur die Startseite des Pi anfragen.
