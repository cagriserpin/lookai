/**
 * @file ui/menus/settings/volume_screen.h
 * @brief Volume settings body renderer.
 */

#pragma once

#include "lvgl.h"

#include "ui_manager.h"

void volume_screen_render(
    lv_obj_t *body,
    const ui_manager_state_t *state,
    lv_event_cb_t volume_slider_changed_cb
);
