/**
 * @file audio/audio_playback.h
 * @brief Speaker playback helpers for STT audio tests.
 */

#pragma once

#include <stdbool.h>

#include "esp_err.h"

/**
 * @brief Initialize the board speaker codec.
 *
 * This can be called more than once.
 */
esp_err_t audio_playback_init(void);

/**
 * @brief Start looping a 16 kHz mono 16-bit ~444 Hz sine LUT.
 *
 * The loop continues until audio_playback_stop() is called.
 */
esp_err_t audio_playback_start_sine_440(void);

/**
 * @brief Stop the current playback loop.
 */
esp_err_t audio_playback_stop(void);

/**
 * @brief Return true while audio playback is active.
 */
bool audio_playback_is_playing(void);
