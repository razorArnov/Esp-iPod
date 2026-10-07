/*
 * iPod Classic UI — Split Menu View & Music Library
 * Faithful replica of AahanDoesGit/iPod-classic-Cm5
 */
#include "ui_main.h"
#include "media_scanner.h"
#include "photo_viewer.h"
#include "video_player.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>

static const char *TAG = "IPOD_MENU";

#define MAX_SONGS   128

typedef struct {
    char title[256];
    char artist[256];
    char album[256];
    char path[256];
    uint32_t duration_sec;
} ipod_song_t;

/* Safe string copy function with zero compiler truncation warnings */
static void copy_str(char *dst, const char *src, size_t max_len)
{
    if (!dst || max_len == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0;
    while (i + 1 < max_len && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Navigation Levels */
typedef enum {
    NAV_MAIN = 0,
    NAV_MUSIC,
    NAV_SONGS,
    NAV_PHOTOS,
    NAV_VIDEOS,
} nav_level_t;

static nav_level_t s_nav_level = NAV_MAIN;

/* Menu item lists */
static const char *MAIN_MENU[] = {
    "Music",
    "Videos",
    "Photos",
    "Extras",
    "Volume",
    "Shuffle Songs",
    "Now Playing"
};
#define MAIN_COUNT (sizeof(MAIN_MENU) / sizeof(MAIN_MENU[0]))

static const char *MUSIC_MENU[] = {
    "Songs",
    "Artists",
    "Albums"
};
#define MUSIC_COUNT (sizeof(MUSIC_MENU) / sizeof(MUSIC_MENU[0]))

/* Library state */
static ipod_song_t *s_songs = NULL;
static int s_song_count = 0;
static int s_sel_idx    = 0;
static int s_item_count = 0;

/* LVGL UI elements */
static lv_obj_t *s_menu_root     = NULL;
static lv_obj_t *s_left_list     = NULL;
static lv_obj_t *s_right_preview = NULL;
static lv_obj_t *s_pv_icon_box   = NULL;
static lv_obj_t *s_pv_note       = NULL;
static lv_obj_t *s_pv_title      = NULL;
static lv_obj_t *s_pv_sub        = NULL;

/* ─────────────────────────────────────────────────────────────────────────
 * Forward declarations
 * ─────────────────────────────────────────────────────────────────────── */
static void build_menu_items(const char **items, int count, const char *title);
static void build_songs_list(void);
static void build_photos_list(void);
static void build_videos_list(void);
static void update_preview(void);
static void scan_music_dir(void);

/* ─────────────────────────────────────────────────────────────────────────
 * Row Styling (iPod classic blue gradient on active selection)
 * ─────────────────────────────────────────────────────────────────────── */
static void set_row_selected(lv_obj_t *row, bool selected)
{
    if (!row) return;

    lv_obj_t *lbl  = lv_obj_get_child(row, 0);
    lv_obj_t *chev = lv_obj_get_child(row, 1);

    if (selected) {
        lv_obj_set_style_bg_color(row, IPOD_COL_SEL_TOP, 0);
        lv_obj_set_style_bg_grad_color(row, IPOD_COL_SEL_BOT, 0);
        lv_obj_set_style_bg_grad_dir(row, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);

        if (lbl)  lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        if (chev) lv_obj_set_style_text_color(chev, lv_color_hex(0xFFFFFF), 0);
    } else {
        lv_obj_set_style_bg_color(row, IPOD_COL_BG, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_grad_dir(row, LV_GRAD_DIR_NONE, 0);

        if (lbl)  lv_obj_set_style_text_color(lbl, IPOD_COL_TEXT, 0);
        if (chev) lv_obj_set_style_text_color(chev, IPOD_COL_CHEVRON, 0);
    }
    lv_obj_invalidate(row);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Create a single menu item row
 * ─────────────────────────────────────────────────────────────────────── */
static void on_item_tap(lv_event_t *e);

static lv_obj_t *make_row(lv_obj_t *parent, const char *label_text, int idx)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, IPOD_SPLIT_LEFT_W, IPOD_MENU_ITEM_H);
    lv_obj_set_style_bg_color(row, IPOD_COL_BG, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);

    /* Thin bottom divider */
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, IPOD_COL_ITEM_LINE, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_pad_left(row, 10, 0);
    lv_obj_set_style_pad_right(row, 6, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(row, (void *)(intptr_t)idx);
    lv_obj_add_event_cb(row, on_item_tap, LV_EVENT_CLICKED, NULL);

    /* Title Label */
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, IPOD_FONT_ITEM, 0);
    lv_obj_set_style_text_color(lbl, IPOD_COL_TEXT, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(lbl, IPOD_SPLIT_LEFT_W - 32);

    /* Chevron ▶ */
    lv_obj_t *chev = lv_label_create(row);
    lv_label_set_text(chev, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(chev, IPOD_COL_CHEVRON, 0);
    lv_obj_set_style_text_font(chev, IPOD_FONT_SMALL, 0);
    lv_obj_align(chev, LV_ALIGN_RIGHT_MID, 0, 0);

    return row;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Update Right Preview Pane based on highlighted item
 * ─────────────────────────────────────────────────────────────────────── */
static void update_preview(void)
{
    if (!s_right_preview || !s_pv_title || !s_pv_sub || !s_pv_note) return;

    if (s_nav_level == NAV_MAIN) {
        switch (s_sel_idx) {
            case 0: /* Music */
                lv_label_set_text(s_pv_note, LV_SYMBOL_AUDIO);
                lv_label_set_text(s_pv_title, "Music");
                char cnt_buf[32];
                snprintf(cnt_buf, sizeof(cnt_buf), "%d Songs", s_song_count);
                lv_label_set_text(s_pv_sub, cnt_buf);
                break;
            case 1: /* Videos */
                lv_label_set_text(s_pv_note, LV_SYMBOL_VIDEO);
                lv_label_set_text(s_pv_title, "Videos");
                char vid_buf[32];
                snprintf(vid_buf, sizeof(vid_buf), "%d Videos", media_get_video_count());
                lv_label_set_text(s_pv_sub, vid_buf);
                break;
            case 2: /* Photos */
                lv_label_set_text(s_pv_note, LV_SYMBOL_IMAGE);
                lv_label_set_text(s_pv_title, "Photos");
                char pho_buf[32];
                snprintf(pho_buf, sizeof(pho_buf), "%d Photos", media_get_photo_count());
                lv_label_set_text(s_pv_sub, pho_buf);
                break;
            case 3: /* Extras */
                lv_label_set_text(s_pv_note, LV_SYMBOL_LIST);
                lv_label_set_text(s_pv_title, "Extras");
                lv_label_set_text(s_pv_sub, "Clock & Games");
                break;
            case 4: /* Volume */
                lv_label_set_text(s_pv_note, LV_SYMBOL_VOLUME_MAX);
                lv_label_set_text(s_pv_title, "Volume");
                char vol_buf[32];
                snprintf(vol_buf, sizeof(vol_buf), "Level: %d%%", audio_player_get_volume());
                lv_label_set_text(s_pv_sub, vol_buf);
                break;
            case 5: /* Shuffle */
                lv_label_set_text(s_pv_note, LV_SYMBOL_SHUFFLE);
                lv_label_set_text(s_pv_title, "Shuffle");
                lv_label_set_text(s_pv_sub, "All Songs");
                break;
            case 6: /* Now Playing */
                lv_label_set_text(s_pv_note, LV_SYMBOL_PLAY);
                lv_label_set_text(s_pv_title, g_ipod_state.title[0] ? g_ipod_state.title : "Now Playing");
                lv_label_set_text(s_pv_sub, g_ipod_state.artist[0] ? g_ipod_state.artist : "iPod Player");
                break;
            default:
                break;
        }
    } else if (s_nav_level == NAV_MUSIC) {
        lv_label_set_text(s_pv_note, LV_SYMBOL_AUDIO);
        if (s_sel_idx < MUSIC_COUNT) {
            lv_label_set_text(s_pv_title, MUSIC_MENU[s_sel_idx]);
        }
        char cnt_buf[32];
        snprintf(cnt_buf, sizeof(cnt_buf), "%d Songs Available", s_song_count);
        lv_label_set_text(s_pv_sub, cnt_buf);
    } else if (s_nav_level == NAV_SONGS) {
        if (s_sel_idx < s_song_count) {
            lv_label_set_text(s_pv_note, LV_SYMBOL_AUDIO);
            lv_label_set_text(s_pv_title, s_songs[s_sel_idx].title);
            lv_label_set_text(s_pv_sub, s_songs[s_sel_idx].artist);
        }
    }
}

/* ─────────────────────────────────────────────────────────────────────────
 * Populate List with items
 * ─────────────────────────────────────────────────────────────────────── */
static void build_menu_items(const char **items, int count, const char *title)
{
    lv_obj_clean(s_left_list);
    ui_statusbar_set_title(title);
    s_sel_idx = 0;
    s_item_count = count;

    for (int i = 0; i < count; i++) {
        lv_obj_t *row = make_row(s_left_list, items[i], i);
        if (i == 0) set_row_selected(row, true);
    }
    update_preview();
}

static void build_songs_list(void)
{
    lv_obj_clean(s_left_list);
    ui_statusbar_set_title("Songs");
    s_sel_idx = 0;
    s_item_count = s_song_count;

    if (s_song_count == 0) {
        lv_obj_t *lbl = lv_label_create(s_left_list);
        lv_label_set_text(lbl, "No songs on SD");
        lv_obj_set_style_text_color(lbl, IPOD_COL_TEXT_SUB, 0);
        lv_obj_set_style_text_font(lbl, IPOD_FONT_ITEM, 0);
        lv_obj_center(lbl);
    } else {
        for (int i = 0; i < s_song_count; i++) {
            lv_obj_t *row = make_row(s_left_list, s_songs[i].title, i);
            if (i == 0) set_row_selected(row, true);
        }
    }
    update_preview();
}

/* ─────────────────────────────────────────────────────────────────────────
 * Photos & Videos List Builders (using media_scanner index)
 * ─────────────────────────────────────────────────────────────────────── */
static void build_photos_list(void)
{
    lv_obj_clean(s_left_list);
    ui_statusbar_set_title("Photos");
    s_sel_idx   = 0;
    int count   = media_get_photo_count();
    s_item_count = count;

    if (count == 0) {
        lv_obj_t *lbl = lv_label_create(s_left_list);
        lv_label_set_text(lbl, "No photos on SD");
        lv_obj_set_style_text_color(lbl, IPOD_COL_TEXT_SUB, 0);
        lv_obj_set_style_text_font(lbl, IPOD_FONT_ITEM, 0);
        lv_obj_center(lbl);
    } else {
        for (int i = 0; i < count; i++) {
            const media_photo_t *p = media_get_photo(i);
            if (p) {
                lv_obj_t *row = make_row(s_left_list, p->name, i);
                if (i == 0) set_row_selected(row, true);
            }
        }
    }
    /* Preview pane: show photo icon */
    lv_label_set_text(s_pv_note, LV_SYMBOL_IMAGE);
    char cnt[32];
    snprintf(cnt, sizeof(cnt), "%d Photos", count);
    lv_label_set_text(s_pv_title, "Photos");
    lv_label_set_text(s_pv_sub, cnt);
}

static void build_videos_list(void)
{
    lv_obj_clean(s_left_list);
    ui_statusbar_set_title("Videos");
    s_sel_idx   = 0;
    int count   = media_get_video_count();
    s_item_count = count;

    if (count == 0) {
        lv_obj_t *lbl = lv_label_create(s_left_list);
        lv_label_set_text(lbl, "No videos on SD");
        lv_obj_set_style_text_color(lbl, IPOD_COL_TEXT_SUB, 0);
        lv_obj_set_style_text_font(lbl, IPOD_FONT_ITEM, 0);
        lv_obj_center(lbl);
    } else {
        for (int i = 0; i < count; i++) {
            const media_video_t *v = media_get_video(i);
            if (v) {
                lv_obj_t *row = make_row(s_left_list, v->name, i);
                if (i == 0) set_row_selected(row, true);
            }
        }
    }
    /* Preview pane: show video icon */
    lv_label_set_text(s_pv_note, LV_SYMBOL_VIDEO);
    char cnt[32];
    snprintf(cnt, sizeof(cnt), "%d Videos", count);
    lv_label_set_text(s_pv_title, "Videos");
    lv_label_set_text(s_pv_sub, cnt);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Check if a filename is a supported audio file
 * ─────────────────────────────────────────────────────────────────────── */
static bool is_supported_audio(const char *name)
{
    if (!name || name[0] == '\0') return false;
    /* Skip hidden files and AppleDouble shadow metadata files */
    if (name[0] == '.' || name[0] == '_') return false;

    size_t len = strlen(name);
    if (len < 5) return false;

    const char *ext = name + len - 4;
    if (strcasecmp(ext, ".mp3") == 0 ||
        strcasecmp(ext, ".wav") == 0 ||
        strcasecmp(ext, ".m4a") == 0 ||
        strcasecmp(ext, ".aac") == 0) {
        return true;
    }
    if (len >= 5) {
        const char *ext5 = name + len - 5;
        if (strcasecmp(ext5, ".flac") == 0) return true;
    }
    return false;
}

/* Format a clean human-readable title from filename */
static void clean_title(char *dst, const char *raw_name, size_t max_len)
{
    char temp[256];
    copy_str(temp, raw_name, sizeof(temp));

    /* Remove extension */
    char *dot = strrchr(temp, '.');
    if (dot) *dot = '\0';

    /* Skip leading track numbers like "01 - ", "01. ", "01 " */
    char *start = temp;
    while (*start && isdigit((unsigned char)*start)) start++;
    if (*start == '.' || *start == '-' || *start == '_' || *start == ' ') {
        start++;
        while (*start == ' ' || *start == '-' || *start == '_') start++;
    }
    if (strlen(start) == 0) start = temp; /* Fallback if all digits */

    copy_str(dst, start, max_len);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Recursive directory scanner for /sdcard
 * ─────────────────────────────────────────────────────────────────────── */
static void scan_recursive_dir(const char *dir_path, int depth)
{
    if (depth > 4 || s_song_count >= MAX_SONGS) return;

    DIR *d = opendir(dir_path);
    if (!d) return;

    ESP_LOGI(TAG, "Scanning SD directory: %s", dir_path);
    struct dirent *ent;
    char full_path[320];

    while ((ent = readdir(d)) != NULL && s_song_count < MAX_SONGS) {
        const char *name = ent->d_name;
        if (!name || name[0] == '\0') continue;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

        /* Skip macOS system, trash, and hidden files/directories */
        if (name[0] == '.' || name[0] == '_') continue;
        if (strncasecmp(name, "TRASH", 5) == 0) continue;
        if (strncasecmp(name, "SPOTLI", 6) == 0) continue;
        if (strncasecmp(name, "FSEVEN", 6) == 0) continue;
        if (strcasecmp(name, "System Volume Information") == 0) continue;
        if (strcasecmp(name, "FOUND.000") == 0) continue;

        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, name);

        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                scan_recursive_dir(full_path, depth + 1);
            } else if (S_ISREG(st.st_mode)) {
                if (is_supported_audio(name)) {
                    copy_str(s_songs[s_song_count].path, full_path, sizeof(s_songs[s_song_count].path));
                    
                    /* Clean title from filename */
                    clean_title(s_songs[s_song_count].title, name, sizeof(s_songs[s_song_count].title));

                    /* Artist name from parent folder if inside a subfolder */
                    const char *slash = strrchr(dir_path, '/');
                    if (slash && strcmp(dir_path, "/sdcard") != 0 && strcasecmp(dir_path, "/sdcard/Music") != 0) {
                        copy_str(s_songs[s_song_count].artist, slash + 1, sizeof(s_songs[s_song_count].artist));
                        copy_str(s_songs[s_song_count].album,  "SD Card", sizeof(s_songs[s_song_count].album));
                    } else {
                        copy_str(s_songs[s_song_count].artist, "SD Card Audio", sizeof(s_songs[s_song_count].artist));
                        copy_str(s_songs[s_song_count].album,  "Music", sizeof(s_songs[s_song_count].album));
                    }

                    s_songs[s_song_count].duration_sec = 210;
                    ESP_LOGI(TAG, "-> [Track #%d]: \"%s\" @ %s", s_song_count + 1, s_songs[s_song_count].title, full_path);
                    s_song_count++;
                }
            }
        }
    }
    closedir(d);
}

static void scan_music_dir(void)
{
    if (!s_songs) {
        s_songs = (ipod_song_t *)calloc(MAX_SONGS, sizeof(ipod_song_t));
    }
    s_song_count = 0;
    if (s_songs) {
        memset(s_songs, 0, MAX_SONGS * sizeof(ipod_song_t));
    }
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, " Starting Full SD Card Recursive Search on /sdcard");
    ESP_LOGI(TAG, "==================================================");

    scan_recursive_dir("/sdcard", 0);

    ESP_LOGI(TAG, "Total SD audio tracks found: %d", s_song_count);

    /* If no real audio files found on SD card, load demo library */
    if (s_song_count == 0) {
        ESP_LOGW(TAG, "No audio files (.mp3, .wav, .flac, .m4a) found on /sdcard! Loading demo tracks...");
        static const struct {
            const char *title;
            const char *artist;
            const char *album;
            uint32_t dur;
        } DEMOS[] = {
            { "Viva La Vida",        "Coldplay",        "Viva la Vida",                242 },
            { "Bohemian Rhapsody",   "Queen",           "A Night at the Opera",        354 },
            { "Hotel California",    "Eagles",          "Hotel California",            391 },
            { "Billie Jean",         "Michael Jackson", "Thriller",                    294 },
            { "Take On Me",          "a-ha",            "Hunting High and Low",        225 },
            { "Sweet Child O' Mine", "Guns N' Roses",   "Appetite for Destruction",    356 },
            { "Clocks",              "Coldplay",        "A Rush of Blood to the Head", 307 },
            { "Shape of You",        "Ed Sheeran",      "Divide",                      233 }
        };
        int num_demos = (int)(sizeof(DEMOS) / sizeof(DEMOS[0]));
        for (int i = 0; i < num_demos && i < MAX_SONGS; i++) {
            copy_str(s_songs[i].title,  DEMOS[i].title,  sizeof(s_songs[i].title));
            copy_str(s_songs[i].artist, DEMOS[i].artist, sizeof(s_songs[i].artist));
            copy_str(s_songs[i].album,  DEMOS[i].album,  sizeof(s_songs[i].album));
            s_songs[i].duration_sec = DEMOS[i].dur;
            s_songs[i].path[0] = '\0';
            s_song_count++;
        }
    }
    g_ipod_state.song_count = s_song_count;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Action execution for selected row (Center / OK Button)
 * ─────────────────────────────────────────────────────────────────────── */
void ui_menu_select(void)
{
    ESP_LOGI(TAG, "ui_menu_select: nav_level=%d, sel_idx=%d", s_nav_level, s_sel_idx);

    if (s_nav_level == NAV_MAIN) {
        switch (s_sel_idx) {
            case 0: /* Music */
                s_nav_level = NAV_MUSIC;
                build_menu_items(MUSIC_MENU, MUSIC_COUNT, "Music");
                break;

            case 1: /* Videos */
                media_scanner_scan("/sdcard");
                s_nav_level = NAV_VIDEOS;
                build_videos_list();
                break;

            case 2: /* Photos */
                media_scanner_scan("/sdcard");
                s_nav_level = NAV_PHOTOS;
                build_photos_list();
                break;

            case 3: /* Extras */
                break;

            case 4: /* Volume */
                ui_set_view(VIEW_VOLUME);
                break;

            case 5: /* Shuffle Songs */
                if (s_song_count > 0) {
                    int r = rand() % s_song_count;
                    g_ipod_state.current_index = r;
                    copy_str(g_ipod_state.title,  s_songs[r].title,  sizeof(g_ipod_state.title));
                    copy_str(g_ipod_state.artist, s_songs[r].artist, sizeof(g_ipod_state.artist));
                    copy_str(g_ipod_state.album,  s_songs[r].album,  sizeof(g_ipod_state.album));
                    g_ipod_state.duration_sec = s_songs[r].duration_sec;
                    g_ipod_state.position_sec = 0;
                    g_ipod_state.is_playing   = true;
                    g_ipod_state.is_paused    = false;
                    audio_player_play(s_songs[r].path);
                    ui_set_view(VIEW_NOW_PLAYING);
                }
                break;

            case 6: /* Now Playing */
                ui_set_view(VIEW_NOW_PLAYING);
                break;

            default:
                break;
        }
    } else if (s_nav_level == NAV_MUSIC) {
        /* Entering Songs */
        s_nav_level = NAV_SONGS;
        build_songs_list();
    } else if (s_nav_level == NAV_SONGS) {
        if (s_sel_idx >= 0 && s_sel_idx < s_song_count) {
            g_ipod_state.current_index = s_sel_idx;
            copy_str(g_ipod_state.title,  s_songs[s_sel_idx].title,  sizeof(g_ipod_state.title));
            copy_str(g_ipod_state.artist, s_songs[s_sel_idx].artist, sizeof(g_ipod_state.artist));
            copy_str(g_ipod_state.album,  s_songs[s_sel_idx].album,  sizeof(g_ipod_state.album));
            g_ipod_state.duration_sec = s_songs[s_sel_idx].duration_sec;
            g_ipod_state.position_sec = 0;
            g_ipod_state.is_playing   = true;
            g_ipod_state.is_paused    = false;
            audio_player_play(s_songs[s_sel_idx].path);
            ui_set_view(VIEW_NOW_PLAYING);
            ESP_LOGI(TAG, "Selected track [%d]: %s", s_sel_idx, s_songs[s_sel_idx].title);
        }
    } else if (s_nav_level == NAV_PHOTOS) {
        /* Open selected photo in full-screen viewer */
        photo_viewer_open(s_sel_idx);
        ui_set_view(VIEW_PHOTO_VIEWER);
    } else if (s_nav_level == NAV_VIDEOS) {
        /* Open selected video in video player screen */
        ui_video_screen_open(s_sel_idx);
    }
}

static void on_item_tap(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(row);

    lv_obj_t *old_row = lv_obj_get_child(s_left_list, s_sel_idx);
    if (old_row) set_row_selected(old_row, false);
    s_sel_idx = idx;
    set_row_selected(row, true);
    update_preview();

    ui_menu_select();
}

/* ─────────────────────────────────────────────────────────────────────────
 * Public Scroll & Back handlers (driven by Click Wheel)
 * ─────────────────────────────────────────────────────────────────────── */
void ui_menu_scroll(int direction)
{
    if (s_item_count <= 0 || !s_left_list) return;
    int new_idx = s_sel_idx + direction;
    if (new_idx < 0) new_idx = 0;
    if (new_idx >= s_item_count) new_idx = s_item_count - 1;

    if (new_idx != s_sel_idx) {
        lv_obj_t *old_row = lv_obj_get_child(s_left_list, s_sel_idx);
        if (old_row) set_row_selected(old_row, false);

        s_sel_idx = new_idx;
        lv_obj_t *new_row = lv_obj_get_child(s_left_list, s_sel_idx);
        if (new_row) {
            set_row_selected(new_row, true);
            lv_obj_scroll_to_view(new_row, LV_ANIM_ON);
        }
        update_preview();
        ESP_LOGI(TAG, "Menu scroll: idx=%d/%d", s_sel_idx, s_item_count);
    }
}

void ui_menu_back(void)
{
    ESP_LOGI(TAG, "ui_menu_back from level=%d", s_nav_level);
    if (s_nav_level == NAV_SONGS) {
        s_nav_level = NAV_MUSIC;
        build_menu_items(MUSIC_MENU, MUSIC_COUNT, "Music");
    } else if (s_nav_level == NAV_MUSIC || s_nav_level == NAV_PHOTOS || s_nav_level == NAV_VIDEOS) {
        s_nav_level = NAV_MAIN;
        build_menu_items(MAIN_MENU, MAIN_COUNT, "iPod");
    } else if (s_nav_level == NAV_MAIN) {
        ESP_LOGI(TAG, "Already at iPod Home Menu");
    }
}

/* ─────────────────────────────────────────────────────────────────────────
 * Next & Previous Track Handlers (>>| and |<<)
 * ─────────────────────────────────────────────────────────────────────── */
void ui_library_play_next(void)
{
    if (s_song_count <= 0) return;
    int idx = g_ipod_state.current_index + 1;
    if (idx >= s_song_count) idx = 0;
    g_ipod_state.current_index = idx;
    copy_str(g_ipod_state.title,  s_songs[idx].title,  sizeof(g_ipod_state.title));
    copy_str(g_ipod_state.artist, s_songs[idx].artist, sizeof(g_ipod_state.artist));
    copy_str(g_ipod_state.album,  s_songs[idx].album,  sizeof(g_ipod_state.album));
    g_ipod_state.duration_sec = s_songs[idx].duration_sec;
    g_ipod_state.position_sec = 0;
    g_ipod_state.is_playing   = true;
    g_ipod_state.is_paused    = false;
    audio_player_play(s_songs[idx].path);

    if (ui_get_current_view() == VIEW_NOW_PLAYING) {
        ui_now_playing_update(&g_ipod_state);
    }
    ESP_LOGI(TAG, "Skip NEXT: [%d] %s - %s", idx, s_songs[idx].title, s_songs[idx].artist);
}

void ui_library_play_prev(void)
{
    if (s_song_count <= 0) return;

    /* If more than 3 seconds in, restart current track */
    if (g_ipod_state.position_sec > 3) {
        g_ipod_state.position_sec = 0;
        audio_player_seek(0);
        if (ui_get_current_view() == VIEW_NOW_PLAYING) {
            ui_now_playing_update(&g_ipod_state);
        }
        ESP_LOGI(TAG, "Restart current track [%d]", g_ipod_state.current_index);
        return;
    }

    int idx = g_ipod_state.current_index - 1;
    if (idx < 0) idx = s_song_count - 1;
    g_ipod_state.current_index = idx;
    copy_str(g_ipod_state.title,  s_songs[idx].title,  sizeof(g_ipod_state.title));
    copy_str(g_ipod_state.artist, s_songs[idx].artist, sizeof(g_ipod_state.artist));
    copy_str(g_ipod_state.album,  s_songs[idx].album,  sizeof(g_ipod_state.album));
    g_ipod_state.duration_sec = s_songs[idx].duration_sec;
    g_ipod_state.position_sec = 0;
    g_ipod_state.is_playing   = true;
    g_ipod_state.is_paused    = false;
    audio_player_play(s_songs[idx].path);

    if (ui_get_current_view() == VIEW_NOW_PLAYING) {
        ui_now_playing_update(&g_ipod_state);
    }
    ESP_LOGI(TAG, "Skip PREV: [%d] %s - %s", idx, s_songs[idx].title, s_songs[idx].artist);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Initialize Split Menu View (Left list + Right preview)
 * ─────────────────────────────────────────────────────────────────────── */
lv_obj_t *ui_menu_view_create(lv_obj_t *parent)
{
    s_menu_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_menu_root);
    lv_obj_set_size(s_menu_root, IPOD_SCREEN_W, IPOD_CONTENT_H);
    lv_obj_align(s_menu_root, LV_ALIGN_TOP_LEFT, 0, IPOD_STATUSBAR_H);
    lv_obj_clear_flag(s_menu_root, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Left Pane: Menu list ── */
    s_left_list = lv_obj_create(s_menu_root);
    lv_obj_remove_style_all(s_left_list);
    lv_obj_set_size(s_left_list, IPOD_SPLIT_LEFT_W, IPOD_CONTENT_H);
    lv_obj_align(s_left_list, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(s_left_list, IPOD_COL_BG, 0);
    lv_obj_set_style_bg_opa(s_left_list, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(s_left_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_left_list, LV_DIR_VER);

    /* Center vertical divider */
    lv_obj_t *div = lv_obj_create(s_menu_root);
    lv_obj_remove_style_all(div);
    lv_obj_set_size(div, 1, IPOD_CONTENT_H);
    lv_obj_align(div, LV_ALIGN_TOP_LEFT, IPOD_SPLIT_LEFT_W, 0);
    lv_obj_set_style_bg_color(div, IPOD_COL_DIVIDER, 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);

    /* ── Right Pane: Preview / Album Art ── */
    s_right_preview = lv_obj_create(s_menu_root);
    lv_obj_remove_style_all(s_right_preview);
    lv_obj_set_size(s_right_preview, IPOD_SPLIT_RIGHT_W, IPOD_CONTENT_H);
    lv_obj_align(s_right_preview, LV_ALIGN_TOP_LEFT, IPOD_SPLIT_LEFT_W + 1, 0);
    lv_obj_set_style_bg_color(s_right_preview, IPOD_COL_PREVIEW_BG, 0);
    lv_obj_set_style_bg_opa(s_right_preview, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_right_preview, LV_OBJ_FLAG_SCROLLABLE);

    /* Preview icon / art box */
    s_pv_icon_box = lv_obj_create(s_right_preview);
    lv_obj_remove_style_all(s_pv_icon_box);
    lv_obj_set_size(s_pv_icon_box, 140, 140);
    lv_obj_align(s_pv_icon_box, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_bg_color(s_pv_icon_box, IPOD_COL_ART_BG, 0);
    lv_obj_set_style_bg_opa(s_pv_icon_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_pv_icon_box, 6, 0);
    lv_obj_set_style_shadow_color(s_pv_icon_box, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(s_pv_icon_box, 35, 0);
    lv_obj_set_style_shadow_width(s_pv_icon_box, 8, 0);
    lv_obj_set_style_shadow_ofs_y(s_pv_icon_box, 3, 0);

    s_pv_note = lv_label_create(s_pv_icon_box);
    lv_label_set_text(s_pv_note, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(s_pv_note, IPOD_FONT_BIG, 0);
    lv_obj_set_style_text_color(s_pv_note, IPOD_COL_TEXT_SUB, 0);
    lv_obj_center(s_pv_note);

    /* Preview Title */
    s_pv_title = lv_label_create(s_right_preview);
    lv_label_set_text(s_pv_title, "Music");
    lv_obj_set_style_text_font(s_pv_title, IPOD_FONT_ITEM, 0);
    lv_obj_set_style_text_color(s_pv_title, IPOD_COL_TEXT, 0);
    lv_obj_set_width(s_pv_title, IPOD_SPLIT_RIGHT_W - 20);
    lv_obj_set_style_text_align(s_pv_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_pv_title, LV_ALIGN_TOP_MID, 0, 204);

    /* Preview Subtitle */
    s_pv_sub = lv_label_create(s_right_preview);
    lv_label_set_text(s_pv_sub, "iPod Library");
    lv_obj_set_style_text_font(s_pv_sub, IPOD_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_pv_sub, IPOD_COL_TEXT_SUB, 0);
    lv_obj_set_width(s_pv_sub, IPOD_SPLIT_RIGHT_W - 20);
    lv_obj_set_style_text_align(s_pv_sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_pv_sub, LV_ALIGN_TOP_MID, 0, 230);

    /* Scan storage and populate Main Menu */
    scan_music_dir();
    build_menu_items(MAIN_MENU, MAIN_COUNT, "iPod");

    ESP_LOGI(TAG, "Split Menu View built (Left 240px, Right 240px)");
    return s_menu_root;
}