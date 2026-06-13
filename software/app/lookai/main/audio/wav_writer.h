/**
 * @file audio/wav_writer.h
 * @brief Minimal WAV header writer for PCM recordings.
 */

#pragma once

#include <stdint.h>
#include <stdio.h>

#include "esp_err.h"

/**
 * @brief Write a placeholder WAV header at the current file position.
 *
 * The header can be finalized later with wav_writer_finalize_header().
 */
esp_err_t wav_writer_write_placeholder_header(
    FILE *file,
    uint32_t sample_rate,
    uint16_t channels,
    uint16_t bits_per_sample
);

/**
 * @brief Rewrite the WAV header with the final PCM payload size.
 *
 * The file position is restored to the previous position before returning.
 */
esp_err_t wav_writer_finalize_header(
    FILE *file,
    uint32_t sample_rate,
    uint16_t channels,
    uint16_t bits_per_sample,
    uint32_t pcm_bytes
);
