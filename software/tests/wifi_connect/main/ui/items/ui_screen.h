/**
 * @file ui/items/ui_screen.h
 * @brief Reusable LVGL screen/header/content helpers.
 */

#pragma once

#include <stdbool.h>

#include "lvgl.h"

/**
 * @brief Event callback used by the back button in screen headers.
 */
typedef void (*ui_screen_back_cb_t)(lv_event_t *event);

/**
 * @brief Clear and style the active screen background.
 *
 * @param screen Active screen object.
 */
void ui_screen_prepare(lv_obj_t *screen);

/**
 * @brief Create a common menu header.
 *
 * @param screen Active screen object.
 * @param title Header title.
 * @param can_go_back Whether to show a back button.
 * @param back_cb Optional back button callback.
 */
void ui_screen_create_header(
    lv_obj_t *screen,
    const char *title,
    bool can_go_back,
    lv_event_cb_t back_cb
);

/**
 * @brief Create the common scrollable content container.
 *
 * @param screen Active screen object.
 * @return Created scrollable content object.
 */
lv_obj_t *ui_screen_create_content(lv_obj_t *screen);
