/*
 * photo_viewer.h — Full-screen Photo Viewer with Slideshow & Click Wheel control
 */
#pragma once

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create the photo viewer LVGL screen on parent.
 * Call once during UI init.
 */
lv_obj_t *photo_viewer_create(lv_obj_t *parent);

/**
 * Open a specific photo (by index into media_get_photo()).
 * Switches to photo viewer screen automatically.
 */
void photo_viewer_open(int index);

/**
 * Navigate to next / previous photo.
 */
void photo_viewer_next(void);
void photo_viewer_prev(void);

/**
 * Toggle automatic slideshow (auto-advances every ~4 seconds).
 */
void photo_viewer_toggle_slideshow(void);

/**
 * Tick — call periodically from ui_update_loop (every 250ms).
 * Handles slideshow timer advancement.
 */
void photo_viewer_tick(void);

/**
 * Returns true if the photo viewer screen is currently active.
 */
bool photo_viewer_is_active(void);

/**
 * Hide the viewer and return to whatever was shown before.
 */
void photo_viewer_close(void);

#ifdef __cplusplus
}
#endif
