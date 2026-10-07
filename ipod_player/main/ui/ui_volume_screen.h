/*
 * ui_volume_screen.h — Dedicated iPod Volume Adjustment Screen
 * Target: ESP32-P4, 480x800 portrait MIPI-DSI display (LVGL 9)
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *ui_volume_screen_create(lv_obj_t *parent);
void      ui_volume_screen_open(void);
void      ui_volume_screen_close(void);
void      ui_volume_screen_adjust(int delta);
void      ui_volume_screen_update(void);
bool      ui_volume_screen_is_active(void);

#ifdef __cplusplus
}
#endif
