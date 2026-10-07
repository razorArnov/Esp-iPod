#include "audio_player.h"
#include "esp_log.h"
#include "bsp_init.h"
#include "bsp/esp-bsp.h"
#include <string.h>
#include <sys/stat.h>
#include <math.h>

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

static const char *TAG = "AUDIO_PLAYER";

static QueueHandle_t s_cmd_queue = NULL;
static QueueHandle_t s_state_queue = NULL;
static EventGroupHandle_t s_audio_events = NULL;
static esp_codec_dev_handle_t s_speaker_codec = NULL;

#define MP3_IN_BUF_SZ   (8192)
#define MP3_OUT_SAMPLES (MINIMP3_MAX_SAMPLES_PER_FRAME) /* 2304 */

static void audio_decode_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Audio Decode Task started on Core %d (Priority %d)", xPortGetCoreID(), uxTaskPriorityGet(NULL));

    transport_cmd_t cmd;
    audio_state_t state = {
        .current_time_sec = 0,
        .total_duration_sec = 240,
        .is_playing = false,
        .title = "No Track Selected",
        .artist = "Unknown Artist"
    };

    FILE *fp = NULL;
    long file_size = 0;
    uint32_t current_hz = 44100;
    uint64_t total_samples = 0;

    mp3dec_t mp3d;
    mp3dec_init(&mp3d);

    /* Allocate streaming and PCM buffers in internal DMA RAM */
    uint8_t *in_buf = heap_caps_malloc(MP3_IN_BUF_SZ, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!in_buf) in_buf = malloc(MP3_IN_BUF_SZ);

    int16_t *pcm_out = heap_caps_malloc(MP3_OUT_SAMPLES * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!pcm_out) pcm_out = malloc(MP3_OUT_SAMPLES * sizeof(int16_t));

    int16_t *stereo_out = heap_caps_malloc(MP3_OUT_SAMPLES * 2 * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!stereo_out) stereo_out = malloc(MP3_OUT_SAMPLES * 2 * sizeof(int16_t));

    if (!in_buf || !pcm_out || !stereo_out) {
        ESP_LOGE(TAG, "Fatal: Unable to allocate audio DMA buffers!");
        vTaskDelete(NULL);
        return;
    }

    size_t in_bytes = 0;
    size_t in_pos = 0;

    /* Initialize ES8311 I2S Codec */
    esp_codec_dev_sample_info_t sample_info = {
        .bits_per_sample = 16,
        .channel = 2,
        .channel_mask = 0x03,
        .sample_rate = 44100,
        .mclk_multiple = 256,
    };

    s_speaker_codec = bsp_audio_codec_speaker_init();
    if (s_speaker_codec != NULL) {
        if (esp_codec_dev_open(s_speaker_codec, &sample_info) == ESP_CODEC_DEV_OK) {
            esp_codec_dev_set_out_vol(s_speaker_codec, 90);
            ESP_LOGI(TAG, "ES8311 Speaker codec opened successfully (44.1kHz 16-bit Stereo, Vol 90%%).");
        } else {
            ESP_LOGE(TAG, "Failed to open ES8311 speaker codec!");
        }
    }

    int sim_sub_sec = 0;

    while (1) {
        /* Check for transport commands */
        TickType_t wait_ticks = (state.is_playing) ? 0 : pdMS_TO_TICKS(100);
        if (xQueueReceive(s_cmd_queue, &cmd, wait_ticks) == pdTRUE) {
            switch (cmd.type) {
                case CMD_SELECT_TRACK:
                case CMD_PLAY:
                    if (strlen(cmd.file_path) > 0) {
                        if (fp) { fclose(fp); fp = NULL; }
                        fp = fopen(cmd.file_path, "rb");
                        if (fp) {
                            state.is_playing = true;
                            state.current_time_sec = 0;
                            total_samples = 0;
                            in_bytes = 0;
                            in_pos = 0;
                            mp3dec_init(&mp3d);

                            struct stat st;
                            if (stat(cmd.file_path, &st) == 0) {
                                file_size = st.st_size;
                                state.total_duration_sec = (uint32_t)(file_size / 16000); /* Initial approx */
                                if (state.total_duration_sec < 10) state.total_duration_sec = 210;
                            }

                            /* Skip ID3v2 metadata header directly to audio stream */
                            uint8_t id3_hdr[10];
                            if (fread(id3_hdr, 1, 10, fp) == 10 && id3_hdr[0] == 'I' && id3_hdr[1] == 'D' && id3_hdr[2] == '3') {
                                uint32_t tag_sz = ((uint32_t)(id3_hdr[6] & 0x7F) << 21) |
                                                  ((uint32_t)(id3_hdr[7] & 0x7F) << 14) |
                                                  ((uint32_t)(id3_hdr[8] & 0x7F) << 7)  |
                                                  ((uint32_t)(id3_hdr[9] & 0x7F));
                                uint32_t audio_offset = 10 + tag_sz;
                                fseek(fp, audio_offset, SEEK_SET);
                                ESP_LOGI(TAG, "Skipped ID3v2 header (%lu bytes), streaming audio directly from byte %lu",
                                         (unsigned long)tag_sz, (unsigned long)audio_offset);
                            } else {
                                fseek(fp, 0, SEEK_SET);
                            }

                            /* Read first chunk of MP3 audio data */
                            if (in_buf) {
                                in_bytes = fread(in_buf, 1, MP3_IN_BUF_SZ, fp);
                            }

                            const char *filename = strrchr(cmd.file_path, '/');
                            snprintf(state.title, sizeof(state.title), "%.255s", filename ? filename + 1 : cmd.file_path);
                            char *dot = strrchr(state.title, '.');
                            if (dot) *dot = '\0';
                            snprintf(state.artist, sizeof(state.artist), "SD Card");

                            xEventGroupSetBits(s_audio_events, AUDIO_EVENT_PLAYING);
                            ESP_LOGI(TAG, "Playing Real MP3: \"%s\" (file size: %ld bytes)", state.title, file_size);
                        } else {
                            ESP_LOGW(TAG, "Cannot open %s, falling back to simulated playback", cmd.file_path);
                            state.is_playing = true;
                            state.current_time_sec = 0;
                            state.total_duration_sec = 210;
                        }
                    } else {
                        /* Demo track without file */
                        if (fp) { fclose(fp); fp = NULL; }
                        state.is_playing = true;
                        state.current_time_sec = 0;
                        if (cmd.seek_pos_sec > 0) state.total_duration_sec = cmd.seek_pos_sec;
                        ESP_LOGI(TAG, "Playing Demo Track (Simulated)");
                    }
                    xQueueOverwrite(s_state_queue, &state);
                    break;

                case CMD_PAUSE:
                    state.is_playing = !state.is_playing;
                    ESP_LOGI(TAG, "Playback state: %s", state.is_playing ? "PLAYING" : "PAUSED");
                    xQueueOverwrite(s_state_queue, &state);
                    break;

                case CMD_STOP:
                    state.is_playing = false;
                    if (fp) { fclose(fp); fp = NULL; }
                    state.current_time_sec = 0;
                    total_samples = 0;
                    in_bytes = 0;
                    in_pos = 0;
                    ESP_LOGI(TAG, "Playback stopped.");
                    xQueueOverwrite(s_state_queue, &state);
                    break;

                case CMD_SEEK:
                    if (fp && file_size > 0 && state.total_duration_sec > 0) {
                        long target_byte = (long)((uint64_t)cmd.seek_pos_sec * file_size / state.total_duration_sec);
                        if (target_byte >= file_size) target_byte = file_size - 1024;
                        if (target_byte < 0) target_byte = 0;

                        fseek(fp, target_byte, SEEK_SET);
                        mp3dec_init(&mp3d);
                        in_pos = 0;
                        in_bytes = fread(in_buf, 1, MP3_IN_BUF_SZ, fp);
                        total_samples = (uint64_t)cmd.seek_pos_sec * current_hz;
                        state.current_time_sec = cmd.seek_pos_sec;
                        ESP_LOGI(TAG, "Seeked to %lu sec (byte %ld)", (unsigned long)cmd.seek_pos_sec, target_byte);
                    } else {
                        state.current_time_sec = cmd.seek_pos_sec;
                    }
                    xQueueOverwrite(s_state_queue, &state);
                    break;

                default:
                    break;
            }
        }

        /* Active Audio Decoding Loop */
        if (state.is_playing) {
            if (fp && in_buf && pcm_out) {
                /* Refill input buffer if running low */
                if ((in_bytes - in_pos) < 2048 && !feof(fp)) {
                    size_t leftover = in_bytes - in_pos;
                    if (leftover > 0) {
                        memmove(in_buf, in_buf + in_pos, leftover);
                    }
                    in_pos = 0;
                    size_t rd = fread(in_buf + leftover, 1, MP3_IN_BUF_SZ - leftover, fp);
                    in_bytes = leftover + rd;
                }

                if (in_bytes > in_pos) {
                    mp3dec_frame_info_t info;
                    int samples = mp3dec_decode_frame(&mp3d, in_buf + in_pos, (int)(in_bytes - in_pos), pcm_out, &info);
                    in_pos += info.frame_bytes;

                    if (info.frame_bytes == 0) {
                        /* Search forward for next sync word */
                        in_pos += 1;
                        taskYIELD();
                    }

                    if (samples > 0) {
                        if (info.hz >= 8000 && (uint32_t)info.hz != current_hz) {
                            current_hz = (uint32_t)info.hz;
                            ESP_LOGI(TAG, "MP3 Frame Info: %lu Hz, %d ch, %d kbps",
                                     (unsigned long)current_hz, info.channels, info.bitrate_kbps);
                            if (info.bitrate_kbps > 0 && file_size > 0) {
                                state.total_duration_sec = (uint32_t)((file_size * 8) / (info.bitrate_kbps * 1000));
                            }
                        }

                        /* Stream PCM audio to ES8311 Codec */
                        if (s_speaker_codec) {
                            if (info.channels == 1 && stereo_out) {
                                for (int i = 0; i < samples; i++) {
                                    stereo_out[i * 2]     = pcm_out[i];
                                    stereo_out[i * 2 + 1] = pcm_out[i];
                                }
                                esp_codec_dev_write(s_speaker_codec, stereo_out, samples * 2 * sizeof(int16_t));
                            } else {
                                esp_codec_dev_write(s_speaker_codec, pcm_out, samples * info.channels * sizeof(int16_t));
                            }
                        }

                        total_samples += samples;
                        if (current_hz > 0) {
                            uint32_t sec = (uint32_t)(total_samples / current_hz);
                            if (sec != state.current_time_sec) {
                                state.current_time_sec = sec;
                                xQueueOverwrite(s_state_queue, &state);
                            }
                        }
                    }
                } else if (feof(fp)) {
                    ESP_LOGI(TAG, "Track playback finished.");
                    state.is_playing = false;
                    fclose(fp);
                    fp = NULL;
                    xQueueOverwrite(s_state_queue, &state);
                }
            } else {
                /* Simulated Track: 1-second ticks */
                sim_sub_sec++;
                if (sim_sub_sec >= 10) {
                    sim_sub_sec = 0;
                    state.current_time_sec++;
                    if (state.total_duration_sec > 0 && state.current_time_sec >= state.total_duration_sec) {
                        state.current_time_sec = 0;
                    }
                    xQueueOverwrite(s_state_queue, &state);
                }
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
    }

    if (in_buf) free(in_buf);
    if (pcm_out) free(pcm_out);
    if (stereo_out) free(stereo_out);
    vTaskDelete(NULL);
}

esp_err_t audio_player_init(void)
{
    s_cmd_queue = xQueueCreate(10, sizeof(transport_cmd_t));
    if (!s_cmd_queue) {
        ESP_LOGE(TAG, "Failed to create command queue!");
        return ESP_ERR_NO_MEM;
    }

    s_state_queue = xQueueCreate(1, sizeof(audio_state_t));
    if (!s_state_queue) {
        ESP_LOGE(TAG, "Failed to create state queue!");
        return ESP_ERR_NO_MEM;
    }

    s_audio_events = xEventGroupCreate();
    if (!s_audio_events) {
        ESP_LOGE(TAG, "Failed to create event group!");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ret = xTaskCreatePinnedToCore(
        audio_decode_task,
        "audio_decode",
        32768, /* 32KB stack for minimp3 decoder + FATFS */
        NULL,
        10,
        NULL,
        1
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to spawn audio_decode_task on Core 1!");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Audio Player initialized with minimp3 decoder engine.");
    return ESP_OK;
}

bool audio_player_send_cmd(const transport_cmd_t *cmd)
{
    if (!s_cmd_queue || !cmd) return false;
    return (xQueueSend(s_cmd_queue, cmd, pdMS_TO_TICKS(50)) == pdTRUE);
}

bool audio_player_get_state(audio_state_t *state)
{
    if (!s_state_queue || !state) return false;
    return (xQueuePeek(s_state_queue, state, 0) == pdTRUE);
}

QueueHandle_t audio_player_get_cmd_queue(void)
{
    return s_cmd_queue;
}

QueueHandle_t audio_player_get_state_queue(void)
{
    return s_state_queue;
}

bool audio_player_play(const char *path)
{
    transport_cmd_t cmd = {0};
    cmd.type = CMD_PLAY;
    if (path) {
        strncpy(cmd.file_path, path, sizeof(cmd.file_path) - 1);
    }
    return audio_player_send_cmd(&cmd);
}

bool audio_player_pause(void)
{
    audio_state_t state;
    if (audio_player_get_state(&state) && !state.is_playing) {
        return true;
    }
    transport_cmd_t cmd = { .type = CMD_PAUSE };
    return audio_player_send_cmd(&cmd);
}

bool audio_player_resume(void)
{
    transport_cmd_t cmd = {0};
    cmd.type = CMD_PLAY;
    return audio_player_send_cmd(&cmd);
}

bool audio_player_toggle_play(void)
{
    transport_cmd_t cmd = { .type = CMD_PAUSE };
    return audio_player_send_cmd(&cmd);
}

bool audio_player_prev(void)
{
    transport_cmd_t cmd = { .type = CMD_PREV };
    return audio_player_send_cmd(&cmd);
}

bool audio_player_next(void)
{
    transport_cmd_t cmd = { .type = CMD_NEXT };
    return audio_player_send_cmd(&cmd);
}

bool audio_player_seek(uint32_t sec)
{
    transport_cmd_t cmd = { .type = CMD_SEEK, .seek_pos_sec = sec };
    return audio_player_send_cmd(&cmd);
}

esp_codec_dev_handle_t audio_player_get_codec_handle(void) {
    return s_speaker_codec;
}

static int s_current_volume = 90;

bool audio_player_set_volume(int vol_pct)
{
    if (vol_pct < 0) vol_pct = 0;
    if (vol_pct > 100) vol_pct = 100;
    s_current_volume = vol_pct;
    if (s_speaker_codec) {
        esp_codec_dev_set_out_vol(s_speaker_codec, s_current_volume);
    }
    return true;
}

int audio_player_get_volume(void)
{
    return s_current_volume;
}

bool audio_player_adjust_volume(int delta)
{
    return audio_player_set_volume(s_current_volume + delta);
}
