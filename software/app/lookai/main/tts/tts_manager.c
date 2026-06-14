/**
 * @file tts/tts_manager.c
 * @brief Text-to-speech manager implementation.
 */

#include "tts_manager.h"

#include "audio_playback.h"
#include "esp_log.h"
#include "tts_api_client.h"
#include "runtime_diag.h"
#include "ui_manager.h"
#include "wifi_manager.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "tts_manager";

#define TTS_EVENT_QUEUE_LEN 4
#define TTS_MANAGER_TASK_STACK_SIZE 4096
#define TTS_GENERATE_TASK_STACK_SIZE 12288
#define TTS_GENERATE_TASK_PRIORITY 4
#define TTS_GENERATE_START_DELAY_MS 400
#define TTS_POST_GENERATE_DELAY_MS 250
#define TTS_PRE_PLAY_DELAY_MS 250
#define TTS_TEXT_BUFFER_SIZE 224
#define TTS_RESULT_BUFFER_SIZE 1024
#define TTS_OUTPUT_WAV_PATH "/spiffs/tts_last.wav"

typedef enum {
    TTS_MANAGER_EVENT_SAMPLE_1 = 0,
    TTS_MANAGER_EVENT_SAMPLE_2,
    TTS_MANAGER_EVENT_GENERATE_DONE,
} tts_manager_event_t;

typedef enum {
    TTS_MANAGER_STATE_READY = 0,
    TTS_MANAGER_STATE_GENERATING,
    TTS_MANAGER_STATE_PLAYING,
    TTS_MANAGER_STATE_ERROR,
} tts_manager_state_t;

static QueueHandle_t s_tts_event_queue = NULL;
static TaskHandle_t s_tts_task_handle = NULL;
static TaskHandle_t s_generate_task_handle = NULL;

static tts_manager_state_t s_state = TTS_MANAGER_STATE_READY;
static char s_pending_text[TTS_TEXT_BUFFER_SIZE] = {0};
static char s_generate_error[TTS_RESULT_BUFFER_SIZE] = {0};
static uint32_t s_generated_wav_bytes = 0;
static bool s_generate_success = false;

static void update_tts_ui(const char *status, const char *result, bool busy)
{
    ui_manager_update_tts_status(
        status,
        result,
        busy
    );
}

static void set_error_message(const char *message)
{
    s_state = TTS_MANAGER_STATE_ERROR;
    update_tts_ui(
        "TTS error",
        message != NULL ? message : "Unknown TTS error.",
        false
    );
}

static void post_event(tts_manager_event_t event)
{
    if (s_tts_event_queue == NULL) {
        return;
    }

    xQueueSend(s_tts_event_queue, &event, 0);
}

void tts_manager_speak_sample_1(void)
{
    post_event(TTS_MANAGER_EVENT_SAMPLE_1);
}

void tts_manager_speak_sample_2(void)
{
    post_event(TTS_MANAGER_EVENT_SAMPLE_2);
}

static const char *text_for_event(tts_manager_event_t event)
{
    switch (event) {
        case TTS_MANAGER_EVENT_SAMPLE_1:
            return "Merhaba! Ben LookAI.";

        case TTS_MANAGER_EVENT_SAMPLE_2:
            return "Sana nasıl yardımcı olabilirim?";

        default:
            return "";
    }
}

static void generate_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Starting TTS generation");

    uint32_t wav_bytes = 0;
    runtime_diag_log("tts_manager_before_api");
    esp_err_t err = tts_api_client_generate_wav(
        s_pending_text,
        TTS_OUTPUT_WAV_PATH,
        &wav_bytes
    );
    runtime_diag_log("tts_manager_after_api");

    if (err == ESP_OK) {
        s_generate_success = true;
        s_generated_wav_bytes = wav_bytes;
        s_generate_error[0] = '\0';
    } else {
        const char *api_error = tts_api_client_get_last_error();

        s_generate_success = false;
        s_generated_wav_bytes = 0;
        snprintf(
            s_generate_error,
            sizeof(s_generate_error),
            "%s",
            api_error != NULL ? api_error : "TTS generation failed."
        );

        ESP_LOGE(TAG, "TTS generation failed: %s", esp_err_to_name(err));
    }

    s_generate_task_handle = NULL;
    post_event(TTS_MANAGER_EVENT_GENERATE_DONE);

    vTaskDelete(NULL);
}

static void handle_generate_done(void)
{
    if (s_state != TTS_MANAGER_STATE_GENERATING) {
        ESP_LOGI(TAG, "Ignoring TTS generation result because state changed");
        return;
    }

    /*
     * Let the worker task delete itself and give the system a moment to return
     * its TLS stack/heap pressure before LVGL tries to redraw and before audio
     * playback starts.
     */
    vTaskDelay(pdMS_TO_TICKS(TTS_POST_GENERATE_DELAY_MS));

    if (!s_generate_success) {
        set_error_message(
            s_generate_error[0] != '\0' ?
                s_generate_error :
                "TTS generation failed."
        );
        return;
    }

    char details[TTS_RESULT_BUFFER_SIZE];
    float kb = (float)s_generated_wav_bytes / 1024.0f;

    snprintf(
        details,
        sizeof(details),
        "File: %s\nSize: %.1f KB\nText: %s",
        TTS_OUTPUT_WAV_PATH,
        kb,
        s_pending_text
    );

    s_state = TTS_MANAGER_STATE_PLAYING;
    update_tts_ui(
        "Playing",
        details,
        true
    );

    /*
     * Give the display a short window to draw the Playing state while the
     * heavyweight HTTP/TLS task is already gone.
     */
    vTaskDelay(pdMS_TO_TICKS(TTS_PRE_PLAY_DELAY_MS));

    audio_playback_result_t playback = {0};
    runtime_diag_log("tts_manager_before_playback");
    esp_err_t err = audio_playback_play_wav_file(TTS_OUTPUT_WAV_PATH, &playback);
    runtime_diag_log("tts_manager_after_playback");

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TTS playback failed: %s", esp_err_to_name(err));
        set_error_message("Could not play generated speech.");
        return;
    }

    s_state = TTS_MANAGER_STATE_READY;
    update_tts_ui(
        "Ready",
        "Select a sample text.",
        false
    );
}

static void handle_speak_request(const char *text)
{
    if (text == NULL || text[0] == '\0') {
        set_error_message("TTS text is empty.");
        return;
    }

    if (
        s_state == TTS_MANAGER_STATE_GENERATING ||
        s_state == TTS_MANAGER_STATE_PLAYING ||
        s_generate_task_handle != NULL
    ) {
        ESP_LOGI(TAG, "Ignoring TTS request while busy");
        return;
    }

    if (!wifi_manager_is_connected()) {
        set_error_message("Connect to Wi-Fi first.");
        return;
    }

    snprintf(s_pending_text, sizeof(s_pending_text), "%s", text);
    s_generate_error[0] = '\0';
    s_generated_wav_bytes = 0;
    s_generate_success = false;

    s_state = TTS_MANAGER_STATE_GENERATING;
    update_tts_ui(
        "Generating",
        s_pending_text,
        true
    );

    /*
     * Let LVGL finish drawing the Generating state before the HTTP/TLS task
     * starts allocating memory.
     */
    vTaskDelay(pdMS_TO_TICKS(TTS_GENERATE_START_DELAY_MS));

    BaseType_t ok = xTaskCreate(
        generate_task,
        "tts_generate",
        TTS_GENERATE_TASK_STACK_SIZE,
        NULL,
        TTS_GENERATE_TASK_PRIORITY,
        &s_generate_task_handle
    );

    if (ok != pdPASS) {
        s_generate_task_handle = NULL;
        s_state = TTS_MANAGER_STATE_ERROR;
        set_error_message("Could not start TTS task.");
    }
}

static void tts_task(void *arg)
{
    (void)arg;

    tts_manager_event_t event;

    update_tts_ui(
        "Ready",
        "Select a sample text.",
        false
    );

    while (true) {
        if (xQueueReceive(s_tts_event_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (event) {
            case TTS_MANAGER_EVENT_SAMPLE_1:
            case TTS_MANAGER_EVENT_SAMPLE_2:
                handle_speak_request(text_for_event(event));
                break;

            case TTS_MANAGER_EVENT_GENERATE_DONE:
                handle_generate_done();
                break;

            default:
                break;
        }
    }
}

esp_err_t tts_manager_start(void)
{
    if (s_tts_event_queue != NULL) {
        return ESP_OK;
    }

    s_tts_event_queue = xQueueCreate(TTS_EVENT_QUEUE_LEN, sizeof(tts_manager_event_t));
    if (s_tts_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create TTS event queue");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreate(
        tts_task,
        "tts_task",
        TTS_MANAGER_TASK_STACK_SIZE,
        NULL,
        5,
        &s_tts_task_handle
    );

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create TTS task");
        vQueueDelete(s_tts_event_queue);
        s_tts_event_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "TTS manager started");

    return ESP_OK;
}
