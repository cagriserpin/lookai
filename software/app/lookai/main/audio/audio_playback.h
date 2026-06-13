/**
 * @file audio/audio_playback.h
 * @brief Speaker playback helpers for STT audio tests and saved recordings.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/**
 * @brief Result information for the last played WAV file.
 */
typedef struct {
    char path[96];                 /**< WAV path. */
    uint32_t duration_ms;          /**< Duration derived from PCM byte count. */
    uint32_t pcm_bytes;            /**< Raw PCM payload size. */
    uint32_t wav_bytes;            /**< Total WAV file size. */
    uint32_t sample_rate;          /**< WAV sample rate. */
    uint16_t channels;             /**< WAV channel count. */
    uint16_t bits_per_sample;      /**< WAV bit depth. */
} audio_playback_result_t;

/**
 * @brief Initialize the board speaker codec.
 *
 * This can be called more than once.
 */
esp_err_t audio_playback_init(void);

/**
 * @brief Start looping a 16 kHz mono 16-bit ~444 Hz sine LUT.
 *
 * The loop continues until audio_playback_stop_sine_440() is called.
 */
esp_err_t audio_playback_start_sine_440(void);

/**
 * @brief Stop the 440 Hz sine LUT loop.
 */
esp_err_t audio_playback_stop_sine_440(void);

/**
 * @brief Play a PCM WAV file synchronously.
 *
 * The function returns after playback finishes or an error occurs.
 */
esp_err_t audio_playback_play_wav_file(
    const char *path,
    audio_playback_result_t *out_result
);

/**
 * @brief Return true while the 440 Hz test tone is active.
 */
bool audio_playback_is_test_tone_playing(void);

/**
 * @brief Return true while any playback path is active.
 */
bool audio_playback_is_busy(void);
