/**
 * @file audio/audio_recorder.h
 * @brief Push-to-talk WAV recorder using the board microphone codec.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define AUDIO_RECORDER_SAMPLE_RATE 16000U
#define AUDIO_RECORDER_CHANNELS 1U
#define AUDIO_RECORDER_BITS_PER_SAMPLE 16U

/**
 * @brief Result information for the last saved recording.
 */
typedef struct {
    char path[96];                 /**< Saved WAV path. */
    uint32_t duration_ms;          /**< Recording duration derived from PCM byte count. */
    uint32_t pcm_bytes;            /**< Raw PCM payload size. */
    uint32_t wav_bytes;            /**< Total WAV file size including the 44-byte header. */
    uint32_t sample_rate;          /**< Sample rate used for the recording. */
    uint16_t channels;             /**< Channel count used for the recording. */
    uint16_t bits_per_sample;      /**< Bit depth used for the recording. */
} audio_recorder_result_t;

/**
 * @brief Initialize SPIFFS and the board microphone codec.
 *
 * This can be called more than once.
 */
esp_err_t audio_recorder_init(void);

/**
 * @brief Start recording to the fixed STT WAV path.
 *
 * Existing file content is overwritten.
 */
esp_err_t audio_recorder_start(void);

/**
 * @brief Stop recording, finalize the WAV header, and return file stats.
 */
esp_err_t audio_recorder_stop(audio_recorder_result_t *out_result);

/**
 * @brief Return true while the recorder task is actively writing samples.
 */
bool audio_recorder_is_recording(void);

/**
 * @brief Get the fixed WAV path used by the recorder.
 */
const char *audio_recorder_get_path(void);
