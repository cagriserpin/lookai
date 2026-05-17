/**
 * @file ui/menus/settings/manage_networks_screen.h
 * @brief Saved Wi-Fi networks management body renderer.
 */

#pragma once

#include "lvgl.h"

#include "ui_manager.h"

/**
 * @brief Render the saved Wi-Fi networks management screen body.
 *
 * @param body Central scaffold body object.
 * @param state Current UI state.
 * @param callbacks UI callbacks.
 * @param connect_saved_cb Green tick callback.
 * @param forget_saved_cb Red X callback.
 * @param portal_toggle_cb Add/close portal callback.
 */
void manage_networks_screen_render(
    lv_obj_t *body,
    const ui_manager_state_t *state,
    const ui_manager_callbacks_t *callbacks,
    lv_event_cb_t connect_saved_cb,
    lv_event_cb_t forget_saved_cb,
    lv_event_cb_t portal_toggle_cb
);
