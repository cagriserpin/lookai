/**
 * @file ui/items/ui_button.h
 * @brief Reusable LVGL button helpers.
 */

#pragma once

#include <stdint.h>

#include "lvgl.h"

/**
 * @brief Create a styled button with a centered label.
 *
 * @param parent Parent LVGL object.
 * @param text Button label.
 * @param width Button width.
 * @param height Button height.
 * @param color Button background color.
 * @param cb Optional click event callback.
 * @param user_data Optional event user data.
 * @return Created button object.
 */
lv_obj_t *ui_button_create(
    lv_obj_t *parent,
    const char *text,
    int width,
    int height,
    uint32_t color,
    lv_event_cb_t cb,
    void *user_data
);
