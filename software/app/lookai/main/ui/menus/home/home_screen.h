/**
 * @file ui/menus/home/home_screen.h
 * @brief Home app picker UI.
 */

#pragma once

#include "lvgl.h"
#include "ui_manager.h"

void home_screen_render(
    lv_obj_t *body,
    const ui_manager_state_t *state,
    lv_event_cb_t stt_app_cb,
    lv_event_cb_t ai_app_cb,
    lv_event_cb_t tts_app_cb,
    lv_event_cb_t settings_cb
);

/**
 * @brief Update Home app picker connectivity state without recreating LVGL objects.
 */
bool home_screen_update(lv_obj_t *body, const ui_manager_state_t *state);

