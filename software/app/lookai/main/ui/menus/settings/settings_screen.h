/**
 * @file ui/menus/settings/settings_screen.h
 * @brief Root settings menu body renderer.
 */

#pragma once

#include "lvgl.h"

#include "ui_manager.h"

void settings_screen_render(
    lv_obj_t *body,
    const ui_manager_state_t *state,
    const ui_manager_callbacks_t *callbacks,
    lv_event_cb_t wifi_button_cb,
    lv_event_cb_t brightness_button_cb,
    lv_event_cb_t stt_button_cb,
    lv_event_cb_t tts_button_cb
);
