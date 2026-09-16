#!/usr/bin/env python3
"""
Geräte-Token und Pi-Adresse in das Tablet schreiben.

Der Token wird **verdeckt** abgefragt (wie ein Passwort) und über die
USB-Leitung direkt in den Flash des Tablets geschrieben. Er landet damit
weder in der Shell-Historie noch in einer Datei noch in einem Chat.

Aufruf::

    scripts/provision.py                 # fragt Token ab, Port /dev/ttyACM0
    scripts/provision.py --status        # nur nachsehen, was hinterlegt ist
    scripts/provision.py --host 192.168.178.113
    scripts/provision.py --port /dev/ttyACM1

Den Token erzeugt man auf dem Pi mit::

    sudo /opt/newsroom21/scripts/secrets.sh     → "9) Tablet-Token erzeugen"

Er ist dort nur ein einziges Mal sichtbar. Geht er verloren, erzeugt man
einfach einen neuen – der alte gilt dann nicht mehr.

Voraussetzung: pyserial. Ist es nicht installiert, tut es das Python aus
PlatformIO::

    ~/.platformio/penv/bin/python scripts/provision.py
"""

import argparse
import getpass
import re
import sys
import time

try:
    import serial  # pyserial
except ImportError:  # pragma: no cover - Hinweis statt Absturz
    sys.exit("pyserial fehlt. Aufruf mit: ~/.platformio/penv/bin/python "
             "scripts/provision.py")


# Muster wie in der Firmware (settings_store.cpp): 32 Zufallsbytes,
# URL-sicher kodiert. Hier wird schon am Rechner geprüft, damit ein Tippfehler
# sofort auffällt und nicht erst als "Token abgelehnt" am Pi.
TOKEN_RE = re.compile(r"^[A-Za-z0-9_-]{43,128}$")
HOST_RE = re.compile(r"^[A-Za-z0-9.-]{1,63}$")


def open_port(port: str) -> "serial.Serial":
    try:
        connection = serial.Serial(port, 115200, timeout=1)
    except serial.SerialException as exc:
        sys.exit(f"Port {port} nicht nutzbar: {exc}")
    # Das Öffnen des Ports setzt den ESP32 zurück (DTR/RTS). Kurz warten, bis
    # er wieder da ist, sonst geht der erste Befehl ins Leere.
    time.sleep(1.5)
    connection.reset_input_buffer()
    return connection


def send(connection, command: str, quiet_value: str = "") -> list:
    """
    Schickt eine Befehlszeile und sammelt die Antwort.

    ``quiet_value`` ist der geheime Teil: Er wird gesendet, aber niemals auf
    dem Bildschirm ausgegeben – auch nicht in einer Fehlermeldung.
    """
    line = command if not quiet_value else f"{command} {quiet_value}"
    connection.write((line + "\n").encode("utf-8"))
    connection.flush()
    # Den geheimen Teil sofort unkenntlich machen
    line = ""

    answers = []
    deadline = time.time() + 3
    while time.time() < deadline:
        raw = connection.readline()
        if not raw:
            continue
        text = raw.decode("utf-8", "replace").rstrip()
        if text:
            answers.append(text)
            deadline = time.time() + 0.5  # weitere Zeilen abwarten
    return answers


def main() -> int:
    parser = argparse.ArgumentParser(description="newsroom-tablet einrichten")
    parser.add_argument("--port", default="/dev/ttyACM0",
                        help="serieller Port des Tablets (Vorgabe: /dev/ttyACM0)")
    parser.add_argument("--host", default="",
                        help="Adresse von newsroom21, z. B. 192.168.178.113")
    parser.add_argument("--status", action="store_true",
                        help="nur anzeigen, was hinterlegt ist")
    parser.add_argument("--forget-token", action="store_true",
                        help="hinterlegten Token loeschen")
    args = parser.parse_args()

    connection = open_port(args.port)
    try:
        if args.status:
            for answer in send(connection, "STATUS"):
                print(answer)
            return 0

        if args.forget_token:
            for answer in send(connection, "FORGET-TOKEN"):
                print(answer)
            return 0

        if args.host:
            if not HOST_RE.match(args.host):
                sys.exit("Adresse ungueltig: nur Buchstaben, Ziffern, Punkt, Bindestrich")
            for answer in send(connection, "HOST", args.host):
                print(answer)

        print("Token vom Pi (sudo scripts/secrets.sh -> 9). Die Eingabe bleibt unsichtbar.")
        token = getpass.getpass("Token: ").strip()
        if not token:
            print("Nichts eingegeben, nichts geaendert.")
            return 1
        if not TOKEN_RE.match(token):
            # Bewusst ohne den eingegebenen Wert in der Meldung.
            sys.exit("Das sieht nicht nach einem Token aus (erwartet 43 Zeichen "
                     "aus A-Z a-z 0-9 - _). Nichts geaendert.")

        answers = send(connection, "TOKEN", token)
        token = ""  # nicht laenger als noetig im Speicher halten
        for answer in answers:
            print(answer)

        print()
        for answer in send(connection, "STATUS"):
            print(answer)
        return 0
    finally:
        connection.close()


if __name__ == "__main__":
    sys.exit(main())
