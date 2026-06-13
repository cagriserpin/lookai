/**
 * @file audio/audio_playback.c
 * @brief Speaker playback helpers for STT audio tests.
 */

#include "audio_playback.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "driver/gpio.h"
#include "esp_codec_dev.h"
#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "audio_playback";

#define AUDIO_PLAYBACK_SAMPLE_RATE 16000
#define AUDIO_PLAYBACK_CHANNELS 1
#define AUDIO_PLAYBACK_BITS_PER_SAMPLE 16

#define AUDIO_PLAYBACK_PA_CTRL_GPIO GPIO_NUM_46
#define AUDIO_PLAYBACK_VOLUME 80

#define AUDIO_PLAYBACK_TASK_STACK_SIZE 4096
#define AUDIO_PLAYBACK_TASK_PRIORITY 5

#define SINE_440_LUT_COUNT 36
#define SINE_440_CHUNK_REPEATS 20
#define SINE_440_CHUNK_SAMPLES (SINE_440_LUT_COUNT * SINE_440_CHUNK_REPEATS)

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

static volatile bool s_should_play = false;
static TaskHandle_t s_play_task_handle = NULL;
static TaskHandle_t s_stop_waiter_handle = NULL;

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

bool audio_playback_is_playing(void)
{
    return s_play_task_handle != NULL;
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

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = AUDIO_PLAYBACK_SAMPLE_RATE,
        .channel = AUDIO_PLAYBACK_CHANNELS,
        .bits_per_sample = AUDIO_PLAYBACK_BITS_PER_SAMPLE,
    };

    esp_err_t pa_err = set_power_amplifier_enabled(true);
    if (pa_err != ESP_OK) {
        s_should_play = false;
    }

    int codec_ret = 0;

    if (s_should_play) {
        codec_ret = esp_codec_dev_open(s_speaker_dev, &fs);
        if (codec_ret != 0) {
            ESP_LOGE(TAG, "Failed to open speaker codec: %d", codec_ret);
            s_should_play = false;
            set_power_amplifier_enabled(false);
        } else {
            codec_ret = esp_codec_dev_set_out_vol(s_speaker_dev, AUDIO_PLAYBACK_VOLUME);
            if (codec_ret != 0) {
                ESP_LOGW(TAG, "Failed to set speaker volume: %d", codec_ret);
            }

            ESP_LOGI(TAG, "Started 440 Hz sine playback test");
        }
    }

    while (s_should_play) {
        codec_ret = esp_codec_dev_write(s_speaker_dev, buffer, sizeof(buffer));
        if (codec_ret != 0) {
            ESP_LOGE(TAG, "Speaker write failed: %d", codec_ret);
            break;
        }
    }

    if (codec_ret == 0) {
        ESP_LOGI(TAG, "Stopped sine playback test");
    }

    codec_ret = esp_codec_dev_close(s_speaker_dev);
    if (codec_ret != 0) {
        ESP_LOGW(TAG, "Failed to close speaker codec cleanly: %d", codec_ret);
    }

    set_power_amplifier_enabled(false);

    TaskHandle_t waiter = s_stop_waiter_handle;
    s_stop_waiter_handle = NULL;
    s_should_play = false;
    s_play_task_handle = NULL;

    if (waiter != NULL) {
        xTaskNotifyGive(waiter);
    }

    vTaskDelete(NULL);
}

esp_err_t audio_playback_start_sine_440(void)
{
    if (s_play_task_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = audio_playback_init();
    if (err != ESP_OK) {
        return err;
    }

    s_should_play = true;
    s_stop_waiter_handle = NULL;

    BaseType_t ok = xTaskCreate(
        sine_task,
        "sine_play",
        AUDIO_PLAYBACK_TASK_STACK_SIZE,
        NULL,
        AUDIO_PLAYBACK_TASK_PRIORITY,
        &s_play_task_handle
    );

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sine playback task");
        s_should_play = false;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t audio_playback_stop(void)
{
    if (s_play_task_handle == NULL) {
        return ESP_OK;
    }

    s_stop_waiter_handle = xTaskGetCurrentTaskHandle();
    s_should_play = false;

    uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1500));
    if (notified == 0) {
        ESP_LOGE(TAG, "Timed out while stopping sine playback");
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}
