/**
 * @file ui/menus/settings/stt/stt_screen.h
 * @brief Speech-to-text UI body renderer.
 */

#pragma once

#include "lvgl.h"

#include "ui_manager.h"

/**
 * @brief Render the speech-to-text screen.
 *
 * The screen is still UI-only. It forwards Push to Talk press/release events
 * to callbacks provided by the application layer.
 */
void stt_screen_render(
    lv_obj_t *body,
    const ui_manager_state_t *state,
    const ui_manager_callbacks_t *callbacks
);
