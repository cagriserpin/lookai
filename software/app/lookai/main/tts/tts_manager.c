/**
 * @file tts/tts_manager.c
 * @brief Text-to-speech manager implementation.
 */

#include "tts_manager.h"

#include "audio_playback.h"
#include "esp_log.h"
#include "esp_timer.h"
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
#define TTS_MANAGER_TASK_STACK_SIZE 5120
#define TTS_GENERATE_TASK_STACK_SIZE 8192
#define TTS_GENERATE_TASK_PRIORITY 4
#define TTS_GENERATE_START_DELAY_MS 100
#define TTS_POST_GENERATE_DELAY_MS 100
#define TTS_PRE_PLAY_DELAY_MS 100
#define TTS_TEXT_BUFFER_SIZE 224
#define TTS_RESULT_BUFFER_SIZE 1024
#define TTS_WAV_PATH "/spiffs/tts_last.wav"

typedef enum {
    TTS_MANAGER_EVENT_SAMPLE_1 = 0,
    TTS_MANAGER_EVENT_SAMPLE_2,
    TTS_MANAGER_EVENT_STREAM_PLAYBACK_STARTED,
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
static uint32_t s_generated_audio_bytes = 0;
static audio_playback_result_t s_stream_playback_result = {0};
static bool s_generate_success = false;

static int64_t s_tts_flow_start_ms = 0;
static int64_t s_tts_api_start_ms = 0;
static int64_t s_tts_api_done_ms = 0;
static int64_t s_tts_playback_start_ms = 0;

static int64_t timing_now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static int64_t timing_since_ms(int64_t start_ms)
{
    if (start_ms <= 0) {
        return -1;
    }

    return timing_now_ms() - start_ms;
}

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

static void tts_stream_playback_started_callback(void *user_ctx)
{
    (void)user_ctx;
    post_event(TTS_MANAGER_EVENT_STREAM_PLAYBACK_STARTED);
}

static void generate_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Starting TTS generation");
    s_tts_api_start_ms = timing_now_ms();
    ESP_LOGI(
        TAG,
        "TIMING TTS api_task_start since_request_ms=%lld",
        (long long)timing_since_ms(s_tts_flow_start_ms)
    );

    uint32_t audio_bytes = 0;
    audio_playback_result_t playback = {0};
    tts_api_client_stream_callbacks_t stream_callbacks = {
        .on_playback_started = tts_stream_playback_started_callback,
        .user_ctx = NULL,
    };

    runtime_diag_log("tts_manager_before_api");
#if CONFIG_LOOKAI_TTS_RESPONSE_FORMAT_PCM
    esp_err_t err = tts_api_client_generate_pcm_streaming(
        s_pending_text,
        &audio_bytes,
        &playback,
        &stream_callbacks
    );
    const char *format_name = "pcm";
#elif CONFIG_LOOKAI_TTS_RESPONSE_FORMAT_WAV
    esp_err_t err = tts_api_client_generate_wav(
        s_pending_text,
        TTS_WAV_PATH,
        &audio_bytes
    );
    const char *format_name = "wav";
    if (err == ESP_OK) {
        if (stream_callbacks.on_playback_started != NULL) {
            stream_callbacks.on_playback_started(stream_callbacks.user_ctx);
        }
        err = audio_playback_play_wav_file(TTS_WAV_PATH, &playback);
    }
#else
#error "No LookAI TTS response format selected."
#endif
    runtime_diag_log("tts_manager_after_api");
    s_tts_api_done_ms = timing_now_ms();
    ESP_LOGI(
        TAG,
        "TIMING TTS api_done format=%s api_ms=%lld total_ms=%lld audio_bytes=%lu played_ms=%lu result=%s",
        format_name,
        (long long)(s_tts_api_start_ms > 0 ? s_tts_api_done_ms - s_tts_api_start_ms : -1),
        (long long)timing_since_ms(s_tts_flow_start_ms),
        (unsigned long)audio_bytes,
        (unsigned long)playback.duration_ms,
        esp_err_to_name(err)
    );

    if (err == ESP_OK) {
        s_generate_success = true;
        s_generated_audio_bytes = audio_bytes;
        s_stream_playback_result = playback;
        s_generate_error[0] = '\0';
    } else {
        const char *api_error = tts_api_client_get_last_error();

        s_generate_success = false;
        s_generated_audio_bytes = 0;
        memset(&s_stream_playback_result, 0, sizeof(s_stream_playback_result));
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

static void handle_stream_playback_started(void)
{
    if (s_state != TTS_MANAGER_STATE_GENERATING) {
        return;
    }

    s_state = TTS_MANAGER_STATE_PLAYING;
    s_tts_playback_start_ms = timing_now_ms();

    ESP_LOGI(
        TAG,
        "TIMING TTS stream_playback_started total_ms=%lld",
        (long long)timing_since_ms(s_tts_flow_start_ms)
    );

    char details[TTS_RESULT_BUFFER_SIZE];
    snprintf(
        details,
        sizeof(details),
        "Playing audio...\nText: %s",
        s_pending_text
    );

    update_tts_ui(
        "Playing",
        details,
        true
    );
}

static void handle_generate_done(void)
{
    if (s_state != TTS_MANAGER_STATE_GENERATING && s_state != TTS_MANAGER_STATE_PLAYING) {
        ESP_LOGI(TAG, "Ignoring TTS generation result because state changed");
        return;
    }

    ESP_LOGI(
        TAG,
        "TIMING TTS generate_done_event since_api_done_ms=%lld total_ms=%lld success=%d",
        (long long)timing_since_ms(s_tts_api_done_ms),
        (long long)timing_since_ms(s_tts_flow_start_ms),
        s_generate_success ? 1 : 0
    );

    if (!s_generate_success) {
        set_error_message(
            s_generate_error[0] != '\0' ?
                s_generate_error :
                "TTS generation failed."
        );
        return;
    }

    ESP_LOGI(
        TAG,
        "TIMING TTS done played_ms=%lu audio_bytes=%lu total_ms=%lld",
        (unsigned long)s_stream_playback_result.duration_ms,
        (unsigned long)s_generated_audio_bytes,
        (long long)timing_since_ms(s_tts_flow_start_ms)
    );

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

    s_tts_flow_start_ms = timing_now_ms();
    s_tts_api_start_ms = 0;
    s_tts_api_done_ms = 0;
    s_tts_playback_start_ms = 0;

    ESP_LOGI(
        TAG,
        "TIMING TTS request_start text_len=%u total_ms=0",
        (unsigned int)strlen(text)
    );

    snprintf(s_pending_text, sizeof(s_pending_text), "%s", text);
    s_generate_error[0] = '\0';
    s_generated_audio_bytes = 0;
    memset(&s_stream_playback_result, 0, sizeof(s_stream_playback_result));
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
    ESP_LOGI(
        TAG,
        "TIMING TTS before_task_create ui_delay_ms=%d total_ms=%lld",
        TTS_GENERATE_START_DELAY_MS,
        (long long)timing_since_ms(s_tts_flow_start_ms)
    );

    BaseType_t task_ok = xTaskCreate(
        generate_task,
        "tts_generate",
        TTS_GENERATE_TASK_STACK_SIZE,
        NULL,
        TTS_GENERATE_TASK_PRIORITY,
        &s_generate_task_handle
    );

    ESP_LOGI(
        TAG,
        "TIMING TTS after_task_create total_ms=%lld result=%s",
        (long long)timing_since_ms(s_tts_flow_start_ms),
        task_ok == pdPASS ? "pdPASS" : "pdFAIL"
    );

    if (task_ok != pdPASS) {
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

            case TTS_MANAGER_EVENT_STREAM_PLAYBACK_STARTED:
                handle_stream_playback_started();
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

    BaseType_t task_ok = xTaskCreate(
        tts_task,
        "tts_task",
        TTS_MANAGER_TASK_STACK_SIZE,
        NULL,
        5,
        &s_tts_task_handle
    );

    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create TTS task");
        vQueueDelete(s_tts_event_queue);
        s_tts_event_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "TTS manager started");

    return ESP_OK;
}
