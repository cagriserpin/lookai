/**
 * @file audio/audio_recorder.c
 * @brief Push-to-talk WAV recorder using the board microphone codec.
 */

#include "audio_recorder.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "wav_writer.h"
#include "runtime_diag.h"

static const char *TAG = "audio_recorder";

#define AUDIO_RECORDER_SPIFFS_LABEL "storage"
#if CONFIG_LOOKAI_STT_AUDIO_FORMAT_PCM
#define AUDIO_RECORDER_PATH BSP_SPIFFS_MOUNT_POINT "/stt_last.pcm"
#define AUDIO_RECORDER_CONTAINER_LABEL "PCM"
#elif CONFIG_LOOKAI_STT_AUDIO_FORMAT_WAV
#define AUDIO_RECORDER_PATH BSP_SPIFFS_MOUNT_POINT "/stt_last.wav"
#define AUDIO_RECORDER_CONTAINER_LABEL "WAV"
#else
#error "No LookAI STT audio format selected."
#endif
#define AUDIO_RECORDER_TASK_STACK_SIZE 4096
#define AUDIO_RECORDER_TASK_PRIORITY 5
#define AUDIO_RECORDER_READ_BUFFER_SIZE 1024
#define AUDIO_RECORDER_STOP_TIMEOUT_MS 3000

static esp_codec_dev_handle_t s_mic_dev = NULL;
static bool s_spiffs_mounted = false;
static bool s_codec_ready = false;

static volatile bool s_should_record = false;
static TaskHandle_t s_record_task_handle = NULL;
static TaskHandle_t s_stop_waiter_handle = NULL;

static FILE *s_record_file = NULL;
static uint32_t s_pcm_bytes = 0;
static esp_err_t s_record_result = ESP_OK;
static audio_recorder_result_t s_last_result = {0};

const char *audio_recorder_get_path(void)
{
    return AUDIO_RECORDER_PATH;
}

bool audio_recorder_is_recording(void)
{
    return s_record_task_handle != NULL;
}

static uint32_t bytes_to_duration_ms(uint32_t pcm_bytes)
{
    uint32_t bytes_per_second =
        AUDIO_RECORDER_SAMPLE_RATE *
        AUDIO_RECORDER_CHANNELS *
        AUDIO_RECORDER_BITS_PER_SAMPLE / 8U;

    if (bytes_per_second == 0) {
        return 0;
    }

    return (uint32_t)(((uint64_t)pcm_bytes * 1000ULL) / bytes_per_second);
}

static esp_err_t mount_spiffs_once(void)
{
    if (s_spiffs_mounted) {
        return ESP_OK;
    }

    /*
     * Do not call bsp_spiffs_mount() here.
     *
     * The BSP helper uses ESP_ERROR_CHECK internally, so an unformatted new
     * SPIFFS partition aborts the app before we can recover. For the recorder,
     * mount SPIFFS directly and allow first-boot formatting.
     */
    const esp_vfs_spiffs_conf_t conf = {
        .base_path = BSP_SPIFFS_MOUNT_POINT,
        .partition_label = AUDIO_RECORDER_SPIFFS_LABEL,
        .max_files = 4,
        .format_if_mount_failed = true,
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err == ESP_ERR_INVALID_STATE) {
        /*
         * Treat "already mounted" as success. This lets the recorder coexist
         * with other modules that may mount SPIFFS earlier.
         */
        s_spiffs_mounted = true;
        return ESP_OK;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS: %s", esp_err_to_name(err));
        return err;
    }

    size_t total = 0;
    size_t used = 0;
    err = esp_spiffs_info(AUDIO_RECORDER_SPIFFS_LABEL, &total, &used);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS mounted at %s: total=%u, used=%u",
                 BSP_SPIFFS_MOUNT_POINT,
                 (unsigned int)total,
                 (unsigned int)used);
    } else {
        ESP_LOGW(TAG, "SPIFFS mounted but info failed: %s", esp_err_to_name(err));
    }

    s_spiffs_mounted = true;
    return ESP_OK;
}

static esp_err_t init_codec_once(void)
{
    if (s_codec_ready && s_mic_dev != NULL) {
        return ESP_OK;
    }

    const i2s_std_config_t i2s_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_RECORDER_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_MONO
        ),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_SCLK,
            .ws = BSP_I2S_LCLK,
            .dout = BSP_I2S_DOUT,
            .din = BSP_I2S_DSIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    esp_err_t err = bsp_audio_init(&i2s_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BSP audio: %s", esp_err_to_name(err));
        return err;
    }

    s_mic_dev = bsp_audio_codec_microphone_init();
    if (s_mic_dev == NULL) {
        ESP_LOGE(TAG, "Failed to initialize microphone codec");
        return ESP_FAIL;
    }

    int codec_ret = esp_codec_dev_set_in_gain(s_mic_dev, 30.0f);
    if (codec_ret != 0) {
        ESP_LOGW(TAG, "Failed to set microphone gain: %d", codec_ret);
    }

    s_codec_ready = true;
    return ESP_OK;
}

esp_err_t audio_recorder_init(void)
{
    esp_err_t err = mount_spiffs_once();
    if (err != ESP_OK) {
        return err;
    }

    return init_codec_once();
}

static void fill_result(uint32_t pcm_bytes, uint32_t wav_bytes)
{
    memset(&s_last_result, 0, sizeof(s_last_result));

    strlcpy(s_last_result.path, AUDIO_RECORDER_PATH, sizeof(s_last_result.path));
    s_last_result.duration_ms = bytes_to_duration_ms(pcm_bytes);
    s_last_result.pcm_bytes = pcm_bytes;
    s_last_result.wav_bytes = wav_bytes;
    s_last_result.sample_rate = AUDIO_RECORDER_SAMPLE_RATE;
    s_last_result.channels = AUDIO_RECORDER_CHANNELS;
    s_last_result.bits_per_sample = AUDIO_RECORDER_BITS_PER_SAMPLE;
}

static void close_recording_file(void)
{
    if (s_record_file == NULL) {
        return;
    }

    fflush(s_record_file);

#if CONFIG_LOOKAI_STT_AUDIO_FORMAT_WAV
    if (wav_writer_finalize_header(
            s_record_file,
            AUDIO_RECORDER_SAMPLE_RATE,
            AUDIO_RECORDER_CHANNELS,
            AUDIO_RECORDER_BITS_PER_SAMPLE,
            s_pcm_bytes
        ) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to finalize WAV header");
        s_record_result = ESP_FAIL;
    }
#endif

    fflush(s_record_file);
    fclose(s_record_file);
    s_record_file = NULL;

    struct stat st = {0};
    uint32_t wav_bytes = 0;

    if (stat(AUDIO_RECORDER_PATH, &st) == 0 && st.st_size > 0) {
        wav_bytes = (uint32_t)st.st_size;
    } else {
        wav_bytes = s_pcm_bytes + 44U;
    }

    fill_result(s_pcm_bytes, wav_bytes);
}

static void record_task(void *arg)
{
    (void)arg;

    uint8_t buffer[AUDIO_RECORDER_READ_BUFFER_SIZE];

    while (s_should_record) {
        /*
         * esp_codec_dev_read() returns 0 on success, not the number of bytes
         * read. The requested buffer length is the amount of valid PCM data when
         * the call succeeds.
         */
        int read_ret = esp_codec_dev_read(s_mic_dev, buffer, sizeof(buffer));
        if (read_ret != 0) {
            ESP_LOGE(TAG, "Microphone read failed: %d", read_ret);
            s_record_result = ESP_FAIL;
            break;
        }

        size_t bytes_read = sizeof(buffer);
        size_t written = fwrite(buffer, 1, bytes_read, s_record_file);
        if (written != bytes_read) {
            ESP_LOGE(TAG, "Failed to write microphone data to WAV file");
            s_record_result = ESP_FAIL;
            break;
        }

        s_pcm_bytes += (uint32_t)bytes_read;
    }

    close_recording_file();

    int codec_ret = esp_codec_dev_close(s_mic_dev);
    if (codec_ret != 0) {
        ESP_LOGW(TAG, "Failed to close microphone codec cleanly: %d", codec_ret);
    }

    TaskHandle_t waiter = s_stop_waiter_handle;
    s_stop_waiter_handle = NULL;
    s_should_record = false;
    s_record_task_handle = NULL;

    if (waiter != NULL) {
        xTaskNotifyGive(waiter);
    }

    vTaskDelete(NULL);
}

esp_err_t audio_recorder_start(void)
{
    if (s_record_task_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    runtime_diag_log("audio_recorder_start_begin");

    esp_err_t err = audio_recorder_init();
    if (err != ESP_OK) {
        return err;
    }

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = AUDIO_RECORDER_SAMPLE_RATE,
        .channel = AUDIO_RECORDER_CHANNELS,
        .bits_per_sample = AUDIO_RECORDER_BITS_PER_SAMPLE,
    };

    int codec_ret = esp_codec_dev_open(s_mic_dev, &fs);
    if (codec_ret != 0) {
        ESP_LOGE(TAG, "Failed to open microphone codec: %d", codec_ret);
        return ESP_FAIL;
    }

    s_record_file = fopen(AUDIO_RECORDER_PATH, "wb+");
    if (s_record_file == NULL) {
        ESP_LOGE(TAG, "Failed to open %s for writing", AUDIO_RECORDER_PATH);
        esp_codec_dev_close(s_mic_dev);
        return ESP_FAIL;
    }

#if CONFIG_LOOKAI_STT_AUDIO_FORMAT_WAV
    err = wav_writer_write_placeholder_header(
        s_record_file,
        AUDIO_RECORDER_SAMPLE_RATE,
        AUDIO_RECORDER_CHANNELS,
        AUDIO_RECORDER_BITS_PER_SAMPLE
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write placeholder WAV header");
        fclose(s_record_file);
        s_record_file = NULL;
        esp_codec_dev_close(s_mic_dev);
        return err;
    }
#endif

    s_pcm_bytes = 0;
    s_record_result = ESP_OK;
    memset(&s_last_result, 0, sizeof(s_last_result));
    s_should_record = true;
    s_stop_waiter_handle = NULL;

    BaseType_t ok = xTaskCreate(
        record_task,
        "audio_record",
        AUDIO_RECORDER_TASK_STACK_SIZE,
        NULL,
        AUDIO_RECORDER_TASK_PRIORITY,
        &s_record_task_handle
    );


    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create audio recording task");
        s_should_record = false;
        close_recording_file();
        esp_codec_dev_close(s_mic_dev);
        return ESP_ERR_NO_MEM;
    }

    runtime_diag_log("audio_recorder_start_done");
    ESP_LOGI(TAG, "Recording %s to %s", AUDIO_RECORDER_CONTAINER_LABEL, AUDIO_RECORDER_PATH);
    return ESP_OK;
}

esp_err_t audio_recorder_stop(audio_recorder_result_t *out_result)
{
    if (s_record_task_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    runtime_diag_log("audio_recorder_stop_begin");

    s_stop_waiter_handle = xTaskGetCurrentTaskHandle();
    s_should_record = false;

    uint32_t notified = ulTaskNotifyTake(
        pdTRUE,
        pdMS_TO_TICKS(AUDIO_RECORDER_STOP_TIMEOUT_MS)
    );

    if (notified == 0) {
        ESP_LOGE(TAG, "Timed out while stopping recording task");
        return ESP_ERR_TIMEOUT;
    }

    if (out_result != NULL) {
        *out_result = s_last_result;
    }

    runtime_diag_log("audio_recorder_stop_done");

    ESP_LOGI(
        TAG,
        "Recording saved: %s, format=%s, duration=%" PRIu32 " ms, bytes=%" PRIu32,
        s_last_result.path,
        AUDIO_RECORDER_CONTAINER_LABEL,
        s_last_result.duration_ms,
        s_last_result.wav_bytes
    );

    return s_record_result;
}
