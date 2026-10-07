/*
 * iPod Classic UI — Main Orchestrator
 * Faithful LVGL 9 replica of AahanDoesGit/iPod-classic-Cm5
 * Top 400px: Display Area (Status bar + Split Menu / Now Playing)
 * Bottom 400px: Click Wheel Area
 */
#include "ui_main.h"
#include "media_scanner.h"
#include "video_player.h"
#include "photo_viewer.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "IPOD_MAIN";

/* Global iPod playback state */
ipod_playback_state_t g_ipod_state = {
    .title = "No Track Selected",
    .artist = "iPod Music Library",
    .album = "Classic",
    .duration_sec = 240,
    .position_sec = 0,
    .is_playing = false,
    .is_paused = false,
    .current_index = 0,
    .song_count = 0
};

/* Top Display UI objects */
static lv_obj_t   *s_root_scr       = NULL;
static lv_obj_t   *s_display_cont   = NULL;
static lv_obj_t   *s_wheel_cont     = NULL;

/* Status bar */
static lv_obj_t   *s_sb_bar         = NULL;
static lv_obj_t   *s_sb_title       = NULL;
static lv_obj_t   *s_sb_play_icon   = NULL;
static lv_obj_t   *s_sb_battery     = NULL;

/* Sub-views */
static lv_obj_t   *s_menu_view      = NULL;
static lv_obj_t   *s_np_view        = NULL;
static lv_obj_t   *s_photo_view     = NULL;
static lv_obj_t   *s_video_view     = NULL;
static lv_obj_t   *s_volume_view    = NULL;
static ipod_view_t s_current_view   = VIEW_MENU;

/* ─────────────────────────────────────────────────────────────────────────
 * Status Bar (28px height across top of display)
 * ─────────────────────────────────────────────────────────────────────── */
static void statusbar_create(lv_obj_t *parent)
{
    s_sb_bar = lv_obj_create(parent);
    lv_obj_remove_style_all(s_sb_bar);
    lv_obj_set_size(s_sb_bar, IPOD_SCREEN_W, IPOD_STATUSBAR_H);
    lv_obj_align(s_sb_bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_sb_bar, IPOD_COL_STATUSBAR, 0);
    lv_obj_set_style_bg_opa(s_sb_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(s_sb_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(s_sb_bar, IPOD_COL_SEPARATOR, 0);
    lv_obj_set_style_border_width(s_sb_bar, 1, 0);
    lv_obj_clear_flag(s_sb_bar, LV_OBJ_FLAG_SCROLLABLE);

    /* Play/Pause state icon (left) */
    s_sb_play_icon = lv_label_create(s_sb_bar);
    lv_obj_set_style_text_color(s_sb_play_icon, IPOD_COL_ICON, 0);
    lv_obj_set_style_text_font(s_sb_play_icon, IPOD_FONT_SMALL, 0);
    lv_label_set_text(s_sb_play_icon, "");
    lv_obj_align(s_sb_play_icon, LV_ALIGN_LEFT_MID, 10, 0);

    /* Screen Title (centered) */
    s_sb_title = lv_label_create(s_sb_bar);
    lv_obj_set_style_text_color(s_sb_title, IPOD_COL_TEXT, 0);
    lv_obj_set_style_text_font(s_sb_title, IPOD_FONT_TITLE, 0);
    lv_label_set_text(s_sb_title, "iPod");
    lv_obj_align(s_sb_title, LV_ALIGN_CENTER, 0, 0);

    /* Battery level icon (right) */
    s_sb_battery = lv_obj_create(s_sb_bar);
    lv_obj_remove_style_all(s_sb_battery);
    lv_obj_set_size(s_sb_battery, 22, 11);
    lv_obj_align(s_sb_battery, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(s_sb_battery, IPOD_COL_BATT, 0);
    lv_obj_set_style_bg_opa(s_sb_battery, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_sb_battery, 2, 0);
}

void ui_statusbar_set_title(const char *title)
{
    if (s_sb_title && title) {
        lv_label_set_text(s_sb_title, title);
    }
}

void ui_statusbar_set_playing(bool playing, bool paused)
{
    if (!s_sb_play_icon) return;
    if (playing && !paused) {
        lv_label_set_text(s_sb_play_icon, LV_SYMBOL_PLAY);
    } else if (playing && paused) {
        lv_label_set_text(s_sb_play_icon, LV_SYMBOL_PAUSE);
    } else {
        lv_label_set_text(s_sb_play_icon, "");
    }
}

/* ─────────────────────────────────────────────────────────────────────────
 * View Management (Top 400px Display Area)
 * ─────────────────────────────────────────────────────────────────────── */
void ui_set_view(ipod_view_t view)
{
    s_current_view = view;

    /* Hide all views first */
    if (s_np_view)     lv_obj_add_flag(s_np_view,     LV_OBJ_FLAG_HIDDEN);
    if (s_menu_view)   lv_obj_add_flag(s_menu_view,   LV_OBJ_FLAG_HIDDEN);
    if (s_photo_view)  lv_obj_add_flag(s_photo_view,  LV_OBJ_FLAG_HIDDEN);
    if (s_video_view)  lv_obj_add_flag(s_video_view,  LV_OBJ_FLAG_HIDDEN);
    if (s_volume_view) lv_obj_add_flag(s_volume_view, LV_OBJ_FLAG_HIDDEN);

    if (view == VIEW_MENU) {
        if (s_menu_view) lv_obj_remove_flag(s_menu_view, LV_OBJ_FLAG_HIDDEN);
        ui_statusbar_set_title("iPod");
    } else if (view == VIEW_NOW_PLAYING) {
        if (s_np_view) {
            lv_obj_remove_flag(s_np_view, LV_OBJ_FLAG_HIDDEN);
            ui_now_playing_update(&g_ipod_state);
        }
        ui_statusbar_set_title("Now Playing");
    } else if (view == VIEW_PHOTO_VIEWER) {
        if (s_photo_view) lv_obj_remove_flag(s_photo_view, LV_OBJ_FLAG_HIDDEN);
        ui_statusbar_set_title("Photos");
    } else if (view == VIEW_VIDEO_PLAYER) {
        if (s_video_view) lv_obj_remove_flag(s_video_view, LV_OBJ_FLAG_HIDDEN);
        ui_statusbar_set_title("Videos");
    } else if (view == VIEW_VOLUME) {
        if (s_volume_view) {
            lv_obj_remove_flag(s_volume_view, LV_OBJ_FLAG_HIDDEN);
            ui_volume_screen_update();
        }
        ui_statusbar_set_title("Volume");
    }
}

ipod_view_t ui_get_current_view(void)
{
    return s_current_view;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Click Wheel Event Handlers
 * ─────────────────────────────────────────────────────────────────────── */
static void on_wheel_scroll(int direction)
{
    if (s_current_view == VIEW_MENU) {
        ui_menu_scroll(direction);
    } else if (s_current_view == VIEW_NOW_PLAYING) {
        ui_now_playing_seek_delta(direction * 5);
    } else if (s_current_view == VIEW_PHOTO_VIEWER) {
        if (direction > 0) photo_viewer_next();
        else photo_viewer_prev();
    } else if (s_current_view == VIEW_VIDEO_PLAYER) {
        if (direction > 0) ui_video_screen_next();
        else ui_video_screen_prev();
    } else if (s_current_view == VIEW_VOLUME) {
        /* Right/CW scroll (+1) increases volume, Left/CCW scroll (-1) decreases volume */
        ui_volume_screen_adjust(direction * 4);
    }
}

static void on_wheel_click(wheel_button_t btn)
{
    ESP_LOGI(TAG, "on_wheel_click: btn=%d (1=Menu, 2=Center, 3=Play, 4=Prev, 5=Next) in view=%d", (int)btn, s_current_view);

    switch (btn) {
        case WHEEL_BTN_MENU:
            if (s_current_view == VIEW_NOW_PLAYING) {
                ui_set_view(VIEW_MENU);
            } else if (s_current_view == VIEW_PHOTO_VIEWER) {
                photo_viewer_close();
                ui_set_view(VIEW_MENU);
            } else if (s_current_view == VIEW_VIDEO_PLAYER) {
                ui_video_screen_close();
            } else if (s_current_view == VIEW_VOLUME) {
                ui_set_view(VIEW_MENU);
            } else {
                ui_menu_back();
            }
            break;

        case WHEEL_BTN_CENTER:
            if (s_current_view == VIEW_MENU) {
                ui_menu_select();
            } else if (s_current_view == VIEW_NOW_PLAYING) {
                ui_set_view(VIEW_MENU);
            } else if (s_current_view == VIEW_PHOTO_VIEWER) {
                photo_viewer_next();
            } else if (s_current_view == VIEW_VIDEO_PLAYER) {
                video_player_toggle_pause();
            } else if (s_current_view == VIEW_VOLUME) {
                ui_set_view(VIEW_MENU);
            }
            break;

        case WHEEL_BTN_PLAY_PAUSE:
            if (s_current_view == VIEW_PHOTO_VIEWER) {
                photo_viewer_toggle_slideshow();
            } else if (s_current_view == VIEW_VIDEO_PLAYER) {
                video_player_toggle_pause();
            } else {
                audio_player_toggle_play();
            }
            break;

        case WHEEL_BTN_PREV:
            if (s_current_view == VIEW_PHOTO_VIEWER) {
                photo_viewer_prev();
            } else if (s_current_view == VIEW_VIDEO_PLAYER) {
                ui_video_screen_prev();
            } else if (s_current_view == VIEW_VOLUME) {
                ui_volume_screen_adjust(-5);
            } else {
                ui_library_play_prev();
            }
            break;

        case WHEEL_BTN_NEXT:
            if (s_current_view == VIEW_PHOTO_VIEWER) {
                photo_viewer_next();
            } else if (s_current_view == VIEW_VIDEO_PLAYER) {
                ui_video_screen_next();
            } else if (s_current_view == VIEW_VOLUME) {
                ui_volume_screen_adjust(+5);
            } else {
                ui_library_play_next();
            }
            break;

        default:
            break;
    }
}

/* ─────────────────────────────────────────────────────────────────────────
 * UI Initialization (Called from main task)
 * ─────────────────────────────────────────────────────────────────────── */
void ui_init(void)
{
    ESP_LOGI(TAG, "Initializing Full iPod Classic Replica UI (Display 480x400 + Click Wheel 480x400)...");

    esp_lv_adapter_lock(-1);

    s_root_scr = lv_scr_act();
    lv_obj_remove_style_all(s_root_scr);
    lv_obj_set_size(s_root_scr, IPOD_SCREEN_W, IPOD_SCREEN_H);
    lv_obj_set_style_bg_color(s_root_scr, IPOD_COL_CHASSIS, 0);
    lv_obj_set_style_bg_opa(s_root_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_root_scr, LV_OBJ_FLAG_SCROLLABLE);

    /* 1. Top Display Screen Container (0..400px) */
    s_display_cont = lv_obj_create(s_root_scr);
    lv_obj_remove_style_all(s_display_cont);
    lv_obj_set_size(s_display_cont, IPOD_SCREEN_W, IPOD_DISPLAY_H);
    lv_obj_align(s_display_cont, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_display_cont, IPOD_COL_BG, 0);
    lv_obj_set_style_bg_opa(s_display_cont, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_display_cont, LV_OBJ_FLAG_SCROLLABLE);

    /* Top Status Bar (28px) */
    statusbar_create(s_display_cont);

    /* Build Sub-views inside the display container */
    s_menu_view   = ui_menu_view_create(s_display_cont);
    s_np_view     = ui_now_playing_view_create(s_display_cont);
    s_photo_view  = photo_viewer_create(s_display_cont);
    s_video_view  = ui_video_screen_create(s_display_cont);
    s_volume_view = ui_volume_screen_create(s_display_cont);
    lv_obj_add_flag(s_np_view,     LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_photo_view,  LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_video_view,  LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_volume_view, LV_OBJ_FLAG_HIDDEN);

    /* 2. Bottom Click Wheel (400..800px) */
    s_wheel_cont = ui_click_wheel_create(s_root_scr, on_wheel_scroll, on_wheel_click);

    esp_lv_adapter_unlock();

    ESP_LOGI(TAG, "iPod Classic Replica UI ready.");
}

/* ─────────────────────────────────────────────────────────────────────────
 * UI Update Loop (Called periodically from app_main)
 * ─────────────────────────────────────────────────────────────────────── */
void ui_update_loop(void)
{
    audio_state_t state;
    if (audio_player_get_state(&state)) {
        esp_lv_adapter_lock(-1);

        g_ipod_state.is_playing   = state.is_playing;
        g_ipod_state.is_paused    = !state.is_playing;
        g_ipod_state.position_sec = state.current_time_sec;
        if (state.total_duration_sec > 0) {
            g_ipod_state.duration_sec = state.total_duration_sec;
        }
        if (state.title[0]) {
            snprintf(g_ipod_state.title, sizeof(g_ipod_state.title), "%.255s", state.title);
        }
        if (state.artist[0]) {
            snprintf(g_ipod_state.artist, sizeof(g_ipod_state.artist), "%.255s", state.artist);
        }

        ui_statusbar_set_playing(state.is_playing, !state.is_playing);

        if (s_current_view == VIEW_NOW_PLAYING) {
            ui_now_playing_update(&g_ipod_state);
        }

        esp_lv_adapter_unlock();
    }

    /* ── Photo slideshow timer ── */
    esp_lv_adapter_lock(-1);
    photo_viewer_tick();
    ui_video_screen_tick();
    esp_lv_adapter_unlock();
}
