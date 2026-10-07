/*
 * ui_volume_screen.c — Dedicated iPod Volume Adjustment Screen (LVGL 9)
 * Allows increasing/decreasing volume using the Click Wheel:
 *   - Right/CW rotation: Increase volume
 *   - Left/CCW rotation: Decrease volume
 *   - MENU or Center button: Return to Menu
 */
#include "ui_volume_screen.h"
#include "ui_main.h"
#include "../audio/audio_player.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "UI_VOL";

static lv_obj_t *s_vol_root      = NULL;
static lv_obj_t *s_vol_icon_box  = NULL;
static lv_obj_t *s_vol_icon      = NULL;
static lv_obj_t *s_vol_title     = NULL;
static lv_obj_t *s_vol_pct_lbl   = NULL;
static lv_obj_t *s_vol_slider    = NULL;
static lv_obj_t *s_vol_min_ic    = NULL;
static lv_obj_t *s_vol_max_ic    = NULL;
static lv_obj_t *s_vol_hint      = NULL;

static void on_slider_event(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int val = (int)lv_slider_get_value(slider);
    audio_player_set_volume(val);
    ui_volume_screen_update();
}

lv_obj_t *ui_volume_screen_create(lv_obj_t *parent)
{
    s_vol_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_vol_root);
    lv_obj_set_size(s_vol_root, IPOD_SCREEN_W, IPOD_CONTENT_H);
    lv_obj_align(s_vol_root, LV_ALIGN_TOP_LEFT, 0, IPOD_STATUSBAR_H);
    lv_obj_set_style_bg_color(s_vol_root, IPOD_COL_BG, 0);
    lv_obj_set_style_bg_opa(s_vol_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_vol_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_vol_root, LV_OBJ_FLAG_HIDDEN);

    /* ── Top Icon Card (140x140 with drop shadow) ── */
    s_vol_icon_box = lv_obj_create(s_vol_root);
    lv_obj_remove_style_all(s_vol_icon_box);
    lv_obj_set_size(s_vol_icon_box, 140, 140);
    lv_obj_align(s_vol_icon_box, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_set_style_bg_color(s_vol_icon_box, IPOD_COL_ART_BG, 0);
    lv_obj_set_style_bg_opa(s_vol_icon_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_vol_icon_box, 6, 0);
    lv_obj_set_style_shadow_color(s_vol_icon_box, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(s_vol_icon_box, 45, 0);
    lv_obj_set_style_shadow_width(s_vol_icon_box, 10, 0);
    lv_obj_set_style_shadow_ofs_y(s_vol_icon_box, 4, 0);
    lv_obj_clear_flag(s_vol_icon_box, LV_OBJ_FLAG_SCROLLABLE);

    s_vol_icon = lv_label_create(s_vol_icon_box);
    lv_label_set_text(s_vol_icon, LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_font(s_vol_icon, IPOD_FONT_BIG, 0);
    lv_obj_set_style_text_color(s_vol_icon, IPOD_COL_TEXT_SUB, 0);
    lv_obj_center(s_vol_icon);

    /* ── Title: Volume ── */
    s_vol_title = lv_label_create(s_vol_root);
    lv_label_set_text(s_vol_title, "Volume");
    lv_obj_set_style_text_font(s_vol_title, IPOD_FONT_ITEM, 0);
    lv_obj_set_style_text_color(s_vol_title, IPOD_COL_TEXT, 0);
    lv_obj_set_style_text_align(s_vol_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_vol_title, LV_ALIGN_TOP_MID, 0, 172);

    /* ── Percentage Value (e.g. "90%") ── */
    s_vol_pct_lbl = lv_label_create(s_vol_root);
    lv_label_set_text(s_vol_pct_lbl, "90%");
    lv_obj_set_style_text_font(s_vol_pct_lbl, IPOD_FONT_ITEM, 0);
    lv_obj_set_style_text_color(s_vol_pct_lbl, IPOD_COL_SEL_BOT, 0);
    lv_obj_set_style_text_align(s_vol_pct_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_vol_pct_lbl, LV_ALIGN_TOP_MID, 0, 198);

    /* ── Left Mute / Low Icon ── */
    int bar_y = 240;
    s_vol_min_ic = lv_label_create(s_vol_root);
    lv_label_set_text(s_vol_min_ic, LV_SYMBOL_MUTE);
    lv_obj_set_style_text_font(s_vol_min_ic, IPOD_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_vol_min_ic, IPOD_COL_TEXT_SUB, 0);
    lv_obj_align(s_vol_min_ic, LV_ALIGN_TOP_LEFT, 24, bar_y - 4);

    /* ── Right Max Volume Icon ── */
    s_vol_max_ic = lv_label_create(s_vol_root);
    lv_label_set_text(s_vol_max_ic, LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_font(s_vol_max_ic, IPOD_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_vol_max_ic, IPOD_COL_TEXT_SUB, 0);
    lv_obj_align(s_vol_max_ic, LV_ALIGN_TOP_RIGHT, -24, bar_y - 4);

    /* ── Volume Slider ── */
    s_vol_slider = lv_slider_create(s_vol_root);
    lv_obj_set_size(s_vol_slider, 340, 8);
    lv_obj_align(s_vol_slider, LV_ALIGN_TOP_MID, 0, bar_y);
    lv_slider_set_range(s_vol_slider, 0, 100);
    lv_slider_set_value(s_vol_slider, 90, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_vol_slider, IPOD_COL_SCRUB_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_vol_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_vol_slider, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_vol_slider, IPOD_COL_SCRUB_FG, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_vol_slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_vol_slider, IPOD_COL_SCRUB_FG, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_vol_slider, 5, LV_PART_KNOB);
    lv_obj_add_event_cb(s_vol_slider, on_slider_event, LV_EVENT_VALUE_CHANGED, NULL);

    /* ── Subtitle Hint ── */
    s_vol_hint = lv_label_create(s_vol_root);
    lv_label_set_text(s_vol_hint, "Rotate Wheel to Adjust");
    lv_obj_set_style_text_font(s_vol_hint, IPOD_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_vol_hint, IPOD_COL_TEXT_SUB, 0);
    lv_obj_set_style_text_align(s_vol_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_vol_hint, LV_ALIGN_TOP_MID, 0, 275);

    ESP_LOGI(TAG, "Volume Screen created.");
    return s_vol_root;
}

void ui_volume_screen_update(void)
{
    if (!s_vol_root) return;

    int vol = audio_player_get_volume();

    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", vol);
    if (s_vol_pct_lbl) {
        lv_label_set_text(s_vol_pct_lbl, buf);
    }

    if (s_vol_slider) {
        lv_slider_set_value(s_vol_slider, vol, LV_ANIM_OFF);
    }

    if (s_vol_icon) {
        if (vol == 0) {
            lv_label_set_text(s_vol_icon, LV_SYMBOL_MUTE);
        } else if (vol < 50) {
            lv_label_set_text(s_vol_icon, LV_SYMBOL_VOLUME_MID);
        } else {
            lv_label_set_text(s_vol_icon, LV_SYMBOL_VOLUME_MAX);
        }
    }
}

void ui_volume_screen_adjust(int delta)
{
    audio_player_adjust_volume(delta);
    ui_volume_screen_update();
    ESP_LOGI(TAG, "Volume adjusted (delta=%d) -> %d%%", delta, audio_player_get_volume());
}

void ui_volume_screen_open(void)
{
    if (s_vol_root) {
        lv_obj_remove_flag(s_vol_root, LV_OBJ_FLAG_HIDDEN);
        ui_volume_screen_update();
    }
}

void ui_volume_screen_close(void)
{
    if (s_vol_root) {
        lv_obj_add_flag(s_vol_root, LV_OBJ_FLAG_HIDDEN);
    }
}

bool ui_volume_screen_is_active(void)
{
    if (!s_vol_root) return false;
    return !lv_obj_has_flag(s_vol_root, LV_OBJ_FLAG_HIDDEN);
}
