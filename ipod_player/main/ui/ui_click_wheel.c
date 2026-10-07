/*
 * iPod Classic UI — Click Wheel Component
 * Implements the iconic rotary touch click wheel from iPod-classic-Cm5
 * Polar coordinate touch handling:
 *   - Center circle (R < 50px)      : Center / OK button
 *   - Top quadrant (-135 .. -45 deg) : MENU (Back)
 *   - Bottom quad (45 .. 135 deg)   : PLAY / PAUSE
 *   - Left quad (< -135 || > 135 deg): PREV (|<<)
 *   - Right quad (-45 .. 45 deg)    : NEXT (>>|)
 *   - Rotary drag / swirl (R 40..170): Clockwise (+1) / Counter-Clockwise (-1)
 */
#include "ui_click_wheel.h"
#include "esp_log.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *TAG = "WHEEL";

/* Click wheel geometry (relative to 480x800 screen) */
#define SCREEN_W            480
#define SCREEN_H            800
#define WHEEL_CENTER_X      240.0f
#define WHEEL_CENTER_Y      600.0f   /* 400 + 200 */
#define WHEEL_R_OUTER       140      /* 280px diameter */
#define WHEEL_R_INNER       48       /* 96px diameter */
#define SCROLL_STEP_DEG     16.0f

static wheel_scroll_cb_t s_scroll_cb = NULL;
static wheel_click_cb_t  s_click_cb  = NULL;

/* LVGL UI references */
static lv_obj_t *s_wheel_cont  = NULL;
static lv_obj_t *s_wheel_outer = NULL;
static lv_obj_t *s_center_btn  = NULL;
static lv_obj_t *s_lbl_menu    = NULL;
static lv_obj_t *s_lbl_play    = NULL;
static lv_obj_t *s_lbl_prev    = NULL;
static lv_obj_t *s_lbl_next    = NULL;

/* Gesture state tracking */
static bool  s_is_pressed       = false;
static bool  s_is_swirling      = false;
static float s_press_x          = 0.0f;
static float s_press_y          = 0.0f;
static float s_press_dist       = 0.0f;
static float s_press_angle      = 0.0f;
static float s_last_angle       = 0.0f;
static float s_accum_scroll_deg = 0.0f;
static float s_total_deg_travel = 0.0f;

static void reset_visual_feedback(void)
{
    if (s_center_btn) {
        lv_obj_set_style_bg_color(s_center_btn, lv_color_hex(0xD6D6D8), 0);
    }
    lv_color_t normal_txt = lv_color_hex(0x666666);
    if (s_lbl_menu) lv_obj_set_style_text_color(s_lbl_menu, normal_txt, 0);
    if (s_lbl_play) lv_obj_set_style_text_color(s_lbl_play, normal_txt, 0);
    if (s_lbl_prev) lv_obj_set_style_text_color(s_lbl_prev, normal_txt, 0);
    if (s_lbl_next) lv_obj_set_style_text_color(s_lbl_next, normal_txt, 0);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Unified Polar Touch Handler for the Click Wheel
 * ─────────────────────────────────────────────────────────────────────── */
static void wheel_touch_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_event_get_indev(e);
    if (!indev) indev = lv_indev_active();
    if (!indev) return;

    lv_point_t pt;
    lv_indev_get_point(indev, &pt);

    float dx = (float)pt.x - WHEEL_CENTER_X;
    float dy = (float)pt.y - WHEEL_CENTER_Y;
    float dist = sqrtf(dx * dx + dy * dy);
    float angle_deg = atan2f(dy, dx) * 180.0f / (float)M_PI; /* -180 .. +180 */

    if (code == LV_EVENT_PRESSED) {
        s_is_pressed = true;
        s_is_swirling = false;
        s_press_x = (float)pt.x;
        s_press_y = (float)pt.y;
        s_press_dist = dist;
        s_press_angle = angle_deg;
        s_last_angle = angle_deg;
        s_accum_scroll_deg = 0.0f;
        s_total_deg_travel = 0.0f;

        /* Visual Feedback */
        if (dist < 50.0f) {
            lv_obj_set_style_bg_color(s_center_btn, lv_color_hex(0xA4A4A6), 0);
        } else if (dist <= 170.0f) {
            lv_color_t active_col = lv_color_hex(0x0066FF);
            if (angle_deg >= -135.0f && angle_deg < -45.0f) {
                if (s_lbl_menu) lv_obj_set_style_text_color(s_lbl_menu, active_col, 0);
            } else if (angle_deg >= 45.0f && angle_deg < 135.0f) {
                if (s_lbl_play) lv_obj_set_style_text_color(s_lbl_play, active_col, 0);
            } else if (angle_deg < -135.0f || angle_deg >= 135.0f) {
                if (s_lbl_prev) lv_obj_set_style_text_color(s_lbl_prev, active_col, 0);
            } else {
                if (s_lbl_next) lv_obj_set_style_text_color(s_lbl_next, active_col, 0);
            }
        }
        ESP_LOGI(TAG, "Wheel Touch Down: pt=(%d, %d), R=%.1f, angle=%.1f", (int)pt.x, (int)pt.y, dist, angle_deg);
    }
    else if (code == LV_EVENT_PRESSING) {
        if (!s_is_pressed) return;

        /* Track rotary swirl on outer ring */
        if (dist >= 40.0f && dist <= 170.0f) {
            float diff = angle_deg - s_last_angle;
            while (diff > 180.0f) diff -= 360.0f;
            while (diff < -180.0f) diff += 360.0f;

            s_total_deg_travel += fabsf(diff);
            s_last_angle = angle_deg;

            /* Swirling threshold: 12 degrees travel confirms continuous rotation */
            if (s_total_deg_travel >= 12.0f) {
                if (!s_is_swirling) {
                    s_is_swirling = true;
                    reset_visual_feedback();
                }

                s_accum_scroll_deg += diff;
                while (s_accum_scroll_deg >= SCROLL_STEP_DEG) {
                    ESP_LOGI(TAG, "Rotary: CW / Down (+1)");
                    if (s_scroll_cb) s_scroll_cb(1);
                    s_accum_scroll_deg -= SCROLL_STEP_DEG;
                }
                while (s_accum_scroll_deg <= -SCROLL_STEP_DEG) {
                    ESP_LOGI(TAG, "Rotary: CCW / Up (-1)");
                    if (s_scroll_cb) s_scroll_cb(-1);
                    s_accum_scroll_deg += SCROLL_STEP_DEG;
                }
            }
        }
    }
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        reset_visual_feedback();

        if (s_is_pressed && !s_is_swirling) {
            /* Clean button tap detected */
            if (s_press_dist < 50.0f) {
                ESP_LOGI(TAG, ">>> WHEEL CLICK: CENTER / OK <<<");
                if (s_click_cb) s_click_cb(WHEEL_BTN_CENTER);
            } else if (s_press_dist >= 50.0f && s_press_dist <= 175.0f) {
                if (s_press_angle >= -135.0f && s_press_angle < -45.0f) {
                    ESP_LOGI(TAG, ">>> WHEEL CLICK: MENU (Back) <<<");
                    if (s_click_cb) s_click_cb(WHEEL_BTN_MENU);
                } else if (s_press_angle >= 45.0f && s_press_angle < 135.0f) {
                    ESP_LOGI(TAG, ">>> WHEEL CLICK: PLAY / PAUSE <<<");
                    if (s_click_cb) s_click_cb(WHEEL_BTN_PLAY_PAUSE);
                } else if (s_press_angle < -135.0f || s_press_angle >= 135.0f) {
                    ESP_LOGI(TAG, ">>> WHEEL CLICK: PREV (|<<) <<<");
                    if (s_click_cb) s_click_cb(WHEEL_BTN_PREV);
                } else {
                    ESP_LOGI(TAG, ">>> WHEEL CLICK: NEXT (>>|) <<<");
                    if (s_click_cb) s_click_cb(WHEEL_BTN_NEXT);
                }
            }
        }
        s_is_pressed = false;
        s_is_swirling = false;
    }
}

/* ─────────────────────────────────────────────────────────────────────────
 * Create Click Wheel Container and Visual Elements
 * ─────────────────────────────────────────────────────────────────────── */
lv_obj_t *ui_click_wheel_create(lv_obj_t *parent, wheel_scroll_cb_t scroll_cb, wheel_click_cb_t click_cb)
{
    s_scroll_cb = scroll_cb;
    s_click_cb  = click_cb;

    /* Chassis container: 480x400 bottom half */
    s_wheel_cont = lv_obj_create(parent);
    lv_obj_remove_style_all(s_wheel_cont);
    lv_obj_set_size(s_wheel_cont, SCREEN_W, 400);
    lv_obj_align(s_wheel_cont, LV_ALIGN_TOP_LEFT, 0, 400);
    lv_obj_set_style_bg_color(s_wheel_cont, lv_color_hex(0xEBEBEB), 0);
    lv_obj_set_style_bg_opa(s_wheel_cont, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_wheel_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_wheel_cont, LV_OBJ_FLAG_CLICKABLE);

    /* Separator line between top display and click wheel chassis */
    lv_obj_set_style_border_side(s_wheel_cont, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(s_wheel_cont, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_border_width(s_wheel_cont, 1, 0);

    /* Outer Click Wheel Disc (White circle, 280x280) */
    s_wheel_outer = lv_obj_create(s_wheel_cont);
    lv_obj_remove_style_all(s_wheel_outer);
    lv_obj_set_size(s_wheel_outer, WHEEL_R_OUTER * 2, WHEEL_R_OUTER * 2);
    lv_obj_align(s_wheel_outer, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(s_wheel_outer, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_wheel_outer, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(s_wheel_outer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_wheel_outer, lv_color_hex(0xD0D0D0), 0);
    lv_obj_set_style_border_width(s_wheel_outer, 1, 0);
    lv_obj_set_style_shadow_color(s_wheel_outer, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(s_wheel_outer, 30, 0);
    lv_obj_set_style_shadow_width(s_wheel_outer, 10, 0);
    lv_obj_set_style_shadow_ofs_y(s_wheel_outer, 2, 0);
    lv_obj_clear_flag(s_wheel_outer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_wheel_outer, LV_OBJ_FLAG_CLICKABLE);

    /* Inner Center Select Button (Silver disc, 96x96) */
    s_center_btn = lv_obj_create(s_wheel_outer);
    lv_obj_remove_style_all(s_center_btn);
    lv_obj_set_size(s_center_btn, WHEEL_R_INNER * 2, WHEEL_R_INNER * 2);
    lv_obj_align(s_center_btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(s_center_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_center_btn, lv_color_hex(0xD6D6D8), 0);
    lv_obj_set_style_bg_opa(s_center_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_center_btn, lv_color_hex(0xBFBFBF), 0);
    lv_obj_set_style_border_width(s_center_btn, 1, 0);
    lv_obj_clear_flag(s_center_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_center_btn, LV_OBJ_FLAG_CLICKABLE);

    /* ── Silk-Screened Quadrant Labels ── */
    /* Top: MENU */
    s_lbl_menu = lv_label_create(s_wheel_outer);
    lv_label_set_text(s_lbl_menu, "MENU");
    lv_obj_set_style_text_font(s_lbl_menu, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_lbl_menu, lv_color_hex(0x666666), 0);
    lv_obj_align(s_lbl_menu, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_clear_flag(s_lbl_menu, LV_OBJ_FLAG_CLICKABLE);

    /* Bottom: PLAY / PAUSE */
    s_lbl_play = lv_label_create(s_wheel_outer);
    lv_label_set_text(s_lbl_play, LV_SYMBOL_PLAY " " LV_SYMBOL_PAUSE);
    lv_obj_set_style_text_font(s_lbl_play, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_lbl_play, lv_color_hex(0x666666), 0);
    lv_obj_align(s_lbl_play, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_clear_flag(s_lbl_play, LV_OBJ_FLAG_CLICKABLE);

    /* Left: PREV */
    s_lbl_prev = lv_label_create(s_wheel_outer);
    lv_label_set_text(s_lbl_prev, LV_SYMBOL_PREV);
    lv_obj_set_style_text_font(s_lbl_prev, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_lbl_prev, lv_color_hex(0x666666), 0);
    lv_obj_align(s_lbl_prev, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_clear_flag(s_lbl_prev, LV_OBJ_FLAG_CLICKABLE);

    /* Right: NEXT */
    s_lbl_next = lv_label_create(s_wheel_outer);
    lv_label_set_text(s_lbl_next, LV_SYMBOL_NEXT);
    lv_obj_set_style_text_font(s_lbl_next, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_lbl_next, lv_color_hex(0x666666), 0);
    lv_obj_align(s_lbl_next, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_clear_flag(s_lbl_next, LV_OBJ_FLAG_CLICKABLE);

    /* Register single unified touch callback on chassis container */
    lv_obj_add_event_cb(s_wheel_cont, wheel_touch_event_cb, LV_EVENT_ALL, NULL);

    ESP_LOGI(TAG, "iPod Click Wheel ready with unified polar touch geometry.");
    return s_wheel_cont;
}
