/**
 * @file tts/tts_api_client.h
 * @brief OpenAI-compatible text-to-speech API client.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/**
 * @brief Generate a WAV file from text using the configured TTS provider.
 *
 * @param text Text to synthesize.
 * @param out_wav_path Destination WAV path on the local filesystem.
 * @param out_wav_bytes Optional generated WAV byte count.
 *
 * @return ESP_OK on success.
 */
esp_err_t tts_api_client_generate_wav(
    const char *text,
    const char *out_wav_path,
    uint32_t *out_wav_bytes
);

/**
 * @brief Return the last human-readable TTS API client error.
 */
const char *tts_api_client_get_last_error(void);
