/**
 * @file stt/stt_manager.h
 * @brief Speech-to-text backend state machine.
 */

#pragma once

#include "esp_err.h"

/**
 * @brief Start the STT backend task and event queue.
 */
esp_err_t stt_manager_start(void);

/**
 * @brief Notify the STT backend that Push to Talk was pressed.
 */
void stt_manager_press(void);

/**
 * @brief Notify the STT backend that Push to Talk was released.
 */
void stt_manager_release(void);
