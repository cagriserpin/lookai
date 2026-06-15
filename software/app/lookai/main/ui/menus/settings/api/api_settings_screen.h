/**
 * @file ui/menus/settings/api/api_settings_screen.h
 * @brief STT/TTS/AI runtime parameter settings screens.
 */

#pragma once

#include "lvgl.h"
#include "ui_manager.h"

typedef enum {
    API_SETTINGS_KIND_STT = 0,
    API_SETTINGS_KIND_AI,
    API_SETTINGS_KIND_TTS,
} api_settings_kind_t;

void api_settings_screen_render(
    lv_obj_t *body,
    api_settings_kind_t kind,
    const ui_manager_state_t *state,
    lv_event_cb_t option_cb
);
