/**
 * @file tts/tts_manager.c
 * @brief Text-to-speech manager implementation.
 */

#include "tts_manager.h"

#include "audio_playback.h"
#include "esp_log.h"
#include "tts_api_client.h"
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
#define TTS_TEXT_BUFFER_SIZE 224
#define TTS_RESULT_BUFFER_SIZE 1024
#define TTS_OUTPUT_WAV_PATH "/spiffs/tts_last.wav"

typedef enum {
    TTS_MANAGER_EVENT_SAMPLE_1 = 0,
    TTS_MANAGER_EVENT_SAMPLE_2,
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
            return "Merhaba! Ben LookAI";

        case TTS_MANAGER_EVENT_SAMPLE_2:
            return "I can speak using Groq TTS.";

        default:
            return "";
    }
}

static void generate_and_play_task(void *arg)
{
    (void)arg;

    char details[TTS_RESULT_BUFFER_SIZE];
    uint32_t wav_bytes = 0;

    ESP_LOGI(TAG, "Starting TTS generation");

    esp_err_t err = tts_api_client_generate_wav(
        s_pending_text,
        TTS_OUTPUT_WAV_PATH,
        &wav_bytes
    );

    if (err != ESP_OK) {
        const char *api_error = tts_api_client_get_last_error();
        ESP_LOGE(TAG, "TTS generation failed: %s", esp_err_to_name(err));
        set_error_message(api_error != NULL ? api_error : "TTS generation failed.");
        s_generate_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    float kb = (float)wav_bytes / 1024.0f;

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

    audio_playback_result_t playback = {0};
    err = audio_playback_play_wav_file(TTS_OUTPUT_WAV_PATH, &playback);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TTS playback failed: %s", esp_err_to_name(err));
        set_error_message("Could not play generated speech.");
        s_generate_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    s_state = TTS_MANAGER_STATE_READY;
    update_tts_ui(
        "Ready",
        "Select a sample text.",
        false
    );

    s_generate_task_handle = NULL;
    vTaskDelete(NULL);
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
        generate_and_play_task,
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

        handle_speak_request(text_for_event(event));
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
