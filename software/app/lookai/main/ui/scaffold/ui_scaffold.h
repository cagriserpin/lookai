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
    UI_SCAFFOLD_TITLE_ICON_HOME,           /**< Home/app picker title icon. */
    UI_SCAFFOLD_TITLE_ICON_SETTINGS,       /**< Settings title icon. */
    UI_SCAFFOLD_TITLE_ICON_WIFI,           /**< Wi-Fi title icon. */
    UI_SCAFFOLD_TITLE_ICON_BRIGHTNESS,     /**< Brightness title icon. */
    UI_SCAFFOLD_TITLE_ICON_VOLUME,         /**< Volume title icon. */
    UI_SCAFFOLD_TITLE_ICON_STT,            /**< Speech-to-text title icon. */
    UI_SCAFFOLD_TITLE_ICON_AI,             /**< AI assistant title icon. */
    UI_SCAFFOLD_TITLE_ICON_TTS,            /**< Text-to-speech title icon. */
} ui_scaffold_title_icon_t;

/**
 * @brief Configuration for the circular scaffold.
 */
typedef struct {
    const char *title;          /**< Screen title shown in the top slice. */
    ui_scaffold_title_icon_t title_icon; /**< Optional icon shown at the top of the top slice. */
    bool show_back;             /**< Whether to show the back icon in the left slice. */
    bool show_bottom_slice;     /**< Whether to reserve the bottom slice. */
    bool full_width_body;       /**< Let the body use the full screen width for immersive screens. */
    bool title_is_time;         /**< Update title text from the live clock and hide the left time chip. */
    lv_event_cb_t back_cb;      /**< Optional click callback for the back icon. */
    bool wifi_connected;        /**< True when Wi-Fi is connected. */
    const char *wifi_text;      /**< Optional Wi-Fi status text. */
    const char *time_text;      /**< Optional time/status text. */
    int wifi_rssi;              /**< Current Wi-Fi RSSI in dBm, or 0 when unknown. */
} ui_scaffold_config_t;

/**
 * @brief Create the circular-safe scaffold and return the central body object.
 *
 * @param screen Active LVGL screen.
 * @param config Scaffold configuration.
 * @return Scrollable central body object where screen-specific UI should render.
 */
lv_obj_t *ui_scaffold_create(lv_obj_t *screen, const ui_scaffold_config_t *config);

/**
 * @brief Update active scaffold status chips without rebuilding the screen.
 */
void ui_scaffold_update_status(const char *time_text, bool wifi_connected, const char *wifi_text, int wifi_rssi);
