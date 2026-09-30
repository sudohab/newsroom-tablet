#include <vector>

#include "fonts/ui_fonts.h"
#include "lvgl_port/lvgl_v8_port.h"
#include "tablet_data.h"
#include "ui_pages.h"
#include "ui_theme.h"

// Seite „Drucker".
//
//   links   3D-Drucker (Moonraker): Zustand, Datei, Fortschrittsbalken,
//           Restzeit, Düse und Bett
//   rechts  Bürodrucker (IPP): Zustand, Meldungen (Papier leer …),
//           Tintenstand als vier Balken in der Farbe der Patrone
//
// Beide Drucker fragt newsroom21 ab; das Tablet holt nur das Ergebnis, alle
// zehn Sekunden, solange die Seite offen ist. Ein nicht eingerichteter Drucker
// zeigt nur einen Hinweis.

namespace ui_pages {
namespace printers {
namespace {

using namespace ui_theme;

constexpr lv_coord_t kWidth = 752;
constexpr lv_coord_t kHeight = 300;
constexpr lv_coord_t kColumn = 360;
constexpr lv_coord_t kRight = kWidth - kColumn;      // 392
constexpr uint32_t kRefreshMs = 10000;
constexpr size_t kMaxInks = 4;

lv_obj_t *page = nullptr;
lv_obj_t *status = nullptr;

// 3D-Drucker
lv_obj_t *label3dName = nullptr;
lv_obj_t *label3dState = nullptr;
lv_obj_t *label3dFile = nullptr;
lv_obj_t *bar3d = nullptr;
lv_obj_t *label3dProgress = nullptr;
lv_obj_t *label3dTemps = nullptr;

// Bürodrucker
lv_obj_t *labelOfficeName = nullptr;
lv_obj_t *labelOfficeState = nullptr;
lv_obj_t *labelOfficeReasons = nullptr;
lv_obj_t *inkName[kMaxInks] = {};
lv_obj_t *inkBar[kMaxInks] = {};
lv_obj_t *inkValue[kMaxInks] = {};

tablet_data::Printer3d printer3d;
tablet_data::OfficePrinter office;
uint32_t lastFetchMs = 0;
bool needsRefresh = true;

// Beschriftung mit fester Größe – ohne Größe wüchse sie über die Spalte
// hinaus (siehe Merksatz im BAUPLAN).
lv_obj_t *fixedLabel(const lv_font_t *font, uint32_t color, lv_coord_t x, lv_coord_t y,
                     lv_coord_t w, lv_coord_t h) {
    lv_obj_t *label = lv_label_create(page);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, w, h);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_label_set_text(label, "");
    return label;
}

lv_obj_t *makeBar(lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h) {
    lv_obj_t *bar = lv_bar_create(page);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, w, h);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_radius(bar, h / 2, 0);
    lv_obj_set_style_radius(bar, h / 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, lv_color_hex(kSurface), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(kAccent), LV_PART_INDICATOR);
    lv_obj_set_style_anim_time(bar, 0, 0);
    return bar;
}

String duration(int seconds) {
    const int minutes = (seconds + 59) / 60;
    char text[24];
    if (minutes >= 60) {
        snprintf(text, sizeof(text), "%d:%02d h", minutes / 60, minutes % 60);
    } else {
        snprintf(text, sizeof(text), "%d min", minutes);
    }
    return String(text);
}

String temperature(int actual, int target) {
    if (actual < 0) return "–";
    String text = String(actual);
    if (target > 0) text += " / " + String(target);
    return text + " °C";
}

const char *state3dText(const String &state) {
    if (state == "printing") return "Druckt";
    if (state == "paused") return "Pausiert";
    if (state == "complete") return "Fertig";
    if (state == "standby") return "Bereit";
    if (state == "cancelled") return "Abgebrochen";
    if (state == "error") return "Fehler";
    return "Aus";
}

const char *officeStateText(const String &state) {
    if (state == "idle") return "Bereit";
    if (state == "processing") return "Druckt";
    if (state == "stopped") return "Angehalten";
    return "Aus";
}

void show3d() {
    if (!printer3d.present) {
        lv_label_set_text(label3dName, "3D-Drucker");
        lv_label_set_text(label3dState, "Nicht eingerichtet");
        lv_obj_set_style_text_color(label3dState, lv_color_hex(kTextMuted), 0);
        lv_label_set_text(label3dFile, "PRINTER_* in .env am Pi");
        lv_obj_add_flag(bar3d, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(label3dProgress, "");
        lv_label_set_text(label3dTemps, "");
        return;
    }
    const bool active = printer3d.state == "printing" || printer3d.state == "paused";
    lv_label_set_text(label3dName, printer3d.name.c_str());
    lv_label_set_text(label3dState, state3dText(printer3d.state));
    lv_obj_set_style_text_color(
        label3dState,
        lv_color_hex(printer3d.state == "error" ? kAlarm
                     : printer3d.state == "offline" ? kTextMuted
                     : active ? kAccent : kText),
        0);
    lv_label_set_text(label3dFile, printer3d.state == "offline" ? "" : printer3d.file.c_str());

    if (active && printer3d.progress >= 0) {
        lv_obj_clear_flag(bar3d, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(bar3d, printer3d.progress, LV_ANIM_OFF);
        String text = String(printer3d.progress) + " %";
        if (printer3d.remaining >= 0) text += "  ·  noch " + duration(printer3d.remaining);
        lv_label_set_text(label3dProgress, text.c_str());
    } else {
        lv_obj_add_flag(bar3d, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(label3dProgress, "");
    }

    if (printer3d.state == "offline") {
        lv_label_set_text(label3dTemps, "");
    } else {
        const String temps = "Düse " + temperature(printer3d.nozzle, printer3d.nozzleTarget) +
                             "    Bett " + temperature(printer3d.bed, printer3d.bedTarget);
        lv_label_set_text(label3dTemps, temps.c_str());
    }
}

void showOffice() {
    if (!office.present) {
        lv_label_set_text(labelOfficeName, "Bürodrucker");
        lv_label_set_text(labelOfficeState, "Nicht eingerichtet");
        lv_obj_set_style_text_color(labelOfficeState, lv_color_hex(kTextMuted), 0);
        lv_label_set_text(labelOfficeReasons, "OFFICE_PRINTER_* in .env am Pi");
        lv_obj_set_style_text_color(labelOfficeReasons, lv_color_hex(kTextMuted), 0);
    } else {
        lv_label_set_text(labelOfficeName, office.name.c_str());
        lv_label_set_text(labelOfficeState, officeStateText(office.state));
        lv_obj_set_style_text_color(
            labelOfficeState,
            lv_color_hex(office.state == "offline" ? kTextMuted
                         : office.state == "stopped" ? kAlarm
                         : office.state == "processing" ? kAccent : kText),
            0);
        String reasons;
        for (const String &reason : office.reasons) {
            if (!reasons.isEmpty()) reasons += "  ·  ";
            reasons += reason;
        }
        lv_label_set_text(labelOfficeReasons, reasons.c_str());
        lv_obj_set_style_text_color(labelOfficeReasons, lv_color_hex(kAlarm), 0);
    }

    for (size_t i = 0; i < kMaxInks; ++i) {
        const bool used = office.present && i < office.inks.size();
        if (!used) {
            lv_obj_add_flag(inkName[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(inkBar[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(inkValue[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const tablet_data::Ink &ink = office.inks[i];
        lv_obj_clear_flag(inkName[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(inkBar[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(inkValue[i], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(inkName[i], ink.name.c_str());
        lv_bar_set_value(inkBar[i], ink.level < 0 ? 0 : ink.level, LV_ANIM_OFF);
        // Schwarz auf schwarzem Grund sähe man nicht – dafür ein helles Grau.
        const uint32_t color = ink.color < 0x202020 ? 0xd1d1d6 : ink.color;
        lv_obj_set_style_bg_color(inkBar[i], lv_color_hex(color), LV_PART_INDICATOR);
        lv_label_set_text(inkValue[i], ink.level < 0 ? "?" : (String(ink.level) + " %").c_str());
        lv_obj_set_style_text_color(inkValue[i],
                                    lv_color_hex(ink.level >= 0 && ink.level <= 10 ? kAlarm : kText), 0);
    }
}

}  // namespace

lv_obj_t *create(lv_obj_t *parent) {
    page = makeSection(parent, 0, 0, kWidth, kHeight);
    makeTitle(page, "Drucker");
    status = makeStatus(page);

    // --- links: 3D-Drucker -------------------------------------------------
    label3dName = fixedLabel(&ui_font_18, kTextMuted, 0, 52, kColumn, 24);
    label3dState = fixedLabel(&ui_font_30, kText, 0, 78, kColumn, 40);
    label3dFile = fixedLabel(&ui_font_18, kTextMuted, 0, 122, kColumn, 24);
    bar3d = makeBar(0, 158, kColumn, 14);
    label3dProgress = fixedLabel(&ui_font_22, kText, 0, 182, kColumn, 30);
    label3dTemps = fixedLabel(&ui_font_18, kTextMuted, 0, 228, kColumn, 24);

    makeSeparator(page, kColumn + 15, 52, 1, kHeight - 60);

    // --- rechts: Bürodrucker -----------------------------------------------
    labelOfficeName = fixedLabel(&ui_font_18, kTextMuted, kRight, 52, kColumn, 24);
    labelOfficeState = fixedLabel(&ui_font_30, kText, kRight, 78, kColumn, 40);
    labelOfficeReasons = fixedLabel(&ui_font_18, kAlarm, kRight, 122, kColumn, 24);
    for (size_t i = 0; i < kMaxInks; ++i) {
        const lv_coord_t y = 156 + i * 34;
        inkName[i] = fixedLabel(&ui_font_18, kText, kRight, y, 100, 24);
        inkBar[i] = makeBar(kRight + 104, y + 5, 180, 12);
        inkValue[i] = fixedLabel(&ui_font_18, kText, kRight + 296, y, 64, 24);
        lv_obj_set_style_text_align(inkValue[i], LV_TEXT_ALIGN_RIGHT, 0);
    }

    show3d();
    showOffice();
    lv_label_set_text(label3dState, "…");
    lv_label_set_text(labelOfficeState, "…");
    return page;
}

void activate() { needsRefresh = true; }

void work() {
    const uint32_t now = millis();
    if (!needsRefresh && now - lastFetchMs < kRefreshMs) return;
    needsRefresh = false;
    lastFetchMs = now;

    // Geholt wird ohne LVGL-Sperre (bis zu acht Sekunden), gezeichnet mit.
    const String error = tablet_data::fetchPrinters(printer3d, office);

    lvgl_port_lock(-1);
    lv_label_set_text(status, error.c_str());
    if (error.isEmpty()) {
        show3d();
        showOffice();
    }
    lvgl_port_unlock();
}

}  // namespace printers
}  // namespace ui_pages
