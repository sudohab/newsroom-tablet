#include <time.h>

#include <vector>

#include "fonts/ui_fonts.h"
#include "lvgl_port/lvgl_v8_port.h"
#include "tablet_data.h"
#include "tablet_state.h"
#include "ui_pages.h"
#include "ui_theme.h"

// Die Podcast-Seite.
//
// Zwei Spalten: links die Abos, rechts die Folgen des ausgewählten Abos.
// Antippen einer Folge spielt sie ab – und zwar dort weiter, wo zuletzt
// aufgehört wurde, denn den Stand merkt sich der Pi. Der Schalter „Von vorn"
// übergeht diesen Stand.
//
// Was die Seite bewusst NICHT kann: abonnieren, Feeds ändern, Folgen
// herunterladen oder löschen. Das bleibt der angemeldeten Weboberfläche
// vorbehalten – ein Gerät, das offen auf dem Tisch liegt, soll nichts
// dauerhaft verändern können.
//
// Arbeitsteilung wie auf allen Seiten: Ein Knopfdruck merkt sich nur den
// Wunsch, geholt und geschickt wird in `work()` außerhalb der LVGL-Sperre.

namespace ui_pages {
namespace podcasts {
namespace {

using namespace ui_theme;

constexpr lv_coord_t kWidth = 752;
constexpr lv_coord_t kHeight = 300;
constexpr lv_coord_t kListTop = 50;
constexpr lv_coord_t kSubWidth = 260;          // linke Spalte: Abos
constexpr lv_coord_t kEpisodeLeft = 276;       // rechte Spalte: Folgen
constexpr lv_coord_t kEpisodeWidth = kWidth - kEpisodeLeft;
constexpr lv_coord_t kBarHeight = 46;
constexpr lv_coord_t kEpisodeHeight = kHeight - kListTop - kBarHeight - 8;
constexpr lv_coord_t kToggleWidth = 96;
// Hoehe einer Folgenzeile: 8 Rand + Titelzeile + 2 + Angabenzeile + 8 Rand.
// Die beiden Zeilenhoehen sind FEST -- siehe Kommentar in rebuildEpisodes().
constexpr lv_coord_t kTitleLine = 28;
constexpr lv_coord_t kInfoLine = 22;
constexpr lv_coord_t kRowHeight = 8 + kTitleLine + 2 + kInfoLine + 8;
constexpr lv_coord_t kTitleWidth = kEpisodeWidth - kToggleWidth - 16;

enum class Job { None, LoadSubscriptions, LoadEpisodes, Play, MarkPlayed, Stop };
volatile Job job = Job::None;

lv_obj_t *page = nullptr;
lv_obj_t *status = nullptr;
lv_obj_t *subList = nullptr;
lv_obj_t *episodeArea = nullptr;
lv_obj_t *switchFromStart = nullptr;

std::vector<tablet_data::Podcast> subscriptions;
std::vector<tablet_data::Episode> episodes;
int selectedSub = -1;
bool loaded = false;
bool quietTime = false;

String pendingPodcastId;
String pendingEpisodeId;
bool pendingPlayed = false;

// „42 Min" oder „1 Std 12 Min" – Sekunden liest niemand gern.
String durationText(int seconds) {
    if (seconds <= 0) return String();
    const int minutes = (seconds + 30) / 60;
    if (minutes < 60) return String(minutes) + " Min";
    return String(minutes / 60) + " Std " + String(minutes % 60) + " Min";
}

// „18.09." – das Jahr steht nur dabei, wenn es ein anderes ist als heute.
String dateText(long publishedTs) {
    if (publishedTs <= 0) return String();
    const time_t stamp = static_cast<time_t>(publishedTs);
    struct tm when;
    if (localtime_r(&stamp, &when) == nullptr) return String();

    char buffer[16];
    const time_t now = time(nullptr);
    struct tm today;
    const bool sameYear = (localtime_r(&now, &today) != nullptr)
                          && (today.tm_year == when.tm_year);
    strftime(buffer, sizeof(buffer), sameYear ? "%d.%m." : "%d.%m.%Y", &when);
    return String(buffer);
}

// Zweite Zeile einer Folge: Datum, Dauer und der Stand.
String episodeInfo(const tablet_data::Episode &episode) {
    String text = dateText(episode.publishedTs);
    const String duration = durationText(episode.duration);
    if (!duration.isEmpty()) text += (text.isEmpty() ? "" : " · ") + duration;
    if (episode.played) {
        text += (text.isEmpty() ? "" : " · ") + String("gehört");
    } else if (episode.position > 30) {
        // Angefangen: zeigen, wie weit – sonst weiß man nicht, worauf das
        // Antippen aufsetzt.
        text += (text.isEmpty() ? "" : " · ") + String("bei ")
              + durationText(episode.position);
    }
    if (episode.video) text += " · Video";
    return text;
}

void onSubscription(lv_event_t *event) {
    const uint32_t index = lv_obj_get_index(lv_event_get_target(event));
    if (index >= subscriptions.size()) return;
    selectedSub = static_cast<int>(index);
    lv_label_set_text(status, "Lade Folgen …");
    job = Job::LoadEpisodes;
}

void onEpisode(lv_event_t *event) {
    const auto index = reinterpret_cast<intptr_t>(
        lv_obj_get_user_data(lv_event_get_target(event)));
    if (index < 0 || index >= static_cast<intptr_t>(episodes.size())) return;
    if (selectedSub < 0 || selectedSub >= static_cast<int>(subscriptions.size())) return;

    pendingPodcastId = subscriptions[selectedSub].id;
    pendingEpisodeId = episodes[index].id;
    lv_label_set_text(status, "Starte …");
    job = Job::Play;
}

void onToggle(lv_event_t *event) {
    const auto index = reinterpret_cast<intptr_t>(
        lv_obj_get_user_data(lv_event_get_target(event)));
    if (index < 0 || index >= static_cast<intptr_t>(episodes.size())) return;

    pendingEpisodeId = episodes[index].id;
    pendingPlayed = !episodes[index].played;
    // Sofort im Bild ändern, damit der Druck spürbar ist; bestätigt wird es,
    // wenn die Antwort da ist.
    episodes[index].played = pendingPlayed;
    job = Job::MarkPlayed;
}

void onStop(lv_event_t *) {
    lv_label_set_text(status, "Halte an …");
    job = Job::Stop;
}

// --- Aufbau der beiden Listen (nur unter der LVGL-Sperre aufrufen) ----------

void rebuildSubscriptions() {
    lv_obj_clean(subList);
    for (const auto &podcast : subscriptions) {
        lv_obj_t *btn = lv_list_add_btn(subList, nullptr, podcast.title.c_str());
        styleListButton(btn);
        // Ein Listenknopf laesst seine Beschriftung von Haus aus endlos
        // durchlaufen. Bei kurzen Sendernamen faellt das nicht auf, bei
        // Podcasttiteln liefe staendig eine Bewegung -- und jede Bewegung
        // heisst neu zeichnen. Also abschneiden.
        lv_obj_t *label = lv_obj_get_child(btn, 0);
        if (label != nullptr) lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_add_event_cb(btn, onSubscription, LV_EVENT_CLICKED, nullptr);
    }
}

void rebuildEpisodes() {
    lv_obj_clean(episodeArea);
    for (size_t i = 0; i < episodes.size(); ++i) {
        const tablet_data::Episode &episode = episodes[i];

        // Eine Zeile: breiter Knopf mit Titel und Angaben, daneben der
        // Umschalter „gehört". Die Breiten stehen fest, der Abstand kommt vom
        // Flex-Layout – so können die beiden nie übereinanderrutschen.
        lv_obj_t *row = makeButtonBar(episodeArea, 0, 0, kEpisodeWidth - 10, kRowHeight);
        lv_obj_set_style_pad_column(row, 8, 0);

        lv_obj_t *play = lv_btn_create(row);
        lv_obj_set_size(play, kTitleWidth, kRowHeight);
        lv_obj_set_user_data(play, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        lv_obj_add_event_cb(play, onEpisode, LV_EVENT_CLICKED, nullptr);
        styleListButton(play);
        lv_obj_set_style_pad_all(play, 8, 0);

        // Gehörte Folgen blasser – man soll auf einen Blick sehen, was neu ist.
        const uint32_t titleColor = episode.played ? kTextMuted : kText;
        // Beide Beschriftungen bekommen eine FESTE Hoehe, nicht nur eine
        // Breite. Mit blosser Breite waechst eine Beschriftung in die Hoehe,
        // sobald der Text umbricht -- ein langer Folgentitel wurde dann zwei-
        // oder dreizeilig und lief in die Zeile darunter. Erst die feste
        // Hoehe laesst LV_LABEL_LONG_DOT wirken: abschneiden statt umbrechen.
        lv_obj_t *title = lv_label_create(play);
        lv_obj_set_style_text_font(title, &ui_font_22, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(titleColor), 0);
        lv_obj_set_size(title, kTitleWidth - 16, kTitleLine);
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        lv_label_set_text(title, episode.title.c_str());
        lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

        lv_obj_t *info = lv_label_create(play);
        lv_obj_set_style_text_font(info, &ui_font_18, 0);
        lv_obj_set_style_text_color(info, lv_color_hex(kTextMuted), 0);
        lv_obj_set_size(info, kTitleWidth - 16, kInfoLine);
        lv_label_set_long_mode(info, LV_LABEL_LONG_DOT);
        lv_label_set_text(info, episodeInfo(episode).c_str());
        lv_obj_align(info, LV_ALIGN_BOTTOM_LEFT, 0, 0);

        lv_obj_t *toggle = addBarButton(row, episode.played ? "offen" : "gehört",
                                        onToggle, kToggleWidth, kRowHeight);
        lv_obj_set_user_data(toggle, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    }
}

}  // namespace

lv_obj_t *create(lv_obj_t *parent) {
    page = makeSection(parent, 0, 0, kWidth, kHeight);
    makeTitle(page, "Podcast");
    status = makeStatus(page);

    subList = lv_list_create(page);
    lv_obj_set_size(subList, kSubWidth, kHeight - kListTop);
    lv_obj_set_pos(subList, 0, kListTop);
    lv_obj_set_style_bg_opa(subList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(subList, 0, 0);
    lv_obj_set_style_pad_all(subList, 0, 0);
    lv_obj_set_style_pad_row(subList, 8, 0);

    makeSeparator(page, kEpisodeLeft - 10, kListTop, 1, kHeight - kListTop);

    episodeArea = makeScrollArea(page, kEpisodeLeft, kListTop,
                                 kEpisodeWidth, kEpisodeHeight);
    lv_obj_set_style_pad_row(episodeArea, 8, 0);

    lv_obj_t *bar = makeButtonBar(page, kEpisodeLeft, kHeight - kBarHeight,
                                  kEpisodeWidth, kBarHeight);
    // „Von vorn" ist ein Schalter, kein Knopf: Er ändert nichts, sondern legt
    // fest, was das nächste Antippen bedeutet.
    lv_obj_t *label = lv_label_create(bar);
    lv_obj_set_style_text_font(label, &ui_font_18, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(kTextMuted), 0);
    lv_label_set_text(label, "Von vorn");
    switchFromStart = lv_switch_create(bar);
    lv_obj_set_size(switchFromStart, 60, 32);
    lv_obj_set_style_bg_color(switchFromStart, lv_color_hex(kSurface), 0);
    lv_obj_set_style_bg_color(switchFromStart, lv_color_hex(kAccent), LV_PART_INDICATOR
                                                                     | LV_STATE_CHECKED);
    addBarSpacer(bar);
    addBarButton(bar, "Aus", onStop, 120, kBarHeight, kAlarm);
    return page;
}

void activate() {
    if (!loaded) job = Job::LoadSubscriptions;
}

void work() {
    const Job current = job;
    if (current == Job::None) return;
    job = Job::None;

    switch (current) {
        case Job::LoadSubscriptions: {
            const String error = tablet_data::fetchPodcasts(subscriptions, quietTime);
            loaded = error.isEmpty();
            lvgl_port_lock(-1);
            rebuildSubscriptions();
            lv_label_set_text(status,
                !error.isEmpty() ? error.c_str()
                : subscriptions.empty() ? "Keine Abos – in der Weboberfläche anlegen"
                : quietTime ? "Ruhezeit – Abspielen geht jetzt nicht"
                : "Podcast wählen");
            lvgl_port_unlock();
            return;
        }

        case Job::LoadEpisodes: {
            if (selectedSub < 0 || selectedSub >= static_cast<int>(subscriptions.size())) return;
            const String error = tablet_data::fetchEpisodes(subscriptions[selectedSub].id,
                                                            episodes);
            lvgl_port_lock(-1);
            rebuildEpisodes();
            lv_label_set_text(status,
                !error.isEmpty() ? error.c_str()
                : episodes.empty() ? "Keine Folgen"
                : (String(episodes.size()) + " Folgen").c_str());
            lvgl_port_unlock();
            return;
        }

        case Job::Play: {
            bool fromStart = false;
            lvgl_port_lock(-1);
            fromStart = lv_obj_has_state(switchFromStart, LV_STATE_CHECKED);
            lvgl_port_unlock();

            const String error = tablet_data::playEpisode(pendingPodcastId,
                                                          pendingEpisodeId, fromStart);
            lvgl_port_lock(-1);
            lv_label_set_text(status, error.isEmpty() ? "Läuft" : error.c_str());
            lvgl_port_unlock();
            return;
        }

        case Job::MarkPlayed: {
            const String error = tablet_data::markEpisodePlayed(pendingEpisodeId,
                                                                pendingPlayed);
            lvgl_port_lock(-1);
            if (error.isEmpty()) {
                // Bestätigt – die Zeilen neu zeichnen, damit Farbe und
                // Beschriftung zum neuen Stand passen.
                rebuildEpisodes();
                lv_label_set_text(status, pendingPlayed ? "Als gehört vermerkt"
                                                        : "Wieder offen");
            } else {
                // Fehlgeschlagen: die vorweggenommene Änderung zurücknehmen.
                for (auto &episode : episodes) {
                    if (episode.id == pendingEpisodeId) episode.played = !pendingPlayed;
                }
                rebuildEpisodes();
                lv_label_set_text(status, error.c_str());
            }
            lvgl_port_unlock();
            return;
        }

        case Job::Stop: {
            const String error = tablet_state::stopMedia();
            lvgl_port_lock(-1);
            lv_label_set_text(status, error.isEmpty() ? "Aus" : error.c_str());
            lvgl_port_unlock();
            return;
        }

        case Job::None:
            return;
    }
}

}  // namespace podcasts
}  // namespace ui_pages
