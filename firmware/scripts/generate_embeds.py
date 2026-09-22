"""
Eingebettete Dateien vor dem Übersetzen erzeugen.

Hintergrund: Wird Arduino als ESP-IDF-Komponente gebaut, kommen einige
Espressif-Zusatzkomponenten mit (Cloud-Diagnose, RainMaker). Sie betten
Zertifikate ein; daraus erzeugt CMake Assembler-Dateien wie
``https_server.crt.S``. PlatformIO übersetzt die Quellen anschließend mit
seinem eigenen Werkzeug – und erzeugt diese Zwischendateien dabei **nicht**.
Der Bau bricht dann ab mit:

    Source `.pio/build/<env>/https_server.crt.S' not found

Dieses Skript lässt vor dem Übersetzen das IDF-Werkzeug ``ninja`` genau diese
Dateien erzeugen. Es läuft in Sekunden und tut nichts, wenn schon alles da ist.

Eingebunden über ``extra_scripts = pre:scripts/generate_embeds.py``.
"""

import os
import re
import subprocess

Import("env")  # noqa: F821  – von PlatformIO bereitgestellt


def generate_embedded_files(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    ninja_file = os.path.join(build_dir, "build.ninja")
    if not os.path.isfile(ninja_file):
        # Erster Durchlauf: CMake war noch nicht dran.
        print("Hinweis: erster Bau - falls er mit \"*.crt.S not found\" abbricht, "
              "einfach noch einmal starten.")
        return

    with open(ninja_file, encoding="utf-8", errors="replace") as handle:
        content = handle.read()

    # Alle Ziele finden, die im Bauordner selbst liegen und auf .S enden –
    # das sind genau die eingebetteten Daten.
    targets = sorted(set(re.findall(r"^build ([A-Za-z0-9_.-]+\.S)\b", content, re.M)))
    missing = [t for t in targets if not os.path.isfile(os.path.join(build_dir, t))]
    if not missing:
        return

    ninja = os.path.join(env.subst("$PROJECT_PACKAGES_DIR"), "tool-ninja", "ninja")
    if not os.path.isfile(ninja):
        ninja = "ninja"

    print("Erzeuge eingebettete Dateien: %s" % ", ".join(missing))
    try:
        subprocess.run([ninja] + missing, cwd=build_dir, check=True,
                       stdout=subprocess.DEVNULL)
    except (subprocess.CalledProcessError, FileNotFoundError) as exc:
        print("Hinweis: Erzeugen fehlgeschlagen (%s). Bau laeuft weiter." % exc)


# Sofort beim Laden ausfuehren, nicht als Vor-Schritt des Bauens: PlatformIO
# prueft die Quelldateien schon beim Aufbau des Abhaengigkeitsbaums - also
# bevor irgendein Vor-Schritt liefe.
#
# Beim allerersten Bau eines leeren Ordners gibt es noch keine build.ninja.
# Dann passiert hier nichts, CMake laeuft, der Bau bricht einmal ab - und der
# naechste Aufruf erzeugt die Dateien und kommt durch. Deshalb der Hinweis.
generate_embedded_files(None, None, env)  # noqa: F821
