/*
 * video_player.c — High-performance Hardware-Accelerated MJPEG/AVI Player
 *                  with synchronized ES8311 I2S audio
 *
 * Supported format: RIFF/AVI with:
 *   - Video stream: MJPEG (stream tag "00dc"), raw compressed JPEG frames
 *     Hardware-decoded via ESP32-P4 on-chip JPEG engine (2D-DMA, ~2.5ms/frame)
 *     with software TJPGD fallback.
 *   - Audio stream: PCM16LE (stream tag "01wb") at 44100 Hz stereo
 *     Streamed via dedicated Priority 11 FreeRTOS task backed by a 256KB ring buffer.
 *   - Rock-solid real-time A/V synchronization governed by hardware microsecond timer.
 */
#include "video_player.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"
#include "esp_codec_dev.h"
#include "audio_player.h"
#include "lvgl.h"
#include "esp_lv_adapter.h"
#include "esp_heap_caps.h"
#include "src/libs/tjpgd/tjpgd.h"
#include "driver/jpeg_decode.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "VIDEO_PLAYER";

/* ── RIFF/AVI Constants ─────────────────────────────────────────────────── */
#define FOURCC(a,b,c,d) ((uint32_t)((d)<<24|(c)<<16|(b)<<8|(a)))
#define RIFF_RIFF  FOURCC('R','I','F','F')
#define RIFF_AVI_  FOURCC('A','V','I',' ')
#define RIFF_LIST  FOURCC('L','I','S','T')
#define RIFF_hdrl  FOURCC('h','d','r','l')
#define RIFF_avih  FOURCC('a','v','i','h')
#define RIFF_strl  FOURCC('s','t','r','l')
#define RIFF_strh  FOURCC('s','t','r','h')
#define RIFF_strf  FOURCC('s','t','r','f')
#define RIFF_movi  FOURCC('m','o','v','i')
#define RIFF_00dc  FOURCC('0','0','d','c')
#define RIFF_01wb  FOURCC('0','1','w','b')
#define RIFF_idx1  FOURCC('i','d','x','1')
#define RIFF_JUNK  FOURCC('J','U','N','K')
#define RIFF_vids  FOURCC('v','i','d','s')
#define RIFF_auds  FOURCC('a','u','d','s')
#define RIFF_mjpg  FOURCC('m','j','p','g')
#define RIFF_MJPG  FOURCC('M','J','P','G')

#define MAX_FRAME_SZ       (256 * 1024)   /* 256 KB per JPEG frame max */
#define AUDIO_CHUNK_SZ     (8192)         /* 8 KB audio read buffer in internal DMA RAM */
#define IO_BUF_SZ          (32768)        /* 32 KB FATFS buffer in internal DMA RAM */
#define AUDIO_RINGBUF_SZ   (262144)       /* 256 KB audio ring buffer in PSRAM (~1.45s) */
#define PREBUFFER_AUDIO_SZ (16384)        /* Start audio when 16KB (~93ms) buffered */

/* ── Display Dimensions & Double Buffers ────────────────────────────────── */
#define MAX_VID_W  480
#define MAX_VID_H  400

static uint16_t             *s_pix_buf[2]         = {NULL, NULL}; /* Double buffers in PSRAM */
static size_t                s_pix_buf_sz[2]      = {0, 0};
static volatile int          s_write_buf_idx      = 0;
static char                 *s_io_buf             = NULL;         /* 32KB internal DMA buffer */
static lv_image_dsc_t        s_frame_dsc[2];                      /* Alternating image dsc   */

/* ── Hardware JPEG Decoder Handle ───────────────────────────────────────── */
static jpeg_decoder_handle_t s_jpeg_decoder       = NULL;

/* ── Audio Streaming Engine ─────────────────────────────────────────────── */
static RingbufHandle_t       s_audio_ringbuf      = NULL;
static TaskHandle_t          s_audio_task         = NULL;
static volatile bool         s_audio_playing      = false;
static volatile uint64_t     s_audio_bytes_played = 0;
static volatile uint32_t     s_audio_buffered_bytes = 0;

/* ── TJPGD Software Decoder Fallback ────────────────────────────────────── */
typedef struct {
    const uint8_t *data;
    uint32_t       size;
    uint32_t       pos;
} jd_mem_src_t;

static size_t jd_mem_input(JDEC *jd, uint8_t *buf, size_t len)
{
    jd_mem_src_t *src = (jd_mem_src_t *)jd->device;
    uint32_t remaining = src->size - src->pos;
    if (len > remaining) len = remaining;
    if (buf && len > 0) memcpy(buf, src->data + src->pos, len);
    src->pos += (uint32_t)len;
    return len;
}

static uint32_t s_tjpgd_frame_w = 0;
static uint32_t s_tjpgd_frame_h = 0;

static int jd_rgb_output(JDEC *jd, void *bitmap, JRECT *rect)
{
    (void)jd;
    uint16_t *dst = s_pix_buf[s_write_buf_idx];
    if (!dst || !bitmap) return 0;

    const uint8_t *src = (const uint8_t *)bitmap;
    uint16_t blk_w = rect->right  - rect->left + 1;
    uint16_t blk_h = rect->bottom - rect->top  + 1;

    for (uint16_t y = 0; y < blk_h; y++) {
        uint16_t dy = rect->top + y;
        if (dy >= s_tjpgd_frame_h || dy >= MAX_VID_H) break;

        uint16_t max_x = (rect->left + blk_w <= s_tjpgd_frame_w) ? blk_w : (s_tjpgd_frame_w - rect->left);
        if (rect->left + max_x > MAX_VID_W) {
            max_x = (MAX_VID_W > rect->left) ? (MAX_VID_W - rect->left) : 0;
        }

        uint16_t *d = &dst[dy * s_tjpgd_frame_w + rect->left];
        for (uint16_t x = 0; x < max_x; x++) {
            uint8_t r = *src++;
            uint8_t g = *src++;
            uint8_t b = *src++;
            *d++ = (uint16_t)(((uint16_t)(r & 0xF8) << 8) |
                             ((uint16_t)(g & 0xFC) << 3) |
                             (b >> 3));
        }
        src += (blk_w - max_x) * 3;
    }
    return 1;
}

static uint8_t s_tjpgd_work[16384];

static bool decode_jpeg_direct(const uint8_t *data, uint32_t data_sz, uint32_t *out_w, uint32_t *out_h)
{
    if (!data || data_sz < 4 || !s_pix_buf[s_write_buf_idx]) return false;

    jd_mem_src_t src = { .data = data, .size = data_sz, .pos = 0 };
    JDEC jd;

    JRESULT rc = jd_prepare(&jd, jd_mem_input, s_tjpgd_work, sizeof(s_tjpgd_work), &src);
    if (rc != JDR_OK) {
        ESP_LOGW(TAG, "jd_prepare failed: %d", rc);
        return false;
    }

    s_tjpgd_frame_w = jd.width;
    s_tjpgd_frame_h = jd.height;
    if (out_w) *out_w = jd.width;
    if (out_h) *out_h = jd.height;

    rc = jd_decomp(&jd, jd_rgb_output, 0);
    if (rc != JDR_OK) {
        ESP_LOGW(TAG, "jd_decomp failed: %d", rc);
        return false;
    }

    return true;
}

/* ── Hardware Accelerated JPEG Decode with Software Fallback ─────────────── */
static bool decode_jpeg_frame(const uint8_t *data, uint32_t data_sz, uint32_t *out_w, uint32_t *out_h)
{
    if (!data || data_sz < 4 || !s_pix_buf[s_write_buf_idx]) return false;

    if (s_jpeg_decoder) {
        jpeg_decode_picture_info_t pic_info;
        esp_err_t ret = jpeg_decoder_get_info(data, data_sz, &pic_info);
        if (ret == ESP_OK) {
            *out_w = pic_info.width;
            *out_h = pic_info.height;

            jpeg_decode_cfg_t decode_cfg = {
                .conv_std      = JPEG_YUV_RGB_CONV_STD_BT601,
                .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
                .rgb_order     = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
            };

            uint32_t decoded_size = 0;
            ret = jpeg_decoder_process(
                s_jpeg_decoder, &decode_cfg,
                data, data_sz,
                (uint8_t *)s_pix_buf[s_write_buf_idx],
                s_pix_buf_sz[s_write_buf_idx],
                &decoded_size
            );
            if (ret == ESP_OK) {
                return true;
            }
            ESP_LOGD(TAG, "HW JPEG decode returned %d, falling back to TJPGD", ret);
        }
    }

    /* Fallback to software TJPGD */
    return decode_jpeg_direct(data, data_sz, out_w, out_h);
}

/* ── Dedicated Audio Playback Task (Core 0, Priority 11) ─────────────────── */
static void video_audio_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Video audio streaming task running on Core %d (Priority %d)",
             xPortGetCoreID(), uxTaskPriorityGet(NULL));

    while (1) {
        if (!s_audio_playing) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        size_t item_size = 0;
        /* Pull up to 4096 bytes of PCM16LE audio from ring buffer */
        uint8_t *item = (uint8_t *)xRingbufferReceiveUpTo(
            s_audio_ringbuf, &item_size, pdMS_TO_TICKS(40), 4096
        );

        if (item && item_size > 0) {
            esp_codec_dev_handle_t codec = audio_player_get_codec_handle();
            if (codec && s_audio_playing) {
                esp_codec_dev_write(codec, item, (int)item_size);
                s_audio_bytes_played += item_size;
            }
            vRingbufferReturnItem(s_audio_ringbuf, item);
        }
    }
}

/* ── AVI Info ───────────────────────────────────────────────────────────── */
typedef struct {
    uint32_t movi_offset;
    uint32_t total_frames;
    uint32_t fps_num;       /* microseconds per frame (usPerFrame from avih) */
    uint32_t width;
    uint32_t height;
    uint32_t audio_rate;
} avi_info_t;

/* ── State & Command Queue ───────────────────────────────────────────────── */
typedef enum { VP_CMD_PLAY=1, VP_CMD_PAUSE, VP_CMD_STOP } vp_cmd_e;
typedef struct { vp_cmd_e cmd; char path[256]; } vp_cmd_t;

static QueueHandle_t    s_cmd_queue  = NULL;
static QueueHandle_t    s_stat_queue = NULL;
static TaskHandle_t     s_task       = NULL;
static lv_obj_t        *s_canvas     = NULL;

/* ── Helpers ─────────────────────────────────────────────────────────────── */
static inline uint32_t read_u32le(FILE *f)
{
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) return 0;
    return (uint32_t)b[0] | ((uint32_t)b[1]<<8) | ((uint32_t)b[2]<<16) | ((uint32_t)b[3]<<24);
}

/* ── Flush audio ringbuffer completely ──────────────────────────────────── */
static void flush_audio_ringbuf(void)
{
    if (!s_audio_ringbuf) return;
    size_t dummy;
    uint8_t *item;
    while ((item = (uint8_t *)xRingbufferReceiveUpTo(s_audio_ringbuf, &dummy, 0, 4096)) != NULL) {
        vRingbufferReturnItem(s_audio_ringbuf, item);
    }
}

/* ── Parse AVI Header ───────────────────────────────────────────────────── */
static bool avi_parse_header(FILE *f, avi_info_t *info)
{
    memset(info, 0, sizeof(*info));
    fseek(f, 0, SEEK_SET);

    uint32_t riff = read_u32le(f);
    uint32_t size = read_u32le(f);
    uint32_t form = read_u32le(f);
    (void)size;
    if (riff != RIFF_RIFF || form != RIFF_AVI_) {
        ESP_LOGE(TAG, "Not an AVI file (RIFF=%08lx FORM=%08lx)",
                 (unsigned long)riff, (unsigned long)form);
        return false;
    }

    bool found_avih = false;

    while (!feof(f)) {
        uint32_t ck_id = read_u32le(f);
        uint32_t ck_sz = read_u32le(f);
        if (feof(f) || ferror(f)) break;

        long ck_pos = ftell(f);

        if (ck_id == RIFF_LIST) {
            uint32_t list_type = read_u32le(f);
            if (list_type == RIFF_movi) {
                info->movi_offset = (uint32_t)ftell(f);
                ESP_LOGI(TAG, "Found movi chunk at 0x%08lx", (unsigned long)info->movi_offset);
                return found_avih;
            }
            continue;
        }

        if (ck_id == RIFF_avih && ck_sz >= 32) {
            info->fps_num      = read_u32le(f); /* usPerFrame */
            read_u32le(f);                      /* maxBytesPerSec */
            read_u32le(f);                      /* paddingGranularity */
            read_u32le(f);                      /* flags */
            info->total_frames = read_u32le(f); /* totalFrames */
            read_u32le(f);                      /* initialFrames */
            read_u32le(f);                      /* streams */
            read_u32le(f);                      /* suggestedBufferSize */
            info->width        = read_u32le(f);
            info->height       = read_u32le(f);
            found_avih = true;
            fseek(f, ck_pos + (long)ck_sz, SEEK_SET);
            continue;
        }

        if (ck_id == RIFF_strh && ck_sz >= 56) {
            uint32_t fcc_type = read_u32le(f);
            if (fcc_type == RIFF_auds) {
                read_u32le(f); /* fccHandler */
                read_u32le(f); /* flags */
                read_u32le(f); /* priority + language */
                read_u32le(f); /* initialFrames */
                uint32_t scale = read_u32le(f);
                uint32_t rate  = read_u32le(f);
                if (scale > 0) info->audio_rate = rate / scale;
            }
            fseek(f, ck_pos + (long)ck_sz, SEEK_SET);
            continue;
        }

        if (ck_id == RIFF_JUNK || ck_id == RIFF_strf || ck_id == RIFF_idx1) {
            fseek(f, ck_pos + (long)ck_sz, SEEK_SET);
            continue;
        }

        if (ck_sz > 0) fseek(f, ck_pos + (long)ck_sz, SEEK_SET);
        else break;
    }

    return found_avih && (info->movi_offset > 0);
}

/* ── Video Playback Task (Core 1, Priority 9) ────────────────────────────── */
static void video_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Video player task running on Core %d (Priority %d)",
             xPortGetCoreID(), uxTaskPriorityGet(NULL));

    /* Allocate frame input buffer with DMA alignment */
    size_t alloc_fb_sz = 0;
    jpeg_decode_memory_alloc_cfg_t tx_mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER,
    };
    uint8_t *frame_buf = (uint8_t *)jpeg_alloc_decoder_mem(MAX_FRAME_SZ, &tx_mem_cfg, &alloc_fb_sz);
    if (!frame_buf) {
        frame_buf = (uint8_t *)heap_caps_aligned_alloc(64, MAX_FRAME_SZ, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    }

    /* Allocate audio chunk buffer in internal DMA RAM */
    uint8_t *audio_buf = (uint8_t *)heap_caps_aligned_alloc(64, AUDIO_CHUNK_SZ, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!audio_buf) {
        audio_buf = (uint8_t *)heap_caps_malloc(AUDIO_CHUNK_SZ, MALLOC_CAP_DMA);
    }

    if (!frame_buf || !audio_buf) {
        ESP_LOGE(TAG, "Failed to allocate playback buffers");
        vTaskDelete(NULL);
        return;
    }

    FILE          *fp     = NULL;
    avi_info_t     avi    = {0};
    bool           paused = false;
    video_status_t status = {.state = VIDEO_STATE_IDLE};
    int64_t        playback_start_us = 0;

    while (1) {
        vp_cmd_t cmd;
        TickType_t wait_ticks = (fp && !paused) ? 0 : portMAX_DELAY;
        if (xQueueReceive(s_cmd_queue, &cmd, wait_ticks) == pdTRUE) {
            if (cmd.cmd == VP_CMD_STOP) {
                s_audio_playing = false;
                if (fp) { fclose(fp); fp = NULL; }
                flush_audio_ringbuf();
                s_audio_bytes_played = 0;
                s_audio_buffered_bytes = 0;
                playback_start_us = 0;
                paused = false;
                status.state = VIDEO_STATE_IDLE;
                xQueueOverwrite(s_stat_queue, &status);
                continue;
            }
            if (cmd.cmd == VP_CMD_PAUSE) {
                paused = !paused;
                s_audio_playing = !paused;
                if (!paused && avi.fps_num > 0) {
                    /* Recalculate playback_start_us so pause duration is preserved */
                    playback_start_us = esp_timer_get_time() - (int64_t)status.current_frame * avi.fps_num;
                }
                status.state = paused ? VIDEO_STATE_PAUSED : VIDEO_STATE_PLAYING;
                xQueueOverwrite(s_stat_queue, &status);
                continue;
            }
            if (cmd.cmd == VP_CMD_PLAY) {
                s_audio_playing = false;
                if (fp) { fclose(fp); fp = NULL; }

                flush_audio_ringbuf();
                s_audio_bytes_played = 0;
                s_audio_buffered_bytes = 0;
                playback_start_us = 0;

                fp = fopen(cmd.path, "rb");
                if (!fp) {
                    ESP_LOGE(TAG, "Cannot open: %s", cmd.path);
                    status.state = VIDEO_STATE_ERROR;
                    xQueueOverwrite(s_stat_queue, &status);
                    continue;
                }

                /* 32KB internal DMA FATFS stream buffer for full hardware SDMMC DMA speed */
                if (s_io_buf) {
                    setvbuf(fp, s_io_buf, _IOFBF, IO_BUF_SZ);
                }

                if (!avi_parse_header(fp, &avi)) {
                    fclose(fp); fp = NULL;
                    status.state = VIDEO_STATE_ERROR;
                    xQueueOverwrite(s_stat_queue, &status);
                    continue;
                }

                /* Ensure ES8311 volume is fixed at 90% */
                esp_codec_dev_handle_t codec = audio_player_get_codec_handle();
                if (codec) {
                    esp_codec_dev_set_out_vol(codec, 90);
                }

                paused = false;
                status.state         = VIDEO_STATE_PLAYING;
                status.total_frames  = avi.total_frames;
                status.current_frame = 0;
                status.position_sec  = 0;
                status.width         = avi.width;
                status.height        = avi.height;
                status.fps           = (avi.fps_num > 0) ? (1000000 / avi.fps_num) : 24;
                status.duration_sec  = (avi.fps_num > 0 && avi.total_frames > 0)
                                       ? (uint32_t)((uint64_t)avi.total_frames * avi.fps_num / 1000000)
                                       : 0;
                xQueueOverwrite(s_stat_queue, &status);

                fseek(fp, (long)avi.movi_offset, SEEK_SET);
                continue;
            }
        }

        if (!fp || paused) continue;

        uint32_t chunk_id = read_u32le(fp);
        uint32_t chunk_sz = read_u32le(fp);
        if (feof(fp) || ferror(fp)) {
            ESP_LOGI(TAG, "AVI EOF reached — draining audio...");
            while (s_audio_playing) {
                size_t pending = 0;
                vRingbufferGetInfo(s_audio_ringbuf, NULL, NULL, NULL, NULL, &pending);
                if (pending == 0) break;
                vTaskDelay(pdMS_TO_TICKS(40));
            }
            s_audio_playing = false;
            fclose(fp); fp = NULL;
            status.state = VIDEO_STATE_FINISHED;
            xQueueOverwrite(s_stat_queue, &status);
            continue;
        }

        uint32_t padded = (chunk_sz + 1) & ~1u;

        if (chunk_id == RIFF_01wb) {
            /* ── Audio chunk (PCM16LE) -> stream directly into 256KB ring buffer ── */
            uint32_t remaining = chunk_sz;
            while (remaining > 0) {
                uint32_t chunk = (remaining > AUDIO_CHUNK_SZ) ? AUDIO_CHUNK_SZ : remaining;
                if (fread(audio_buf, 1, chunk, fp) == chunk) {
                    BaseType_t res = xRingbufferSend(
                        s_audio_ringbuf, audio_buf, chunk, pdMS_TO_TICKS(100)
                    );
                    if (res == pdTRUE) {
                        s_audio_buffered_bytes += chunk;
                    } else {
                        ESP_LOGW(TAG, "Audio ring buffer full, dropped %lu bytes", (unsigned long)chunk);
                    }
                } else {
                    break;
                }
                remaining -= chunk;
            }
            if (remaining > 0) fseek(fp, (long)remaining, SEEK_CUR);
            if (padded > chunk_sz) fseek(fp, (long)(padded - chunk_sz), SEEK_CUR);

            /* Start audio playback once pre-buffer threshold is reached */
            if (!s_audio_playing && s_audio_buffered_bytes >= PREBUFFER_AUDIO_SZ) {
                s_audio_playing = true;
                if (playback_start_us == 0) {
                    playback_start_us = esp_timer_get_time();
                }
            }
        } else if (chunk_id == RIFF_00dc) {
            /* ── Video frame (MJPEG) ── */
            if (chunk_sz == 0 || chunk_sz > MAX_FRAME_SZ) {
                if (padded > 0) fseek(fp, (long)padded, SEEK_CUR);
            } else {
                uint32_t frame_interval_us = (avi.fps_num > 0) ? avi.fps_num : 41666;

                /* Rock-solid real-time pacing via hardware microsecond timer */
                if (status.current_frame > 0 && playback_start_us > 0) {
                    int64_t target_us = playback_start_us + (int64_t)status.current_frame * frame_interval_us;
                    int64_t now_us = esp_timer_get_time();

                    if (now_us < target_us) {
                        int64_t delay_us = target_us - now_us;
                        if (delay_us > 1000 && delay_us < 200000) {
                            vTaskDelay(pdMS_TO_TICKS(delay_us / 1000));
                        }
                    } else if (now_us > target_us + 120000) {
                        /* Lagging by >120ms (3 frames): skip decoding this frame to catch up */
                        fseek(fp, (long)padded, SEEK_CUR);
                        status.current_frame++;
                        continue;
                    }
                }

                if (fread(frame_buf, 1, chunk_sz, fp) == chunk_sz) {
                    if (padded > chunk_sz) fseek(fp, (long)(padded - chunk_sz), SEEK_CUR);

                    if (s_canvas && s_pix_buf[s_write_buf_idx]) {
                        uint32_t fw = 0, fh = 0;
                        bool ok = decode_jpeg_frame(frame_buf, chunk_sz, &fw, &fh);
                        if (ok) {
                            int ready_idx = s_write_buf_idx;
                            s_frame_dsc[ready_idx].header.w      = (uint16_t)fw;
                            s_frame_dsc[ready_idx].header.h      = (uint16_t)fh;
                            s_frame_dsc[ready_idx].header.stride = (uint16_t)(fw * 2);
                            s_frame_dsc[ready_idx].data_size     = fw * fh * 2;
                            s_frame_dsc[ready_idx].data          = (const uint8_t *)s_pix_buf[ready_idx];

                            s_write_buf_idx ^= 1;

                            if (esp_lv_adapter_lock(pdMS_TO_TICKS(30)) == ESP_OK) {
                                lv_image_set_src(s_canvas, &s_frame_dsc[ready_idx]);
                                lv_obj_invalidate(s_canvas);
                                esp_lv_adapter_unlock();
                            }
                        }
                    }

                    if (status.current_frame == 0 && playback_start_us == 0) {
                        playback_start_us = esp_timer_get_time();
                    }

                    status.current_frame++;
                    if (status.fps > 0) {
                        status.position_sec = status.current_frame / status.fps;
                    }
                    xQueueOverwrite(s_stat_queue, &status);
                } else {
                    /* Short read: safely skip to next chunk */
                    ESP_LOGW(TAG, "Short read on frame %lu, skipping", (unsigned long)status.current_frame);
                    if (padded > 0) fseek(fp, (long)padded, SEEK_CUR);
                    status.current_frame++;
                }
            }
        } else if (chunk_id == RIFF_LIST) {
            fseek(fp, 4, SEEK_CUR); /* skip list tag */
        } else if (chunk_id == RIFF_idx1) {
            ESP_LOGI(TAG, "idx1 reached — draining audio...");
            while (s_audio_playing) {
                size_t pending = 0;
                vRingbufferGetInfo(s_audio_ringbuf, NULL, NULL, NULL, NULL, &pending);
                if (pending == 0) break;
                vTaskDelay(pdMS_TO_TICKS(40));
            }
            s_audio_playing = false;
            fclose(fp); fp = NULL;
            status.state = VIDEO_STATE_FINISHED;
            xQueueOverwrite(s_stat_queue, &status);
        } else {
            if (padded > 0) fseek(fp, (long)padded, SEEK_CUR);
        }
    }

    if (fp) fclose(fp);
    heap_caps_free(frame_buf);
    heap_caps_free(audio_buf);
    vTaskDelete(NULL);
}

/* ── Public API ──────────────────────────────────────────────────────────── */
esp_err_t video_player_init(void)
{
    if (s_task) return ESP_OK;

    /* Initialize ESP32-P4 Hardware JPEG Decoder */
    jpeg_decode_engine_cfg_t decode_eng_cfg = {
        .intr_priority = 0,
        .timeout_ms    = 1000,
    };
    esp_err_t ret = jpeg_new_decoder_engine(&decode_eng_cfg, &s_jpeg_decoder);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize hardware JPEG decoder (%d), will use TJPGD fallback", ret);
        s_jpeg_decoder = NULL;
    } else {
        ESP_LOGI(TAG, "ESP32-P4 Hardware JPEG Decoder initialized successfully");
    }

    /* Allocate double PSRAM pixel buffers with 2D-DMA alignment */
    jpeg_decode_memory_alloc_cfg_t mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };
    for (int i = 0; i < 2; i++) {
        size_t alloc_sz = 0;
        s_pix_buf[i] = (uint16_t *)jpeg_alloc_decoder_mem(
            MAX_VID_W * MAX_VID_H * sizeof(uint16_t), &mem_cfg, &alloc_sz
        );
        if (!s_pix_buf[i]) {
            s_pix_buf[i] = heap_caps_aligned_alloc(64, MAX_VID_W * MAX_VID_H * sizeof(uint16_t),
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
            alloc_sz = MAX_VID_W * MAX_VID_H * sizeof(uint16_t);
        }
        if (!s_pix_buf[i]) {
            ESP_LOGE(TAG, "Cannot alloc pixel buffer %d in PSRAM", i);
            return ESP_ERR_NO_MEM;
        }
        s_pix_buf_sz[i] = alloc_sz;
        memset(s_pix_buf[i], 0, alloc_sz);

        memset(&s_frame_dsc[i], 0, sizeof(lv_image_dsc_t));
        s_frame_dsc[i].header.cf     = LV_COLOR_FORMAT_RGB565;
        s_frame_dsc[i].header.magic  = LV_IMAGE_HEADER_MAGIC;
        s_frame_dsc[i].header.w      = MAX_VID_W;
        s_frame_dsc[i].header.h      = MAX_VID_H;
        s_frame_dsc[i].header.stride = MAX_VID_W * 2;
        s_frame_dsc[i].data_size     = MAX_VID_W * MAX_VID_H * 2;
        s_frame_dsc[i].data          = (const uint8_t *)s_pix_buf[i];
    }
    s_write_buf_idx = 0;

    /* Allocate 32KB FATFS stream buffer in INTERNAL DMA RAM for zero-copy SDMMC */
    s_io_buf = (char *)heap_caps_aligned_alloc(64, IO_BUF_SZ, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!s_io_buf) {
        ESP_LOGW(TAG, "Could not allocate 32KB IO buffer in internal DMA RAM, fallback to standard malloc");
        s_io_buf = (char *)heap_caps_malloc(IO_BUF_SZ, MALLOC_CAP_DMA);
    }

    /* Allocate 256KB Audio Ring Buffer in PSRAM (~1.45 seconds of PCM audio) */
    s_audio_ringbuf = xRingbufferCreateWithCaps(AUDIO_RINGBUF_SZ, RINGBUF_TYPE_BYTEBUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_audio_ringbuf) {
        ESP_LOGW(TAG, "Cannot create 256KB ring buffer in PSRAM, falling back to 64KB");
        s_audio_ringbuf = xRingbufferCreate(65536, RINGBUF_TYPE_BYTEBUF);
    }
    if (!s_audio_ringbuf) {
        ESP_LOGE(TAG, "Cannot create audio ring buffer");
        return ESP_ERR_NO_MEM;
    }

    s_cmd_queue  = xQueueCreate(1, sizeof(vp_cmd_t));
    s_stat_queue = xQueueCreate(1, sizeof(video_status_t));
    if (!s_cmd_queue || !s_stat_queue) return ESP_ERR_NO_MEM;

    video_status_t idle = {.state = VIDEO_STATE_IDLE};
    xQueueOverwrite(s_stat_queue, &idle);

    /* Dedicated audio task: Priority 11 on Core 0 */
    BaseType_t r_aud = xTaskCreatePinnedToCore(
        video_audio_task, "vid_audio", 8192, NULL, 11, &s_audio_task, 0
    );
    if (r_aud != pdPASS) return ESP_FAIL;

    /* Video decode task: Priority 9 on Core 1 */
    BaseType_t r_vid = xTaskCreatePinnedToCore(
        video_task, "video_play", 32768, NULL, 9, &s_task, 1
    );
    if (r_vid != pdPASS) return ESP_FAIL;

    ESP_LOGI(TAG, "Video Player init OK — Hardware JPEG Decoder + 256KB Audio Ring Buffer active");
    return ESP_OK;
}

esp_err_t video_player_play(const char *path, lv_obj_t *canvas)
{
    if (!s_cmd_queue) return ESP_ERR_INVALID_STATE;
    audio_player_pause();
    s_canvas     = canvas;
    vp_cmd_t cmd = {.cmd = VP_CMD_PLAY};
    strncpy(cmd.path, path, sizeof(cmd.path) - 1);
    xQueueOverwrite(s_cmd_queue, &cmd);
    return ESP_OK;
}

void video_player_toggle_pause(void)
{
    if (!s_cmd_queue) return;
    vp_cmd_t cmd = {.cmd = VP_CMD_PAUSE};
    xQueueOverwrite(s_cmd_queue, &cmd);
}

void video_player_stop(void)
{
    if (!s_cmd_queue) return;
    vp_cmd_t cmd = {.cmd = VP_CMD_STOP};
    xQueueOverwrite(s_cmd_queue, &cmd);
    s_canvas = NULL;
}

bool video_player_get_status(video_status_t *out)
{
    if (!s_stat_queue || !out) return false;
    return (xQueuePeek(s_stat_queue, out, 0) == pdTRUE);
}

bool video_player_is_active(void)
{
    if (!s_stat_queue) return false;
    video_status_t st;
    if (xQueuePeek(s_stat_queue, &st, 0) != pdTRUE) return false;
    return (st.state == VIDEO_STATE_PLAYING || st.state == VIDEO_STATE_PAUSED);
}
