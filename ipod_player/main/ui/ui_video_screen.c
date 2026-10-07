/*
 * ui_video_screen.c — In-App Video Player Screen (LVGL 9)
 * Wraps video_player.c with a full iPod-style playback UI:
 *   - Full display canvas (480x360) for MJPEG frames
 *   - Bottom bar (40px): title, progress, play/pause indicator
 *   - Click Wheel: Play/Pause = toggle, MENU = stop & return
 */
#include "ui_main.h"
#include "media_scanner.h"
#include "video_player.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "UI_VID";

static lv_obj_t *s_root    = NULL;
static lv_obj_t *s_canvas  = NULL;
static lv_obj_t *s_bar     = NULL;
static lv_obj_t *s_title   = NULL;
static lv_obj_t *s_time    = NULL;
static lv_obj_t *s_scrub   = NULL;
static lv_obj_t *s_play_ic = NULL;

#define CANVAS_W  480
#define CANVAS_H  360

static int s_cur_index = 0;

/* ── Create ─────────────────────────────────────────────────────────────── */
lv_obj_t *ui_video_screen_create(lv_obj_t *parent)
{
    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, IPOD_SCREEN_W, IPOD_DISPLAY_H);
    lv_obj_align(s_root, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);

    /* Video frame image widget (direct PSRAM pixel buffer) */
    s_canvas = lv_image_create(s_root);
    lv_obj_set_size(s_canvas, CANVAS_W, CANVAS_H);
    lv_obj_align(s_canvas, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_image_set_inner_align(s_canvas, LV_IMAGE_ALIGN_CENTER);
    lv_obj_set_style_bg_color(s_canvas, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_canvas, LV_OPA_COVER, 0);

    /* ── Bottom bar (40px: y=360..399) ── */
    s_bar = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_bar);
    lv_obj_set_size(s_bar, IPOD_SCREEN_W, 40);
    lv_obj_align(s_bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x141414), 0);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_play_ic = lv_label_create(s_bar);
    lv_obj_set_style_text_font(s_play_ic, IPOD_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_play_ic, lv_color_hex(0x4FA5FF), 0);
    lv_obj_align(s_play_ic, LV_ALIGN_LEFT_MID, 8, -4);
    lv_label_set_text(s_play_ic, LV_SYMBOL_PLAY);

    s_title = lv_label_create(s_bar);
    lv_obj_set_style_text_font(s_title, IPOD_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_title, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_title, IPOD_SCREEN_W - 120);
    lv_obj_align(s_title, LV_ALIGN_LEFT_MID, 28, -4);
    lv_label_set_text(s_title, "Video");

    s_time = lv_label_create(s_bar);
    lv_obj_set_style_text_font(s_time, IPOD_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_time, lv_color_hex(0x888888), 0);
    lv_obj_align(s_time, LV_ALIGN_RIGHT_MID, -8, -4);
    lv_label_set_text(s_time, "0:00");

    s_scrub = lv_slider_create(s_bar);
    lv_obj_set_size(s_scrub, IPOD_SCREEN_W - 16, 4);
    lv_obj_align(s_scrub, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_slider_set_range(s_scrub, 0, 100);
    lv_slider_set_value(s_scrub, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_scrub, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_scrub, lv_color_hex(0x4FA5FF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_scrub, lv_color_hex(0x4FA5FF), LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_scrub, 0, LV_PART_KNOB);

    ESP_LOGI(TAG, "Video Screen created (canvas %dx%d).", CANVAS_W, CANVAS_H);
    return s_root;
}

void ui_video_screen_open(int index)
{
    int total = media_get_video_count();
    if (total == 0) {
        ESP_LOGW(TAG, "No videos found on SD card.");
        return;
    }
    s_cur_index = index;
    if (s_cur_index < 0) s_cur_index = 0;
    if (s_cur_index >= total) s_cur_index = total - 1;

    const media_video_t *v = media_get_video(s_cur_index);
    if (!v) return;

    lv_label_set_text(s_title, v->name);
    lv_label_set_text(s_time, "0:00");
    lv_slider_set_value(s_scrub, 0, LV_ANIM_OFF);
    lv_label_set_text(s_play_ic, LV_SYMBOL_PLAY);

    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    ui_set_view(VIEW_VIDEO_PLAYER);

    video_player_play(v->path, s_canvas);
    ESP_LOGI(TAG, "Playing video: %s", v->path);
}

void ui_video_screen_close(void)
{
    video_player_stop();
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    ui_set_view(VIEW_MENU);
}

void ui_video_screen_tick(void)
{
    if (!ui_video_screen_is_active()) return;

    video_status_t st;
    if (!video_player_get_status(&st)) return;

    /* Update play/pause icon */
    lv_label_set_text(s_play_ic,
        (st.state == VIDEO_STATE_PAUSED) ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);

    /* Update time */
    if (s_time) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%lu:%02lu",
                 (unsigned long)st.position_sec / 60,
                 (unsigned long)st.position_sec % 60);
        lv_label_set_text(s_time, buf);
    }

    /* Update scrub bar */
    if (s_scrub && st.duration_sec > 0) {
        int32_t pct = (int32_t)(st.position_sec * 100 / st.duration_sec);
        lv_slider_set_value(s_scrub, pct, LV_ANIM_OFF);
    }

    /* Auto-return when finished */
    if (st.state == VIDEO_STATE_FINISHED || st.state == VIDEO_STATE_ERROR) {
        ui_video_screen_close();
    }
}

bool ui_video_screen_is_active(void)
{
    if (!s_root) return false;
    return !lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}

void ui_video_screen_next(void)
{
    int total = media_get_video_count();
    if (total <= 1) return;
    int next = (s_cur_index + 1) % total;
    ui_video_screen_open(next);
}

void ui_video_screen_prev(void)
{
    int total = media_get_video_count();
    if (total <= 1) return;
    int prev = (s_cur_index - 1 + total) % total;
    ui_video_screen_open(prev);
}
