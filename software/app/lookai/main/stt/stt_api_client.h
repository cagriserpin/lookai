/**
 * @file stt/stt_api_client.h
 * @brief OpenAI-compatible speech-to-text API client.
 */

#pragma once

#include <stddef.h>

#include "esp_err.h"

/**
 * @brief Transcribe a WAV file with the configured STT provider.
 *
 * The client uses a multipart transcription endpoint.
 * The default project configuration points to OpenAI.
 *
 * @param wav_path Path to a WAV file on the local filesystem.
 * @param out_text Destination buffer for the transcript text.
 * @param out_text_size Size of out_text in bytes.
 *
 * @return ESP_OK on success.
 */
esp_err_t stt_api_client_transcribe_wav(
    const char *wav_path,
    char *out_text,
    size_t out_text_size
);

/**
 * @brief Return the last human-readable STT API client error.
 */
const char *stt_api_client_get_last_error(void);
