/**
 * @file audio/audio_playback.h
 * @brief Speaker playback helpers for STT audio tests and saved recordings.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/**
 * @brief Result information for the last played WAV file.
 */
typedef struct {
    char path[96];                 /**< WAV path or stream label. */
    uint32_t duration_ms;          /**< Duration derived from PCM byte count. */
    uint32_t pcm_bytes;            /**< Raw PCM payload size. */
    uint32_t wav_bytes;            /**< Total WAV/container byte count. */
    uint32_t sample_rate;          /**< WAV sample rate. */
    uint16_t channels;             /**< WAV channel count. */
    uint16_t bits_per_sample;      /**< WAV bit depth. */
} audio_playback_result_t;

/**
 * @brief Aggregate metrics for direct PCM streaming playback.
 */
typedef struct {
    uint32_t producer_bytes;          /**< Bytes written by the HTTP producer. */
    uint32_t consumer_bytes;          /**< Bytes consumed by the playback task. */
    uint32_t ring_full_wait_count;    /**< Producer had to wait for a free block. */
    uint32_t ring_empty_wait_count;   /**< Consumer waited for data after playback started. */
    uint32_t underrun_count;          /**< Playback-side data underrun events. */
    uint32_t max_fill_bytes;          /**< Maximum queued PCM payload bytes observed. */
    uint32_t max_ready_blocks;        /**< Maximum ready blocks observed. */
    uint32_t block_count;             /**< Ring block count. */
    uint32_t block_size;              /**< Ring block size in bytes. */
} audio_playback_stream_metrics_t;

typedef struct audio_playback_pcm_stream audio_playback_pcm_stream_t;

typedef struct audio_playback_stream audio_playback_stream_t;

typedef enum {
    AUDIO_PLAYBACK_STREAM_EVENT_STARTED = 0,
    AUDIO_PLAYBACK_STREAM_EVENT_DONE,
} audio_playback_stream_event_t;

typedef void (*audio_playback_stream_callback_t)(
    audio_playback_stream_event_t event,
    const audio_playback_result_t *result,
    void *user_ctx
);

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
 * @brief Play a raw signed 16-bit PCM file synchronously.
 *
 * The function returns after playback finishes or an error occurs.
 */
esp_err_t audio_playback_play_pcm_file(
    const char *path,
    uint32_t sample_rate,
    uint16_t channels,
    uint16_t bits_per_sample,
    audio_playback_result_t *out_result
);

/**
 * @brief Start a streaming WAV playback consumer.
 *
 * Bytes written with audio_playback_stream_wav_write() must start at the
 * beginning of a WAV container. The stream task parses the header, opens the
 * codec, and plays PCM payload while more bytes are still being written.
 */
esp_err_t audio_playback_stream_wav_start(
    audio_playback_stream_t **out_stream,
    audio_playback_stream_callback_t callback,
    void *user_ctx
);

/**
 * @brief Write WAV/container bytes into an active stream playback consumer.
 */
esp_err_t audio_playback_stream_wav_write(
    audio_playback_stream_t *stream,
    const void *data,
    size_t len,
    uint32_t timeout_ms
);

/**
 * @brief Mark the producer side as finished.
 */
void audio_playback_stream_wav_finish(
    audio_playback_stream_t *stream,
    esp_err_t producer_result
);

/**
 * @brief Wait for streaming playback to finish and free stream resources.
 */
esp_err_t audio_playback_stream_wav_wait(
    audio_playback_stream_t *stream,
    uint32_t timeout_ms,
    audio_playback_result_t *out_result
);

/**
 * @brief Start a direct PCM streaming playback consumer.
 *
 * The producer must feed raw signed 16-bit PCM bytes with
 * audio_playback_stream_pcm_write(). No WAV container is expected.
 */
esp_err_t audio_playback_stream_pcm_start(
    audio_playback_pcm_stream_t **out_stream,
    uint32_t sample_rate,
    uint16_t channels,
    uint16_t bits_per_sample,
    audio_playback_stream_callback_t callback,
    void *user_ctx
);

/**
 * @brief Write raw PCM bytes into an active direct PCM stream.
 */
esp_err_t audio_playback_stream_pcm_write(
    audio_playback_pcm_stream_t *stream,
    const void *data,
    size_t len,
    uint32_t timeout_ms
);

/**
 * @brief Mark the direct PCM producer side as finished.
 */
void audio_playback_stream_pcm_finish(
    audio_playback_pcm_stream_t *stream,
    esp_err_t producer_result
);

/**
 * @brief Wait for direct PCM streaming playback to finish and free resources.
 */
esp_err_t audio_playback_stream_pcm_wait(
    audio_playback_pcm_stream_t *stream,
    uint32_t timeout_ms,
    audio_playback_result_t *out_result,
    audio_playback_stream_metrics_t *out_metrics
);

/**
 * @brief Return true while the 440 Hz test tone is active.
 */
bool audio_playback_is_test_tone_playing(void);

/**
 * @brief Return true while any playback path is active.
 */
bool audio_playback_is_busy(void);

/**
 * @brief Request the currently active playback stream/file to stop.
 */
esp_err_t audio_playback_stop_current(void);
