/**
 * @file audio/audio_playback.c
 * @brief Speaker playback helpers for STT audio tests and saved recordings.
 */

#include "audio_playback.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "driver/gpio.h"
#include "esp_codec_dev.h"
#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "audio_playback";

#define AUDIO_PLAYBACK_TEST_SAMPLE_RATE 16000
#define AUDIO_PLAYBACK_TEST_CHANNELS 1
#define AUDIO_PLAYBACK_TEST_BITS_PER_SAMPLE 16

#define AUDIO_PLAYBACK_PA_CTRL_GPIO GPIO_NUM_46
#define AUDIO_PLAYBACK_VOLUME 100

#define AUDIO_PLAYBACK_TASK_STACK_SIZE 4096
#define AUDIO_PLAYBACK_TASK_PRIORITY 5
#define AUDIO_PLAYBACK_BUFFER_SIZE 1024
#define AUDIO_PLAYBACK_WAV_HEADER_SIZE 44

#define SINE_440_LUT_COUNT 36
#define SINE_440_CHUNK_REPEATS 20
#define SINE_440_CHUNK_SAMPLES (SINE_440_LUT_COUNT * SINE_440_CHUNK_REPEATS)

typedef struct {
    uint32_t sample_rate;
    uint32_t pcm_bytes;
    uint32_t data_offset;
    uint16_t channels;
    uint16_t bits_per_sample;
} wav_file_info_t;

static const int16_t s_sine_440_lut[SINE_440_LUT_COUNT] = {
     0,  1423,  2802,  4096,  5266,  6275,  7094,
  7698,  8068,  8192,  8068,  7698,  7094,  6275,
  5266,  4096,  2802,  1423,     0, -1423, -2802,
 -4096, -5266, -6275, -7094, -7698, -8068, -8192,
 -8068, -7698, -7094, -6275, -5266, -4096, -2802,
 -1423
};

static esp_codec_dev_handle_t s_speaker_dev = NULL;
static bool s_ready = false;

static volatile bool s_should_play_test = false;
static volatile bool s_wav_playing = false;
static TaskHandle_t s_test_task_handle = NULL;
static TaskHandle_t s_stop_waiter_handle = NULL;

static uint16_t read_le16(const uint8_t *src)
{
    return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

static uint32_t read_le32(const uint8_t *src)
{
    return (uint32_t)src[0] |
        ((uint32_t)src[1] << 8) |
        ((uint32_t)src[2] << 16) |
        ((uint32_t)src[3] << 24);
}

static uint32_t bytes_to_duration_ms(
    uint32_t pcm_bytes,
    uint32_t sample_rate,
    uint16_t channels,
    uint16_t bits_per_sample
)
{
    uint32_t bytes_per_second =
        sample_rate * (uint32_t)channels * (uint32_t)bits_per_sample / 8U;

    if (bytes_per_second == 0) {
        return 0;
    }

    return (uint32_t)(((uint64_t)pcm_bytes * 1000ULL) / bytes_per_second);
}

static esp_err_t set_power_amplifier_enabled(bool enabled)
{
    static bool configured = false;

    if (!configured) {
        gpio_config_t io_conf = {
            .pin_bit_mask = 1ULL << AUDIO_PLAYBACK_PA_CTRL_GPIO,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };

        esp_err_t err = gpio_config(&io_conf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure PA_CTRL GPIO: %s", esp_err_to_name(err));
            return err;
        }

        configured = true;
    }

    esp_err_t err = gpio_set_level(AUDIO_PLAYBACK_PA_CTRL_GPIO, enabled ? 1 : 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set PA_CTRL GPIO: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Power amplifier %s", enabled ? "enabled" : "disabled");
    return ESP_OK;
}

static void build_sine_chunk(int16_t *buffer, size_t sample_count)
{
    for (size_t i = 0; i < sample_count; i++) {
        buffer[i] = s_sine_440_lut[i % SINE_440_LUT_COUNT];
    }
}

static esp_err_t open_speaker_codec(
    uint32_t sample_rate,
    uint16_t channels,
    uint16_t bits_per_sample
)
{
    esp_err_t err = set_power_amplifier_enabled(true);
    if (err != ESP_OK) {
        return err;
    }

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = sample_rate,
        .channel = channels,
        .bits_per_sample = bits_per_sample,
    };

    int codec_ret = esp_codec_dev_open(s_speaker_dev, &fs);
    if (codec_ret != 0) {
        ESP_LOGE(TAG, "Failed to open speaker codec: %d", codec_ret);
        set_power_amplifier_enabled(false);
        return ESP_FAIL;
    }

    codec_ret = esp_codec_dev_set_out_vol(s_speaker_dev, AUDIO_PLAYBACK_VOLUME);
    if (codec_ret != 0) {
        ESP_LOGW(TAG, "Failed to set speaker volume: %d", codec_ret);
    }

    return ESP_OK;
}

static void close_speaker_codec(void)
{
    int codec_ret = esp_codec_dev_close(s_speaker_dev);
    if (codec_ret != 0) {
        ESP_LOGW(TAG, "Failed to close speaker codec cleanly: %d", codec_ret);
    }

    set_power_amplifier_enabled(false);
}

static esp_err_t read_wav_info(FILE *file, wav_file_info_t *out_info)
{
    if (file == NULL || out_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t header[AUDIO_PLAYBACK_WAV_HEADER_SIZE];

    if (fseek(file, 0, SEEK_SET) != 0) {
        return ESP_FAIL;
    }

    if (fread(header, 1, sizeof(header), file) != sizeof(header)) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (
        memcmp(&header[0], "RIFF", 4) != 0 ||
        memcmp(&header[8], "WAVE", 4) != 0 ||
        memcmp(&header[12], "fmt ", 4) != 0 ||
        memcmp(&header[36], "data", 4) != 0
    ) {
        ESP_LOGE(TAG, "Unsupported WAV header");
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint16_t audio_format = read_le16(&header[20]);
    uint16_t channels = read_le16(&header[22]);
    uint32_t sample_rate = read_le32(&header[24]);
    uint16_t bits_per_sample = read_le16(&header[34]);
    uint32_t pcm_bytes = read_le32(&header[40]);

    if (audio_format != 1) {
        ESP_LOGE(TAG, "Unsupported WAV format: %u", (unsigned int)audio_format);
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (bits_per_sample != 16) {
        ESP_LOGE(TAG, "Unsupported bit depth: %u", (unsigned int)bits_per_sample);
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (channels != 1 && channels != 2) {
        ESP_LOGE(TAG, "Unsupported channel count: %u", (unsigned int)channels);
        return ESP_ERR_NOT_SUPPORTED;
    }

    memset(out_info, 0, sizeof(*out_info));
    out_info->sample_rate = sample_rate;
    out_info->channels = channels;
    out_info->bits_per_sample = bits_per_sample;
    out_info->pcm_bytes = pcm_bytes;
    out_info->data_offset = AUDIO_PLAYBACK_WAV_HEADER_SIZE;

    return ESP_OK;
}

bool audio_playback_is_test_tone_playing(void)
{
    return s_test_task_handle != NULL;
}

bool audio_playback_is_busy(void)
{
    return s_test_task_handle != NULL || s_wav_playing;
}

esp_err_t audio_playback_init(void)
{
    if (s_ready && s_speaker_dev != NULL) {
        return ESP_OK;
    }

    /*
     * audio_recorder_init() already calls bsp_audio_init() for the shared I2S
     * bus during app startup. Here we only create the speaker codec device.
     */
    s_speaker_dev = bsp_audio_codec_speaker_init();
    if (s_speaker_dev == NULL) {
        ESP_LOGE(TAG, "Failed to initialize speaker codec");
        return ESP_FAIL;
    }

    esp_err_t err = set_power_amplifier_enabled(false);
    if (err != ESP_OK) {
        return err;
    }

    s_ready = true;
    return ESP_OK;
}

static void sine_task(void *arg)
{
    (void)arg;

    int16_t buffer[SINE_440_CHUNK_SAMPLES];
    build_sine_chunk(buffer, SINE_440_CHUNK_SAMPLES);

    esp_err_t err = open_speaker_codec(
        AUDIO_PLAYBACK_TEST_SAMPLE_RATE,
        AUDIO_PLAYBACK_TEST_CHANNELS,
        AUDIO_PLAYBACK_TEST_BITS_PER_SAMPLE
    );

    if (err != ESP_OK) {
        s_should_play_test = false;
    } else {
        ESP_LOGI(TAG, "Started 440 Hz sine playback test");
    }

    while (s_should_play_test) {
        int codec_ret = esp_codec_dev_write(s_speaker_dev, buffer, sizeof(buffer));
        if (codec_ret != 0) {
            ESP_LOGE(TAG, "Speaker write failed: %d", codec_ret);
            break;
        }
    }

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Stopped sine playback test");
        close_speaker_codec();
    }

    TaskHandle_t waiter = s_stop_waiter_handle;
    s_stop_waiter_handle = NULL;
    s_should_play_test = false;
    s_test_task_handle = NULL;

    if (waiter != NULL) {
        xTaskNotifyGive(waiter);
    }

    vTaskDelete(NULL);
}

esp_err_t audio_playback_start_sine_440(void)
{
    if (audio_playback_is_busy()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = audio_playback_init();
    if (err != ESP_OK) {
        return err;
    }

    s_should_play_test = true;
    s_stop_waiter_handle = NULL;

    BaseType_t ok = xTaskCreate(
        sine_task,
        "sine_play",
        AUDIO_PLAYBACK_TASK_STACK_SIZE,
        NULL,
        AUDIO_PLAYBACK_TASK_PRIORITY,
        &s_test_task_handle
    );

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sine playback task");
        s_should_play_test = false;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t audio_playback_stop_sine_440(void)
{
    if (s_test_task_handle == NULL) {
        return ESP_OK;
    }

    s_stop_waiter_handle = xTaskGetCurrentTaskHandle();
    s_should_play_test = false;

    uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1500));
    if (notified == 0) {
        ESP_LOGE(TAG, "Timed out while stopping sine playback");
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t audio_playback_play_wav_file(
    const char *path,
    audio_playback_result_t *out_result
)
{
    if (path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (audio_playback_is_busy()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = audio_playback_init();
    if (err != ESP_OK) {
        return err;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open %s for playback", path);
        return ESP_ERR_NOT_FOUND;
    }

    wav_file_info_t wav = {0};
    err = read_wav_info(file, &wav);
    if (err != ESP_OK) {
        fclose(file);
        return err;
    }

    if (fseek(file, (long)wav.data_offset, SEEK_SET) != 0) {
        fclose(file);
        return ESP_FAIL;
    }

    err = open_speaker_codec(wav.sample_rate, wav.channels, wav.bits_per_sample);
    if (err != ESP_OK) {
        fclose(file);
        return err;
    }

    ESP_LOGI(TAG, "Playing WAV file: %s", path);

    s_wav_playing = true;

    uint8_t *buffer = (uint8_t *)malloc(AUDIO_PLAYBACK_BUFFER_SIZE);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate WAV playback buffer");
        close_speaker_codec();
        s_wav_playing = false;
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    uint32_t remaining = wav.pcm_bytes;
    esp_err_t play_result = ESP_OK;

    while (remaining > 0) {
        size_t to_read = remaining > AUDIO_PLAYBACK_BUFFER_SIZE ?
            AUDIO_PLAYBACK_BUFFER_SIZE :
            (size_t)remaining;
        size_t bytes_read = fread(buffer, 1, to_read, file);

        if (bytes_read == 0) {
            if (ferror(file)) {
                ESP_LOGE(TAG, "Failed while reading WAV data");
                play_result = ESP_FAIL;
            }
            break;
        }

        int codec_ret = esp_codec_dev_write(s_speaker_dev, buffer, bytes_read);
        if (codec_ret != 0) {
            ESP_LOGE(TAG, "Speaker write failed: %d", codec_ret);
            play_result = ESP_FAIL;
            break;
        }

        remaining -= (uint32_t)bytes_read;
    }

    free(buffer);

    close_speaker_codec();
    s_wav_playing = false;

    struct stat st = {0};
    uint32_t wav_bytes = wav.pcm_bytes + AUDIO_PLAYBACK_WAV_HEADER_SIZE;
    if (stat(path, &st) == 0 && st.st_size > 0) {
        wav_bytes = (uint32_t)st.st_size;
    }

    if (out_result != NULL) {
        memset(out_result, 0, sizeof(*out_result));
        strlcpy(out_result->path, path, sizeof(out_result->path));
        out_result->duration_ms = bytes_to_duration_ms(
            wav.pcm_bytes,
            wav.sample_rate,
            wav.channels,
            wav.bits_per_sample
        );
        out_result->pcm_bytes = wav.pcm_bytes;
        out_result->wav_bytes = wav_bytes;
        out_result->sample_rate = wav.sample_rate;
        out_result->channels = wav.channels;
        out_result->bits_per_sample = wav.bits_per_sample;
    }

    fclose(file);

    if (play_result == ESP_OK) {
        ESP_LOGI(TAG, "WAV playback done: %s", path);
    }

    return play_result;
}
