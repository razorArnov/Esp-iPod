/*
 * media_scanner.h — Multi-format SD Card Media Scanner
 * Detects and categorizes: Songs (.mp3/.wav), Photos (.jpg/.jpeg/.png/.bmp),
 * Videos (.avi/.mjpeg) from /sdcard
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MEDIA_MAX_SONGS   128
#define MEDIA_MAX_PHOTOS  256
#define MEDIA_MAX_VIDEOS  64
#define MEDIA_PATH_LEN    256
#define MEDIA_NAME_LEN    128

typedef struct {
    char title[MEDIA_NAME_LEN];
    char artist[MEDIA_NAME_LEN];
    char album[MEDIA_NAME_LEN];
    char path[MEDIA_PATH_LEN];
    uint32_t duration_sec;
} media_song_t;

typedef struct {
    char name[MEDIA_NAME_LEN];
    char path[MEDIA_PATH_LEN];
} media_photo_t;

typedef struct {
    char name[MEDIA_NAME_LEN];
    char path[MEDIA_PATH_LEN];
    uint32_t duration_sec;
} media_video_t;

void media_scanner_scan(const char *root_path);

int                  media_get_song_count(void);
const media_song_t  *media_get_song(int idx);

int                   media_get_photo_count(void);
const media_photo_t  *media_get_photo(int idx);

int                   media_get_video_count(void);
const media_video_t  *media_get_video(int idx);

void media_basename_no_ext(const char *path, char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
