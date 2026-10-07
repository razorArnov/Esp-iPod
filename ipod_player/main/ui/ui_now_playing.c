/*
 * iPod Classic UI — Now Playing View
 * Faithful replica of NowPlayingScreen from AahanDoesGit/iPod-classic-Cm5
 * Fits inside the top display screen (480x372)
 */
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "ui_main.h"
#include "../audio/audio_player.h"

static const char *TAG = "IPOD_NP";

static lv_obj_t *s_np_root     = NULL;
static lv_obj_t *s_art_box     = NULL;
static lv_obj_t *s_art_note    = NULL;
static lv_obj_t *s_title_lbl   = NULL;
static lv_obj_t *s_artist_lbl  = NULL;
static lv_obj_t *s_album_lbl   = NULL;
static lv_obj_t *s_scrub_bar   = NULL;
static lv_obj_t *s_time_cur    = NULL;
static lv_obj_t *s_time_total  = NULL;

static void fmt_time(char *buf, size_t sz, uint32_t sec)
{
    snprintf(buf, sz, "%u:%02u", (unsigned int)(sec / 60), (unsigned int)(sec % 60));
}

void ui_now_playing_seek_delta(int sec)
{
    uint32_t new_pos = 0;
    if (sec < 0 && (uint32_t)(-sec) > g_ipod_state.position_sec) {
        new_pos = 0;
    } else {
        new_pos = g_ipod_state.position_sec + sec;
    }
    if (g_ipod_state.duration_sec > 0 && new_pos > g_ipod_state.duration_sec) {
        new_pos = g_ipod_state.duration_sec;
    }
    audio_player_seek(new_pos);
}

void ui_now_playing_update(const ipod_playback_state_t *st)
{
    if (!st || !s_title_lbl) return;

    /* Title */
    lv_label_set_text(s_title_lbl, st->title[0] ? st->title : "Not Playing");
    lv_label_set_text(s_artist_lbl, st->artist[0] ? st->artist : "iPod Library");
    lv_label_set_text(s_album_lbl, st->album[0] ? st->album : "Audio Track");

    /* Time counters */
    char buf[16];
    fmt_time(buf, sizeof(buf), st->position_sec);
    if (s_time_cur) lv_label_set_text(s_time_cur, buf);

    if (st->duration_sec >= st->position_sec) {
        fmt_time(buf, sizeof(buf), st->duration_sec);
        if (s_time_total) lv_label_set_text(s_time_total, buf);
    }

    /* Progress slider */
    if (st->duration_sec > 0 && s_scrub_bar) {
        int32_t pct = (int32_t)(st->position_sec * 100 / st->duration_sec);
        lv_slider_set_value(s_scrub_bar, pct, LV_ANIM_ON);
    }
}

static void on_scrub_event(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t val = lv_slider_get_value(slider);
    if (g_ipod_state.duration_sec > 0) {
        uint32_t seek_sec = (uint32_t)(val * g_ipod_state.duration_sec / 100);
        audio_player_seek(seek_sec);
    }
}

lv_obj_t *ui_now_playing_view_create(lv_obj_t *parent)
{
    s_np_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_np_root);
    lv_obj_set_size(s_np_root, IPOD_SCREEN_W, IPOD_CONTENT_H);
    lv_obj_align(s_np_root, LV_ALIGN_TOP_LEFT, 0, IPOD_STATUSBAR_H);
    lv_obj_set_style_bg_color(s_np_root, IPOD_COL_BG, 0);
    lv_obj_set_style_bg_opa(s_np_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_np_root, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Album Art Tile (140x140 square with drop shadow) ── */
    s_art_box = lv_obj_create(s_np_root);
    lv_obj_remove_style_all(s_art_box);
    lv_obj_set_size(s_art_box, 140, 140);
    lv_obj_align(s_art_box, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_set_style_bg_color(s_art_box, IPOD_COL_ART_BG, 0);
    lv_obj_set_style_bg_opa(s_art_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_art_box, 6, 0);
    /* Drop shadow */
    lv_obj_set_style_shadow_color(s_art_box, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(s_art_box, 45, 0);
    lv_obj_set_style_shadow_width(s_art_box, 10, 0);
    lv_obj_set_style_shadow_ofs_y(s_art_box, 4, 0);
    lv_obj_clear_flag(s_art_box, LV_OBJ_FLAG_SCROLLABLE);

    s_art_note = lv_label_create(s_art_box);
    lv_label_set_text(s_art_note, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(s_art_note, IPOD_FONT_BIG, 0);
    lv_obj_set_style_text_color(s_art_note, IPOD_COL_TEXT_SUB, 0);
    lv_obj_center(s_art_note);

    /* ── Track Title ── */
    s_title_lbl = lv_label_create(s_np_root);
    lv_label_set_text(s_title_lbl, "Not Playing");
    lv_obj_set_style_text_font(s_title_lbl, IPOD_FONT_ITEM, 0);
    lv_obj_set_style_text_color(s_title_lbl, IPOD_COL_TEXT, 0);
    lv_obj_set_width(s_title_lbl, IPOD_SCREEN_W - 40);
    lv_label_set_long_mode(s_title_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(s_title_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_title_lbl, LV_ALIGN_TOP_MID, 0, 172);

    /* ── Artist ── */
    s_artist_lbl = lv_label_create(s_np_root);
    lv_label_set_text(s_artist_lbl, "iPod Library");
    lv_obj_set_style_text_font(s_artist_lbl, IPOD_FONT_TITLE, 0);
    lv_obj_set_style_text_color(s_artist_lbl, IPOD_COL_TEXT_SUB, 0);
    lv_obj_set_width(s_artist_lbl, IPOD_SCREEN_W - 40);
    lv_label_set_long_mode(s_artist_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_artist_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_artist_lbl, LV_ALIGN_TOP_MID, 0, 198);

    /* ── Album ── */
    s_album_lbl = lv_label_create(s_np_root);
    lv_label_set_text(s_album_lbl, "Audio Track");
    lv_obj_set_style_text_font(s_album_lbl, IPOD_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_album_lbl, IPOD_COL_TEXT_SUB, 0);
    lv_obj_set_width(s_album_lbl, IPOD_SCREEN_W - 40);
    lv_label_set_long_mode(s_album_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_album_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_album_lbl, LV_ALIGN_TOP_MID, 0, 220);

    /* ── Scrub Bar (Progress) ── */
    int scrub_y = 265;
    s_time_cur = lv_label_create(s_np_root);
    lv_label_set_text(s_time_cur, "0:00");
    lv_obj_set_style_text_font(s_time_cur, IPOD_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_time_cur, IPOD_COL_TEXT_SUB, 0);
    lv_obj_align(s_time_cur, LV_ALIGN_TOP_LEFT, 24, scrub_y);

    s_time_total = lv_label_create(s_np_root);
    lv_label_set_text(s_time_total, "0:00");
    lv_obj_set_style_text_font(s_time_total, IPOD_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_time_total, IPOD_COL_TEXT_SUB, 0);
    lv_obj_align(s_time_total, LV_ALIGN_TOP_RIGHT, -24, scrub_y);

    s_scrub_bar = lv_slider_create(s_np_root);
    lv_obj_set_size(s_scrub_bar, 340, 6);
    lv_obj_align(s_scrub_bar, LV_ALIGN_TOP_MID, 0, scrub_y + 4);
    lv_slider_set_range(s_scrub_bar, 0, 100);
    lv_slider_set_value(s_scrub_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_scrub_bar, IPOD_COL_SCRUB_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_scrub_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_scrub_bar, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_scrub_bar, IPOD_COL_SCRUB_FG, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_scrub_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_scrub_bar, IPOD_COL_SCRUB_FG, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_scrub_bar, 4, LV_PART_KNOB);
    lv_obj_add_event_cb(s_scrub_bar, on_scrub_event, LV_EVENT_VALUE_CHANGED, NULL);

    ESP_LOGI(TAG, "Now Playing View built (480x372)");
    return s_np_root;
}
