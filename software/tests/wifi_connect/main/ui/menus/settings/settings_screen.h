/**
 * @file ui/menus/settings/settings_screen.h
 * @brief Root settings menu body renderer.
 */

#pragma once

#include "lvgl.h"

#include "ui_manager.h"

/**
 * @brief Render the root settings screen body.
 *
 * @param body Central scaffold body object.
 * @param state Current UI state.
 * @param callbacks UI callbacks.
 * @param wifi_button_cb Callback for opening Wi-Fi settings.
 */
void settings_screen_render(
    lv_obj_t *body,
    const ui_manager_state_t *state,
    const ui_manager_callbacks_t *callbacks,
    lv_event_cb_t wifi_button_cb
);
