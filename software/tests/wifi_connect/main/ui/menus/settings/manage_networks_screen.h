/**
 * @file ui/menus/settings/manage_networks_screen.h
 * @brief Saved Wi-Fi networks management screen renderer.
 */

#pragma once

#include <stdbool.h>

#include "lvgl.h"

#include "ui_manager.h"

/**
 * @brief Render the saved Wi-Fi networks management screen.
 *
 * @param screen Active LVGL screen.
 * @param state Current UI state.
 * @param callbacks UI callbacks.
 * @param can_go_back True when back navigation is available.
 * @param back_cb Back button callback.
 * @param connect_saved_cb Green tick callback.
 * @param forget_saved_cb Red X callback.
 * @param portal_toggle_cb Add/close portal callback.
 */
void manage_networks_screen_render(
    lv_obj_t *screen,
    const ui_manager_state_t *state,
    const ui_manager_callbacks_t *callbacks,
    bool can_go_back,
    lv_event_cb_t back_cb,
    lv_event_cb_t connect_saved_cb,
    lv_event_cb_t forget_saved_cb,
    lv_event_cb_t portal_toggle_cb
);
