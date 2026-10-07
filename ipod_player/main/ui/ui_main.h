				/*
 * iPod Classic UI — Main Header
 * Faithful LVGL 9 replica of AahanDoesGit/iPod-classic-Cm5
 * Target: ESP32-P4, 480x800 portrait MIPI-DSI display
 */
#pragma once

#include "lvgl.h"
#include "esp_lv_adapter.h"
#include "../audio/audio_player.h"
#include "ui_click_wheel.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── Display Dimensions ─────────────────────────────────────────────────── */
#define IPOD_SCREEN_W       480
#define IPOD_SCREEN_H       800

#define IPOD_DISPLAY_H      400
#define IPOD_WHEEL_H        400
#define IPOD_STATUSBAR_H    28
#define IPOD_CONTENT_H      (IPOD_DISPLAY_H - IPOD_STATUSBAR_H) // 372px

/* Split menu dimensions */
#define IPOD_SPLIT_LEFT_W   240
#define IPOD_SPLIT_RIGHT_W  240
#define IPOD_MENU_ITEM_H    42

/* ─── iPod Classic Colour Palette ───────────────────────────────────────── */
#define IPOD_COL_BG         lv_color_hex(0xFFFFFF)
#define IPOD_COL_PREVIEW_BG lv_color_hex(0xF0F0F0)
#define IPOD_COL_CHASSIS    lv_color_hex(0xEBEBEB)
#define IPOD_COL_STATUSBAR  lv_color_hex(0xF2F2F2)
#define IPOD_COL_SEPARATOR  lv_color_hex(0xAAAAAA)
#define IPOD_COL_DIVIDER    lv_color_hex(0xCCCCCC)
#define IPOD_COL_ITEM_LINE  lv_color_hex(0xE0E0E0)
#define IPOD_COL_TEXT       lv_color_hex(0x000000)
#define IPOD_COL_TEXT_SUB   lv_color_hex(0x888888)
#define IPOD_COL_SEL_TOP    lv_color_hex(0x4FA5FF)
#define IPOD_COL_SEL_BOT    lv_color_hex(0x0066FF)
#define IPOD_COL_ICON       lv_color_hex(0x444444)
#define IPOD_COL_BATT       lv_color_hex(0x555555)
#define IPOD_COL_ART_BG     lv_color_hex(0xC8C8C8)
#define IPOD_COL_SCRUB_BG   lv_color_hex(0xDDDDDD)
#define IPOD_COL_SCRUB_FG   lv_color_hex(0x0066FF)
#define IPOD_COL_CHEVRON    lv_color_hex(0xAAAAAA)

/* ─── Fonts ─────────────────────────────────────────────────────────────── */
#define IPOD_FONT_TITLE     (&lv_font_montserrat_12)
#define IPOD_FONT_ITEM      (&lv_font_montserrat_16)
#define IPOD_FONT_SMALL     (&lv_font_montserrat_12)
#define IPOD_FONT_BIG       (&lv_font_montserrat_48)

#include "ui_volume_screen.h"

/* ─── Screen View Type ──────────────────────────────────────────────────── */
typedef enum {
    VIEW_MENU = 0,
    VIEW_NOW_PLAYING,
    VIEW_PHOTO_VIEWER,
    VIEW_VIDEO_PLAYER,
    VIEW_VOLUME,
} ipod_view_t;

/* ─── Global Playback State ─────────────────────────────────────────────── */
typedef struct {
    char title[256];
    char artist[256];
    char album[256];
    uint32_t duration_sec;
    uint32_t position_sec;
    bool is_playing;
    bool is_paused;
    int  current_index;
    int  song_count;
} ipod_playback_state_t;

extern ipod_playback_state_t g_ipod_state;

/* ─── Public UI System API ───────────────────────────────────────────────── */
void ui_init(void);
void ui_update_loop(void);
void ui_set_view(ipod_view_t view);
ipod_view_t ui_get_current_view(void);
void ui_statusbar_set_title(const char *title);
void ui_statusbar_set_playing(bool playing, bool paused);

/* ─── Sub-views ─────────────────────────────────────────────────────────── */
lv_obj_t *ui_menu_view_create(lv_obj_t *parent);
void ui_menu_scroll(int direction);
void ui_menu_select(void);
void ui_menu_back(void);
void ui_library_play_prev(void);
void ui_library_play_next(void);

lv_obj_t *ui_now_playing_view_create(lv_obj_t *parent);
void ui_now_playing_update(const ipod_playback_state_t *st);
void ui_now_playing_seek_delta(int sec);

/* ─── Photo Viewer ───────────────────────────────────────────────────────── */
lv_obj_t *photo_viewer_create(lv_obj_t *parent);
void photo_viewer_open(int index);
void photo_viewer_next(void);
void photo_viewer_prev(void);
void photo_viewer_toggle_slideshow(void);
void photo_viewer_tick(void);
bool photo_viewer_is_active(void);
void photo_viewer_close(void);

/* ─── Video Player Screen ────────────────────────────────────────────────── */
lv_obj_t *ui_video_screen_create(lv_obj_t *parent);
void ui_video_screen_open(int index);
void ui_video_screen_close(void);
void ui_video_screen_next(void);
void ui_video_screen_prev(void);
void ui_video_screen_tick(void);
bool ui_video_screen_is_active(void);

#ifdef __cplusplus
}
#endif

