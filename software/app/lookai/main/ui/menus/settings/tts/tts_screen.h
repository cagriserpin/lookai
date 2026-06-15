/**
 * @file ui/menus/settings/tts/tts_screen.h
 * @brief Text-to-speech UI body renderer.
 */

#pragma once

#include "lvgl.h"

#include "ui_manager.h"

/**
 * @brief Render the text-to-speech screen.
 */
void tts_screen_render(
    lv_obj_t *body,
    const ui_manager_state_t *state,
    const ui_manager_callbacks_t *callbacks
);

/**
 * @brief Update the existing text-to-speech screen in place.
 */
bool tts_screen_update(
    lv_obj_t *body,
    const ui_manager_state_t *state,
    const ui_manager_callbacks_t *callbacks
);
