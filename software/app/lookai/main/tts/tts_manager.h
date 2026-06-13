/**
 * @file tts/tts_manager.h
 * @brief Text-to-speech manager API.
 */

#pragma once

#include "esp_err.h"

/**
 * @brief Start the TTS manager background task.
 */
esp_err_t tts_manager_start(void);

/**
 * @brief Generate and play sample phrase 1.
 */
void tts_manager_speak_sample_1(void);

/**
 * @brief Generate and play sample phrase 2.
 */
void tts_manager_speak_sample_2(void);
