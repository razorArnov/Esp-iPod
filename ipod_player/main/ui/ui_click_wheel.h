/*
 * iPod Classic UI — Click Wheel Component
 * Implements the iconic rotary touch click wheel from iPod-classic-Cm5
 */
#pragma once

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WHEEL_BTN_NONE = 0,
    WHEEL_BTN_MENU,
    WHEEL_BTN_CENTER,
    WHEEL_BTN_PLAY_PAUSE,
    WHEEL_BTN_PREV,
    WHEEL_BTN_NEXT
} wheel_button_t;

typedef void (*wheel_scroll_cb_t)(int direction); // +1: clockwise / down, -1: ccw / up
typedef void (*wheel_click_cb_t)(wheel_button_t btn);

/**
 * @brief Initialize and build Click Wheel widget at the bottom of parent container
 * @param parent Root container (typically bottom 400px of screen)
 * @param scroll_cb Callback when rotary drag scrolls
 * @param click_cb Callback when quadrant or center button is clicked
 * @return lv_obj_t* The wheel container
 */
lv_obj_t *ui_click_wheel_create(lv_obj_t *parent, wheel_scroll_cb_t scroll_cb, wheel_click_cb_t click_cb);

#ifdef __cplusplus
}
#endif
