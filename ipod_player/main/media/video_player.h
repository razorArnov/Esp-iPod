/*
 * video_player.h — MJPEG/AVI Video Player
 * Plays RIFF/AVI files with MJPEG video + PCM/MP3 audio through ES8311.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VIDEO_STATE_IDLE = 0,
    VIDEO_STATE_PLAYING,
    VIDEO_STATE_PAUSED,
    VIDEO_STATE_FINISHED,
    VIDEO_STATE_ERROR,
} video_state_e;

typedef struct {
    video_state_e state;
    uint32_t      current_frame;
    uint32_t      total_frames;
    uint32_t      position_sec;
    uint32_t      duration_sec;
    uint32_t      width;
    uint32_t      height;
    uint32_t      fps;
    char          title[128];
} video_status_t;

/**
 * Initialize video player subsystem.
 * Must be called once before any other video API.
 */
esp_err_t video_player_init(void);

/**
 * Start playing an MJPEG/AVI file.
 * @param path     Full path to the .avi or .mjpeg file on /sdcard
 * @param canvas   LVGL canvas object to render frames into (sized to video resolution)
 */
esp_err_t video_player_play(const char *path, lv_obj_t *canvas);

/** Pause / resume toggle */
void video_player_toggle_pause(void);

/** Stop playback and close file */
void video_player_stop(void);

/** Get current status (non-blocking, safe from any task) */
bool video_player_get_status(video_status_t *out);

/** Check if currently active (playing or paused) */
bool video_player_is_active(void);

#ifdef __cplusplus
}
#endif
