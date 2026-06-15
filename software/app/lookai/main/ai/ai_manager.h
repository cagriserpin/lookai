/**
 * @file ai/ai_manager.h
 * @brief Voice assistant backend state machine.
 */

#pragma once

#include "esp_err.h"

/**
 * @brief Start the AI backend task and event queue.
 */
esp_err_t ai_manager_start(void);

/**
 * @brief Notify the AI backend that BOOT was pressed.
 */
void ai_manager_press(void);

/**
 * @brief Notify the AI backend that BOOT was released.
 */
void ai_manager_release(void);
