/**
 * @file ui/menus/settings/ai/ai_screen.h
 * @brief Voice assistant UI body renderer.
 */

#pragma once

#include "lvgl.h"

#include "ui_manager.h"

/**
 * @brief Render the AI assistant screen.
 */
void ai_screen_render(
    lv_obj_t *body,
    const ui_manager_state_t *state,
    const ui_manager_callbacks_t *callbacks
);

/**
 * @brief Update the existing AI screen in place.
 */
bool ai_screen_update(
    lv_obj_t *body,
    const ui_manager_state_t *state,
    const ui_manager_callbacks_t *callbacks
);
