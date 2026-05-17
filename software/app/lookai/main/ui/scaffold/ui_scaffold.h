/**
 * @file ui/scaffold/ui_scaffold.h
 * @brief Circular-screen scaffold layout API.
 */

#pragma once

#include <stdbool.h>

#include "lvgl.h"

/**
 * @brief Built-in title icons supported by the scaffold.
 */
typedef enum {
    UI_SCAFFOLD_TITLE_ICON_NONE = 0,       /**< No title icon. */
    UI_SCAFFOLD_TITLE_ICON_SETTINGS,       /**< Settings title icon. */
    UI_SCAFFOLD_TITLE_ICON_WIFI,           /**< Wi-Fi title icon. */
    UI_SCAFFOLD_TITLE_ICON_BRIGHTNESS,     /**< Brightness title icon. */
} ui_scaffold_title_icon_t;

/**
 * @brief Configuration for the circular scaffold.
 */
typedef struct {
    const char *title;          /**< Screen title shown in the top slice. */
    ui_scaffold_title_icon_t title_icon; /**< Optional icon shown at the top of the top slice. */
    bool show_back;             /**< Whether to show the back icon in the left slice. */
    bool show_bottom_slice;     /**< Whether to reserve the bottom slice. */
    lv_event_cb_t back_cb;      /**< Optional click callback for the back icon. */
} ui_scaffold_config_t;

/**
 * @brief Create the circular-safe scaffold and return the central body object.
 *
 * @param screen Active LVGL screen.
 * @param config Scaffold configuration.
 * @return Scrollable central body object where screen-specific UI should render.
 */
lv_obj_t *ui_scaffold_create(lv_obj_t *screen, const ui_scaffold_config_t *config);
