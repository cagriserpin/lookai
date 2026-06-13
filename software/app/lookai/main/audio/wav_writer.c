/**
 * @file audio/wav_writer.c
 * @brief Minimal WAV header writer for PCM recordings.
 */

#include "wav_writer.h"

#include <string.h>

#define WAV_HEADER_SIZE 44

static void write_le16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFF);
    dst[1] = (uint8_t)((value >> 8) & 0xFF);
}

static void write_le32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFF);
    dst[1] = (uint8_t)((value >> 8) & 0xFF);
    dst[2] = (uint8_t)((value >> 16) & 0xFF);
    dst[3] = (uint8_t)((value >> 24) & 0xFF);
}

static void build_wav_header(
    uint8_t header[WAV_HEADER_SIZE],
    uint32_t sample_rate,
    uint16_t channels,
    uint16_t bits_per_sample,
    uint32_t pcm_bytes
)
{
    uint32_t byte_rate =
        sample_rate * (uint32_t)channels * (uint32_t)bits_per_sample / 8U;
    uint16_t block_align = (uint16_t)(channels * bits_per_sample / 8U);
    uint32_t riff_size = 36U + pcm_bytes;

    memset(header, 0, WAV_HEADER_SIZE);

    memcpy(&header[0], "RIFF", 4);
    write_le32(&header[4], riff_size);
    memcpy(&header[8], "WAVE", 4);

    memcpy(&header[12], "fmt ", 4);
    write_le32(&header[16], 16);              /* PCM fmt chunk size */
    write_le16(&header[20], 1);               /* PCM format */
    write_le16(&header[22], channels);
    write_le32(&header[24], sample_rate);
    write_le32(&header[28], byte_rate);
    write_le16(&header[32], block_align);
    write_le16(&header[34], bits_per_sample);

    memcpy(&header[36], "data", 4);
    write_le32(&header[40], pcm_bytes);
}

esp_err_t wav_writer_write_placeholder_header(
    FILE *file,
    uint32_t sample_rate,
    uint16_t channels,
    uint16_t bits_per_sample
)
{
    if (file == NULL || channels == 0 || bits_per_sample == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t header[WAV_HEADER_SIZE];
    build_wav_header(header, sample_rate, channels, bits_per_sample, 0);

    return fwrite(header, 1, WAV_HEADER_SIZE, file) == WAV_HEADER_SIZE ?
        ESP_OK :
        ESP_FAIL;
}

esp_err_t wav_writer_finalize_header(
    FILE *file,
    uint32_t sample_rate,
    uint16_t channels,
    uint16_t bits_per_sample,
    uint32_t pcm_bytes
)
{
    if (file == NULL || channels == 0 || bits_per_sample == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    long current_pos = ftell(file);
    if (current_pos < 0) {
        return ESP_FAIL;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        return ESP_FAIL;
    }

    uint8_t header[WAV_HEADER_SIZE];
    build_wav_header(header, sample_rate, channels, bits_per_sample, pcm_bytes);

    esp_err_t result = fwrite(header, 1, WAV_HEADER_SIZE, file) == WAV_HEADER_SIZE ?
        ESP_OK :
        ESP_FAIL;

    if (fseek(file, current_pos, SEEK_SET) != 0) {
        return ESP_FAIL;
    }

    return result;
}
