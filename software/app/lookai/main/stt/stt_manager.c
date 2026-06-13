/**
 * @file stt/stt_manager.c
 * @brief Speech-to-text backend state machine implementation.
 */

#include "stt_manager.h"

#include "audio_playback.h"
#include "audio_recorder.h"
#include "esp_log.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "ui_manager.h"

static const char *TAG = "stt_manager";

#define STT_EVENT_QUEUE_LEN 8
#define STT_TASK_STACK_SIZE 10240

typedef enum {
    STT_MANAGER_EVENT_PRESS = 0,
    STT_MANAGER_EVENT_RELEASE,
    STT_MANAGER_EVENT_TOGGLE_SPEAKER_TEST,
    STT_MANAGER_EVENT_PLAY_RECORDING,
} stt_manager_event_t;

typedef enum {
    STT_MANAGER_STATE_READY = 0,
    STT_MANAGER_STATE_RECORDING,
    STT_MANAGER_STATE_PROCESSING,
    STT_MANAGER_STATE_SPEAKER_TEST,
    STT_MANAGER_STATE_PLAYING_RECORDING,
    STT_MANAGER_STATE_DONE,
    STT_MANAGER_STATE_ERROR,
} stt_manager_state_t;

static QueueHandle_t s_stt_event_queue = NULL;
static TaskHandle_t s_stt_task_handle = NULL;
static stt_manager_state_t s_state = STT_MANAGER_STATE_READY;

static void update_stt_ui(
    const char *status,
    const char *result,
    bool recording,
    bool processing,
    bool speaker_test_active,
    bool recording_playback_active
)
{
    ui_manager_update_stt_status(
        status,
        result,
        recording,
        processing,
        speaker_test_active,
        recording_playback_active
    );
}

static void post_event(stt_manager_event_t event)
{
    if (s_stt_event_queue == NULL) {
        return;
    }

    xQueueSend(s_stt_event_queue, &event, 0);
}

void stt_manager_press(void)
{
    post_event(STT_MANAGER_EVENT_PRESS);
}

void stt_manager_release(void)
{
    post_event(STT_MANAGER_EVENT_RELEASE);
}

void stt_manager_toggle_speaker_test(void)
{
    post_event(STT_MANAGER_EVENT_TOGGLE_SPEAKER_TEST);
}

void stt_manager_play_recording(void)
{
    post_event(STT_MANAGER_EVENT_PLAY_RECORDING);
}

static void set_error_status(const char *message, esp_err_t err)
{
    char result[192];

    snprintf(
        result,
        sizeof(result),
        "%s\n%s",
        message != NULL ? message : "Audio error",
        esp_err_to_name(err)
    );

    s_state = STT_MANAGER_STATE_ERROR;
    update_stt_ui("Audio error", result, false, false, false, false);
}

static void handle_press(void)
{
    if (
        s_state == STT_MANAGER_STATE_PROCESSING ||
        s_state == STT_MANAGER_STATE_SPEAKER_TEST ||
        s_state == STT_MANAGER_STATE_PLAYING_RECORDING
    ) {
        ESP_LOGI(TAG, "Ignoring TALK press while STT/audio is busy");
        return;
    }

    ESP_LOGI(TAG, "TALK pressed");

    /*
     * Start the real recorder, but do not repaint the STT screen from here.
     * The UI updates "Recording..." locally while the user is still holding
     * the same LVGL button object. Re-rendering on press can cause LVGL to lose
     * the matching RELEASED event.
     */
    esp_err_t err = audio_recorder_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start audio recorder: %s", esp_err_to_name(err));
        set_error_status("Could not start recording.", err);
        return;
    }

    s_state = STT_MANAGER_STATE_RECORDING;
}

static void handle_release(void)
{
    if (s_state != STT_MANAGER_STATE_RECORDING) {
        ESP_LOGI(TAG, "Ignoring release because STT is not recording");
        return;
    }

    ESP_LOGI(TAG, "TALK released");

    s_state = STT_MANAGER_STATE_PROCESSING;
    update_stt_ui("Saving...", "", false, true, false, false);

    audio_recorder_result_t result = {0};
    esp_err_t err = audio_recorder_stop(&result);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop audio recorder: %s", esp_err_to_name(err));
        set_error_status("Could not save recording.", err);
        return;
    }

    float seconds = (float)result.duration_ms / 1000.0f;
    float kb = (float)result.wav_bytes / 1024.0f;

    char details[256];
    snprintf(
        details,
        sizeof(details),
        "Saved recording\nLength: %.1f sec\nSize: %.1f KB",
        seconds,
        kb
    );

    s_state = STT_MANAGER_STATE_DONE;
    update_stt_ui(
        "Recording saved",
        details,
        false,
        false,
        false,
        false
    );
}

static void handle_toggle_speaker_test(void)
{
    if (s_state == STT_MANAGER_STATE_RECORDING) {
        ESP_LOGI(TAG, "Ignoring speaker test while recording");
        return;
    }

    if (s_state == STT_MANAGER_STATE_PROCESSING || s_state == STT_MANAGER_STATE_PLAYING_RECORDING) {
        ESP_LOGI(TAG, "Ignoring speaker test while busy");
        return;
    }

    if (s_state == STT_MANAGER_STATE_SPEAKER_TEST) {
        ESP_LOGI(TAG, "Stopping 440 Hz speaker test");

        update_stt_ui(
            "Stopping speaker...",
            "Stopping 440 Hz test tone.",
            false,
            true,
            true,
            false
        );

        esp_err_t err = audio_playback_stop_sine_440();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop speaker test: %s", esp_err_to_name(err));
            set_error_status("Could not stop speaker test.", err);
            return;
        }

        s_state = STT_MANAGER_STATE_DONE;
        update_stt_ui(
            "Speaker test stopped",
            "440 Hz test tone stopped.",
            false,
            false,
            false,
            false
        );
        return;
    }

    ESP_LOGI(TAG, "Starting 440 Hz speaker test");

    esp_err_t err = audio_playback_start_sine_440();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start speaker test: %s", esp_err_to_name(err));
        set_error_status("Could not start speaker test.", err);
        return;
    }

    s_state = STT_MANAGER_STATE_SPEAKER_TEST;
    update_stt_ui(
        "Testing speaker...",
        "440 Hz test tone is playing.",
        false,
        false,
        true,
        false
    );
}

static void handle_play_recording(void)
{
    if (s_state == STT_MANAGER_STATE_RECORDING) {
        ESP_LOGI(TAG, "Ignoring recording playback while recording");
        return;
    }

    if (
        s_state == STT_MANAGER_STATE_PROCESSING ||
        s_state == STT_MANAGER_STATE_SPEAKER_TEST ||
        s_state == STT_MANAGER_STATE_PLAYING_RECORDING
    ) {
        ESP_LOGI(TAG, "Ignoring recording playback while busy");
        return;
    }

    const char *path = audio_recorder_get_path();

    ESP_LOGI(TAG, "Playing saved recording: %s", path);

    s_state = STT_MANAGER_STATE_PLAYING_RECORDING;
    update_stt_ui(
        "Playing recording...",
        "Playing saved microphone recording.",
        false,
        false,
        false,
        true
    );

    audio_playback_result_t result = {0};
    esp_err_t err = audio_playback_play_wav_file(path, &result);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to play recording: %s", esp_err_to_name(err));
        set_error_status("Could not play recording.", err);
        return;
    }

    float seconds = (float)result.duration_ms / 1000.0f;
    float kb = (float)result.wav_bytes / 1024.0f;

    char details[256];
    snprintf(
        details,
        sizeof(details),
        "Played recording\nLength: %.1f sec\nSize: %.1f KB",
        seconds,
        kb
    );

    s_state = STT_MANAGER_STATE_DONE;
    update_stt_ui(
        "Playback done",
        details,
        false,
        false,
        false,
        false
    );
}

static void stt_task(void *arg)
{
    (void)arg;

    stt_manager_event_t event;

    update_stt_ui("Ready", "", false, false, false, false);

    while (true) {
        if (xQueueReceive(s_stt_event_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (event) {
            case STT_MANAGER_EVENT_PRESS:
                handle_press();
                break;

            case STT_MANAGER_EVENT_RELEASE:
                handle_release();
                break;

            case STT_MANAGER_EVENT_TOGGLE_SPEAKER_TEST:
                handle_toggle_speaker_test();
                break;

            case STT_MANAGER_EVENT_PLAY_RECORDING:
                handle_play_recording();
                break;

            default:
                break;
        }
    }
}

esp_err_t stt_manager_start(void)
{
    if (s_stt_event_queue != NULL) {
        return ESP_OK;
    }

    s_stt_event_queue = xQueueCreate(STT_EVENT_QUEUE_LEN, sizeof(stt_manager_event_t));
    if (s_stt_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create STT event queue");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreate(
        stt_task,
        "stt_task",
        STT_TASK_STACK_SIZE,
        NULL,
        5,
        &s_stt_task_handle
    );

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create STT task");
        vQueueDelete(s_stt_event_queue);
        s_stt_event_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "STT manager started");

    return ESP_OK;
}
