#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include "esp_codec_dev.h"


/* Volume Control (0..100) */
bool audio_player_set_volume(int vol_pct);
int  audio_player_get_volume(void);
bool audio_player_adjust_volume(int delta);

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CMD_PLAY,
    CMD_PAUSE,
    CMD_STOP,
    CMD_SEEK,
    CMD_NEXT,
    CMD_PREV,
    CMD_SELECT_TRACK
} transport_cmd_type_t;

typedef struct {
    transport_cmd_type_t type;
    char file_path[256];
    uint32_t seek_pos_sec;
} transport_cmd_t;

typedef struct {
    uint32_t current_time_sec;
    uint32_t total_duration_sec;
    bool is_playing;
    char title[256];
    char artist[256];
} audio_state_t;

// Event Bits
#define AUDIO_EVENT_PLAYING (1 << 0)
#define AUDIO_EVENT_PAUSED  (1 << 1)
#define AUDIO_EVENT_STOPPED (1 << 2)
#define AUDIO_EVENT_EOF     (1 << 3)

/**
 * @brief Initialize Audio Player queues, event groups, ES8311 speaker device, and start Core 1 decoder task.
 */
esp_err_t audio_player_init(void);
esp_codec_dev_handle_t audio_player_get_codec_handle(void);

/**
 * @brief Send transport command to Core 1 Audio Decoder task.
 */
bool audio_player_send_cmd(const transport_cmd_t *cmd);

/**
 * @brief Fetch current playback state (atomic overwrite queue read).
 */
bool audio_player_get_state(audio_state_t *state);

/**
 * @brief Get handle to transport command queue.
 */
QueueHandle_t audio_player_get_cmd_queue(void);

/**
 * @brief Get handle to audio state queue.
 */
QueueHandle_t audio_player_get_state_queue(void);

/* Convenience Transport Helpers */
bool audio_player_play(const char *path);
bool audio_player_pause(void);
bool audio_player_resume(void);
bool audio_player_toggle_play(void);
bool audio_player_prev(void);
bool audio_player_next(void);
bool audio_player_seek(uint32_t sec);


/* Volume Control (0..100) */
bool audio_player_set_volume(int vol_pct);
int  audio_player_get_volume(void);
bool audio_player_adjust_volume(int delta);

#ifdef __cplusplus
}
#endif
