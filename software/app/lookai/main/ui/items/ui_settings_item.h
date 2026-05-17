/**
 * @file ui/items/ui_settings_item.h
 * @brief Reusable settings menu item helper.
 */

#pragma once

#include <stdint.h>

#include "lvgl.h"

/**
 * @brief Built-in settings item icon types.
 */
typedef enum {
    UI_SETTINGS_ITEM_ICON_WIFI,       /**< Wi-Fi symbol in colored circle. */
    UI_SETTINGS_ITEM_ICON_SUN,        /**< Drawn sun icon in colored circle. */
    UI_SETTINGS_ITEM_ICON_TEXT,       /**< Custom text/glyph in colored circle. */
} ui_settings_item_icon_type_t;

/**
 * @brief Settings item icon configuration.
 */
typedef struct {
    ui_settings_item_icon_type_t type; /**< Icon type. */
    const char *text;                  /**< Text when type is UI_SETTINGS_ITEM_ICON_TEXT. */
    uint32_t color;                    /**< Circle background color. */
} ui_settings_item_icon_t;

/**
 * @brief Create a generic settings menu item.
 */
lv_obj_t *ui_settings_item_create(
    lv_obj_t *parent,
    const ui_settings_item_icon_t *icon_config,
    const char *title,
    lv_event_cb_t cb,
    void *user_data
);
