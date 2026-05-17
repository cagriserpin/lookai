/**
 * @file ui/scaffold/ui_scaffold.h
 * @brief Circular-screen scaffold layout API.
 */

#pragma once

#include <stdbool.h>

#include "lvgl.h"

/**
 * @brief Configuration for the circular scaffold.
 *
 * The scaffold owns global layout regions: title slice, left action slice,
 * central body, right slice, and bottom slice. Screens only render inside the
 * returned body object.
 */
typedef struct {
    const char *title;      /**< Screen title shown in the top slice. */
    bool show_back;         /**< Whether to show the back icon in the left slice. */
    lv_event_cb_t back_cb;  /**< Optional click callback for the back icon. */
} ui_scaffold_config_t;

/**
 * @brief Create the circular-safe scaffold and return the central body object.
 *
 * @param screen Active LVGL screen.
 * @param config Scaffold configuration.
 * @return Scrollable central body object where screen-specific UI should render.
 */
lv_obj_t *ui_scaffold_create(lv_obj_t *screen, const ui_scaffold_config_t *config);
