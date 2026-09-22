# ESP-IDF-Projektdatei für newsroom-tablet.
#
# PlatformIO legt diese Datei selbst an, wenn sie fehlt. Sie steht hier im Git,
# weil wir eine Zeile ergänzt haben:
#
# Der Arduino-Kern bringt als ESP-IDF-Komponente einen ganzen Schwung
# Zusatzkomponenten mit (Sprachverarbeitung, Modbus, Cloud-Diagnose …), die
# dieses Projekt nicht braucht. ESP Insights – Espressifs Cloud-Diagnose –
# bricht den Bau sogar ab, weil es ein Serverzertifikat vom Projekt erwartet.
# Deshalb werden die nicht benötigten Komponenten ausgeschlossen: Das spart
# Bauzeit, Programmspeicher und eine Fehlerquelle.
cmake_minimum_required(VERSION 3.16.0)

set(EXCLUDE_COMPONENTS
    espressif__esp_insights
    espressif__esp_diagnostics
    espressif__esp_diag_data_store
    espressif__esp_rainmaker
    espressif__rmaker_common
    espressif__esp-sr
    espressif__esp-dl
    espressif__dl_fft
    espressif__esp-dsp
    espressif__esp-modbus
    espressif__esp-serial-flasher
    chmorgan__esp-libhelix-mp3
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(firmware)
