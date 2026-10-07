/*
 * media_scanner.c — Multi-format SD Card Media Scanner
 * Recursively scans /sdcard for Songs, Photos, and Videos.
 */
#include "media_scanner.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>

static const char *TAG = "MEDIA_SCAN";

static media_song_t  *s_songs  = NULL;
static media_photo_t *s_photos = NULL;
static media_video_t *s_videos = NULL;
static int s_song_count  = 0;
static int s_photo_count = 0;
static int s_video_count = 0;

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/* Copies basename without extension into out */
void media_basename_no_ext(const char *path, char *out, size_t out_len)
{
    if (!path || !out || out_len == 0) return;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    size_t i = 0;
    while (i + 1 < out_len && base[i] && base[i] != '.') {
        out[i] = base[i];
        i++;
    }
    out[i] = '\0';
}

/* Returns pointer to lowercase extension (including dot), or NULL */
static const char *get_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot;
}

static bool ext_is(const char *ext, const char *match) {
    if (!ext) return false;
    char low[16] = {0};
    size_t i = 0;
    for (; ext[i] && i < 15; i++) low[i] = (char)tolower((unsigned char)ext[i]);
    return strcmp(low, match) == 0;
}

/* ── ID3v2 title/artist/album extractor ──────────────────────────────────── */
static void extract_id3_tag(const char *path, char *title, char *artist, char *album,
                             size_t max_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return;

    uint8_t hdr[10];
    if (fread(hdr, 1, 10, f) < 10 || hdr[0]!='I' || hdr[1]!='D' || hdr[2]!='3') {
        fclose(f);
        return;
    }
    uint32_t tag_sz = ((uint32_t)(hdr[6]&0x7F)<<21)|((uint32_t)(hdr[7]&0x7F)<<14)|
                      ((uint32_t)(hdr[8]&0x7F)<<7) | ((uint32_t)(hdr[9]&0x7F));

    if (tag_sz > 256 * 1024) tag_sz = 256 * 1024;
    uint8_t *tag_buf = malloc(tag_sz);
    if (!tag_buf) { fclose(f); return; }
    if (fread(tag_buf, 1, tag_sz, f) < tag_sz) { free(tag_buf); fclose(f); return; }
    fclose(f);

    uint32_t pos = 0;
    bool got_title=false, got_artist=false, got_album=false;

    while (pos + 10 <= tag_sz && !(got_title && got_artist && got_album)) {
        uint8_t *fr = tag_buf + pos;
        if (!isupper(fr[0]) && !isdigit(fr[0])) break;
        uint32_t fr_sz = ((uint32_t)fr[4]<<24)|((uint32_t)fr[5]<<16)|
                         ((uint32_t)fr[6]<<8)  | (uint32_t)fr[7];
        if (fr_sz == 0 || pos + 10 + fr_sz > tag_sz) break;

        char id[5] = {fr[0], fr[1], fr[2], fr[3], 0};
        char *dst = NULL;
        bool *flag = NULL;
        if (strcmp(id,"TIT2")==0) { dst=title; flag=&got_title; }
        else if (strcmp(id,"TPE1")==0) { dst=artist; flag=&got_artist; }
        else if (strcmp(id,"TALB")==0) { dst=album; flag=&got_album; }

        if (dst && flag && fr_sz > 1) {
            uint8_t enc = fr[10];
            uint8_t *txt = fr + 11;
            size_t txt_len = fr_sz - 1;
            size_t out_pos = 0;

            if (enc == 1 || enc == 2) {
                /* UTF-16: skip BOM if present */
                size_t k = 0;
                if (txt_len >= 2 && ((txt[0]==0xFF && txt[1]==0xFE) || (txt[0]==0xFE && txt[1]==0xFF))) {
                    k = 2;
                }
                while (k + 1 < txt_len && out_pos + 1 < max_len) {
                    uint8_t ch = (enc == 1 && txt[0]==0xFF) ? txt[k] : (txt[k] ? txt[k] : txt[k+1]);
                    if (ch >= 32 && ch < 127) {
                        dst[out_pos++] = (char)ch;
                    }
                    k += 2;
                }
            } else {
                /* ISO-8859-1 or UTF-8 */
                for (size_t k = 0; k < txt_len && out_pos + 1 < max_len; k++) {
                    if (txt[k] == 0) break;
                    if (txt[k] >= 32) dst[out_pos++] = (char)txt[k];
                }
            }
            dst[out_pos] = '\0';
            *flag = (out_pos > 0);
        }
        pos += 10 + fr_sz;
    }
    free(tag_buf);
}

/* ── Recursive directory scanner ─────────────────────────────────────────── */
static void scan_dir(const char *dir_path)
{
    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *entry;
    char full[MEDIA_PATH_LEN];

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue; /* skip hidden/system files */

        /* Build full path */
        size_t base_len = strlen(dir_path);
        size_t name_len = strlen(entry->d_name);
        if (base_len + 1 + name_len >= MEDIA_PATH_LEN) continue;
        memcpy(full, dir_path, base_len);
        full[base_len] = '/';
        memcpy(full + base_len + 1, entry->d_name, name_len + 1);

        struct stat st;
        if (stat(full, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            scan_dir(full); /* recurse */
            continue;
        }

        const char *ext = get_ext(entry->d_name);
        if (!ext) continue;

        /* ── Songs ── */
        if ((ext_is(ext, ".mp3") || ext_is(ext, ".wav")) && s_song_count < MEDIA_MAX_SONGS) {
            media_song_t *s = &s_songs[s_song_count];
            memset(s, 0, sizeof(*s));
            snprintf(s->path, MEDIA_PATH_LEN, "%s", full);
            media_basename_no_ext(full, s->title, MEDIA_NAME_LEN);
            /* Try ID3v2 tags */
            char tmp_title[MEDIA_NAME_LEN] = {0};
            char tmp_artist[MEDIA_NAME_LEN] = {0};
            char tmp_album[MEDIA_NAME_LEN] = {0};
            extract_id3_tag(full, tmp_title, tmp_artist, tmp_album, MEDIA_NAME_LEN);
            if (tmp_title[0]) snprintf(s->title, MEDIA_NAME_LEN, "%s", tmp_title);
            if (tmp_artist[0]) snprintf(s->artist, MEDIA_NAME_LEN, "%s", tmp_artist);
            if (tmp_album[0]) snprintf(s->album, MEDIA_NAME_LEN, "%s", tmp_album);
            s->duration_sec = (uint32_t)(st.st_size / 16000);
            ESP_LOGI(TAG, "[Song  #%d] \"%s\" @ %s", s_song_count+1, s->title, full);
            s_song_count++;
        }
        /* ── Photos ── */
        else if ((ext_is(ext,".jpg")||ext_is(ext,".jpeg")||ext_is(ext,".png")||ext_is(ext,".bmp"))
                 && s_photo_count < MEDIA_MAX_PHOTOS) {
            media_photo_t *p = &s_photos[s_photo_count];
            memset(p, 0, sizeof(*p));
            snprintf(p->path, MEDIA_PATH_LEN, "%s", full);
            media_basename_no_ext(full, p->name, MEDIA_NAME_LEN);
            ESP_LOGI(TAG, "[Photo #%d] \"%s\" @ %s", s_photo_count+1, p->name, full);
            s_photo_count++;
        }
        /* ── Videos ── */
        else if ((ext_is(ext,".avi")||ext_is(ext,".mjpeg")||ext_is(ext,".mjpg"))
                 && s_video_count < MEDIA_MAX_VIDEOS) {
            media_video_t *v = &s_videos[s_video_count];
            memset(v, 0, sizeof(*v));
            snprintf(v->path, MEDIA_PATH_LEN, "%s", full);
            media_basename_no_ext(full, v->name, MEDIA_NAME_LEN);
            ESP_LOGI(TAG, "[Video #%d] \"%s\" @ %s", s_video_count+1, v->name, full);
            s_video_count++;
        }
    }
    closedir(dir);
}

/* ── Public API ──────────────────────────────────────────────────────────── */
void media_scanner_scan(const char *root_path)
{
    if (!s_songs)  s_songs  = calloc(MEDIA_MAX_SONGS, sizeof(media_song_t));
    if (!s_photos) s_photos = calloc(MEDIA_MAX_PHOTOS, sizeof(media_photo_t));
    if (!s_videos) s_videos = calloc(MEDIA_MAX_VIDEOS, sizeof(media_video_t));

    s_song_count  = 0;
    s_photo_count = 0;
    s_video_count = 0;
    if (s_songs)  memset(s_songs,  0, MEDIA_MAX_SONGS * sizeof(media_song_t));
    if (s_photos) memset(s_photos, 0, MEDIA_MAX_PHOTOS * sizeof(media_photo_t));
    if (s_videos) memset(s_videos, 0, MEDIA_MAX_VIDEOS * sizeof(media_video_t));

    ESP_LOGI(TAG, "===== Media Scanner: scanning %s =====", root_path);
    scan_dir(root_path);
    ESP_LOGI(TAG, "Scan complete: %d songs, %d photos, %d videos",
             s_song_count, s_photo_count, s_video_count);
}

int media_get_song_count(void)  { return s_song_count; }
int media_get_photo_count(void) { return s_photo_count; }
int media_get_video_count(void) { return s_video_count; }

const media_song_t  *media_get_song(int idx)  { return (idx>=0&&idx<s_song_count)  ? &s_songs[idx]  : NULL; }
const media_photo_t *media_get_photo(int idx) { return (idx>=0&&idx<s_photo_count) ? &s_photos[idx] : NULL; }
const media_video_t *media_get_video(int idx) { return (idx>=0&&idx<s_video_count) ? &s_videos[idx] : NULL; }
