/**
 * @file tts/tts_api_client.h
 * @brief OpenAI text-to-speech API client.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "audio_playback.h"
#include "esp_err.h"

typedef void (*tts_api_client_playback_started_cb_t)(void *user_ctx);

typedef struct {
    tts_api_client_playback_started_cb_t on_playback_started;
    void *user_ctx;
} tts_api_client_stream_callbacks_t;

/**
 * @brief Generate a WAV file from text using the configured OpenAI TTS endpoint.
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
 * @brief Generate speech and play it while the HTTP response is still downloading.
 *
 * The downloaded WAV is still saved to out_wav_path for debugging/replay, but
 * playback starts from a stream buffer as soon as the WAV header and a small
 * prebuffer are available.
 */
esp_err_t tts_api_client_generate_wav_streaming(
    const char *text,
    const char *out_wav_path,
    uint32_t *out_wav_bytes,
    audio_playback_result_t *out_playback_result,
    const tts_api_client_stream_callbacks_t *callbacks
);

/**
 * @brief Generate raw PCM speech and play it directly from a 3-block circular buffer.
 *
 * This path does not write the TTS result to SPIFFS. The OpenAI response is
 * requested as raw 24 kHz, 16-bit, mono PCM and streamed directly into the
 * playback ring buffer.
 */
esp_err_t tts_api_client_generate_pcm_streaming(
    const char *text,
    uint32_t *out_pcm_bytes,
    audio_playback_result_t *out_playback_result,
    const tts_api_client_stream_callbacks_t *callbacks
);

/**
 * @brief Return the last human-readable TTS API client error.
 */
const char *tts_api_client_get_last_error(void);
