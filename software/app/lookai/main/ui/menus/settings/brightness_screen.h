/**
 * @file ui/menus/settings/brightness_screen.h
 * @brief Brightness settings body renderer.
 */

#pragma once

#include "lvgl.h"

#include "ui_manager.h"

void brightness_screen_render(
    lv_obj_t *body,
    const ui_manager_state_t *state,
    lv_event_cb_t slider_changed_cb
);
