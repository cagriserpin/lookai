/**
 * @file ui/menus/settings/wifi_settings_screen.h
 * @brief Wi-Fi settings menu screen renderer.
 */

#pragma once

#include <stdbool.h>

#include "lvgl.h"

#include "ui_manager.h"

/**
 * @brief Render the Wi-Fi status/settings screen.
 *
 * @param screen Active LVGL screen.
 * @param state Current UI state.
 * @param callbacks UI callbacks.
 * @param can_go_back True when back navigation is available.
 * @param back_cb Back button callback.
 * @param manage_saved_cb Callback for opening saved networks.
 * @param portal_toggle_cb Callback for opening/closing setup portal.
 */
void wifi_settings_screen_render(
    lv_obj_t *screen,
    const ui_manager_state_t *state,
    const ui_manager_callbacks_t *callbacks,
    bool can_go_back,
    lv_event_cb_t back_cb,
    lv_event_cb_t manage_saved_cb,
    lv_event_cb_t portal_toggle_cb
);
