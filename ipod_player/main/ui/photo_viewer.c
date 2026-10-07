/*
 * photo_viewer.c — LVGL 9 Photo Viewer
 * Full-screen display of JPEG/PNG/BMP with slideshow and Click Wheel nav.
 *
 * Uses LVGL's built-in image decoder (TJPGD for JPEG, LODEPNG for PNG, BMP).
 * Images must be on the FS mounted at 'S:' -> /sdcard.
 */
#include "photo_viewer.h"
#include "media_scanner.h"
#include "ui_main.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "PHOTO_VIEW";

/* ── State ──────────────────────────────────────────────────────────────── */
static lv_obj_t *s_root      = NULL;  /* full viewer screen */
static lv_obj_t *s_img       = NULL;  /* lv_image widget */
static lv_obj_t *s_name_lbl  = NULL;  /* filename label */
static lv_obj_t *s_counter   = NULL;  /* "3 / 12" label */
static lv_obj_t *s_ss_icon   = NULL;  /* slideshow icon */
static lv_obj_t *s_bar       = NULL;  /* top bar */

static int  s_cur_index    = 0;
static int  s_total        = 0;
static bool s_slideshow    = false;
static int  s_ss_ticks     = 0;         /* ticks since last advance */
#define SS_ADVANCE_TICKS   16           /* at 250ms tick = 4 seconds */

/* Path buffer for LVGL 'S:' FS prefix */
static char s_lvgl_path[MEDIA_PATH_LEN + 4];

/* ── Helpers ─────────────────────────────────────────────────────────────── */
static void make_lvgl_path(const char *sdcard_path)
{
    /* LVGL POSIX FS has root at '/sdcard', so strip '/sdcard' prefix */
    const char *rel = sdcard_path;
    if (strncmp(rel, "/sdcard", 7) == 0) {
        rel += 7;
    }
    if (rel[0] == '/') {
        snprintf(s_lvgl_path, sizeof(s_lvgl_path), "S:%s", rel);
    } else {
        snprintf(s_lvgl_path, sizeof(s_lvgl_path), "S:/%s", rel);
    }
}

static void update_display(void)
{
    s_total = media_get_photo_count();
    if (s_total == 0 || s_cur_index < 0 || s_cur_index >= s_total) return;

    const media_photo_t *photo = media_get_photo(s_cur_index);
    if (!photo) return;

    ESP_LOGI(TAG, "Showing photo %d/%d: %s", s_cur_index + 1, s_total, photo->path);

    make_lvgl_path(photo->path);

    /* Direct 1:1 scale for streaming TJPGD MCU decoder */
    lv_image_set_scale(s_img, 256);
    lv_image_set_src(s_img, s_lvgl_path);
    lv_obj_invalidate(s_img);

    /* Update labels */
    lv_label_set_text(s_name_lbl, photo->name);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d / %d", s_cur_index + 1, s_total);
    lv_label_set_text(s_counter, buf);

    lv_label_set_text(s_ss_icon, s_slideshow ? LV_SYMBOL_PLAY : "");
}

/* ── Public API ──────────────────────────────────────────────────────────── */
lv_obj_t *photo_viewer_create(lv_obj_t *parent)
{
    /* Root screen — covers full display area */
    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, IPOD_SCREEN_W, IPOD_DISPLAY_H);
    lv_obj_align(s_root, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN); /* hidden by default */

    /* ── Full-screen image widget ── */
    s_img = lv_image_create(s_root);
    lv_obj_set_size(s_img, IPOD_SCREEN_W, IPOD_DISPLAY_H - 28);
    lv_obj_align(s_img, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_image_set_inner_align(s_img, LV_IMAGE_ALIGN_CENTER);
    lv_image_set_scale(s_img, 256);
    lv_obj_set_style_bg_color(s_img, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_img, LV_OPA_COVER, 0);

    /* ── Top info bar ── */
    s_bar = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_bar);
    lv_obj_set_size(s_bar, IPOD_SCREEN_W, 28);
    lv_obj_align(s_bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(s_bar, 200, 0);
    lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_SCROLLABLE);

    /* Photo name */
    s_name_lbl = lv_label_create(s_bar);
    lv_obj_set_style_text_font(s_name_lbl, IPOD_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_name_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_long_mode(s_name_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(s_name_lbl, IPOD_SCREEN_W - 80);
    lv_obj_align(s_name_lbl, LV_ALIGN_LEFT_MID, 8, 0);
    lv_label_set_text(s_name_lbl, "Photo");

    /* Counter */
    s_counter = lv_label_create(s_bar);
    lv_obj_set_style_text_font(s_counter, IPOD_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_counter, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(s_counter, LV_ALIGN_RIGHT_MID, -32, 0);
    lv_label_set_text(s_counter, "0 / 0");

    /* Slideshow icon */
    s_ss_icon = lv_label_create(s_bar);
    lv_obj_set_style_text_font(s_ss_icon, IPOD_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_ss_icon, lv_color_hex(0x4FA5FF), 0);
    lv_obj_align(s_ss_icon, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_label_set_text(s_ss_icon, "");

    ESP_LOGI(TAG, "Photo Viewer created.");
    return s_root;
}

void photo_viewer_open(int index)
{
    s_total = media_get_photo_count();
    if (s_total == 0) {
        ESP_LOGW(TAG, "No photos found on SD card.");
        return;
    }
    s_cur_index = index;
    if (s_cur_index < 0) s_cur_index = 0;
    if (s_cur_index >= s_total) s_cur_index = s_total - 1;

    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    update_display();
    ESP_LOGI(TAG, "Photo viewer opened at index %d", s_cur_index);
}

void photo_viewer_next(void)
{
    s_total = media_get_photo_count();
    if (s_total == 0) return;
    s_cur_index = (s_cur_index + 1) % s_total;
    s_ss_ticks = 0;
    update_display();
}

void photo_viewer_prev(void)
{
    s_total = media_get_photo_count();
    if (s_total == 0) return;
    s_cur_index = (s_cur_index - 1 + s_total) % s_total;
    s_ss_ticks = 0;
    update_display();
}

void photo_viewer_toggle_slideshow(void)
{
    s_slideshow = !s_slideshow;
    s_ss_ticks  = 0;
    lv_label_set_text(s_ss_icon, s_slideshow ? LV_SYMBOL_PLAY : "");
    ESP_LOGI(TAG, "Slideshow %s", s_slideshow ? "ON" : "OFF");
}

void photo_viewer_tick(void)
{
    if (!s_slideshow || !photo_viewer_is_active()) return;
    s_ss_ticks++;
    if (s_ss_ticks >= SS_ADVANCE_TICKS) {
        s_ss_ticks = 0;
        photo_viewer_next();
    }
}

bool photo_viewer_is_active(void)
{
    if (!s_root) return false;
    return !lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}

void photo_viewer_close(void)
{
    if (s_root) lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    s_slideshow = false;
    s_ss_ticks  = 0;
}
