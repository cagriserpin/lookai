/**
 * @file ui/menus/settings/wifi_settings_screen.h
 * @brief Wi-Fi settings menu body renderer.
 */

#pragma once

#include "lvgl.h"

#include "ui_manager.h"

/**
 * @brief Render the Wi-Fi status/settings screen body.
 *
 * @param body Central scaffold body object.
 * @param state Current UI state.
 * @param callbacks UI callbacks.
 * @param manage_saved_cb Callback for opening saved networks.
 * @param portal_toggle_cb Callback for opening/closing setup portal.
 */
void wifi_settings_screen_render(
    lv_obj_t *body,
    const ui_manager_state_t *state,
    const ui_manager_callbacks_t *callbacks,
    lv_event_cb_t manage_saved_cb,
    lv_event_cb_t portal_toggle_cb
);
