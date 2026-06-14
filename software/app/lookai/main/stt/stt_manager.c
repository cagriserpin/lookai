/**
 * @file stt/stt_manager.c
 * @brief Speech-to-text backend state machine implementation.
 */

#include "stt_manager.h"

#include "audio_playback.h"
#include "audio_recorder.h"
#include "runtime_diag.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "stt_api_client.h"
#include "ui_manager.h"
#include "wifi_manager.h"
#include "sdkconfig.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "stt_manager";

#define STT_EVENT_QUEUE_LEN 8
#define STT_TASK_STACK_SIZE 6144
#define STT_TRANSCRIBE_TASK_STACK_SIZE 12288
#define STT_TRANSCRIBE_TASK_PRIORITY 4
#define STT_TRANSCRIBE_START_DELAY_MS 100
#define STT_TRANSCRIPT_BUFFER_SIZE 256
#define STT_PATH_BUFFER_SIZE 96

typedef enum {
    STT_MANAGER_EVENT_PRESS = 0,
    STT_MANAGER_EVENT_RELEASE,
    STT_MANAGER_EVENT_TOGGLE_SPEAKER_TEST,
    STT_MANAGER_EVENT_PLAY_RECORDING,
    STT_MANAGER_EVENT_TRANSCRIBE_DONE,
} stt_manager_event_t;

typedef enum {
    STT_MANAGER_STATE_READY = 0,
    STT_MANAGER_STATE_RECORDING,
    STT_MANAGER_STATE_PROCESSING,
    STT_MANAGER_STATE_TRANSCRIBING,
    STT_MANAGER_STATE_SPEAKER_TEST,
    STT_MANAGER_STATE_PLAYING_RECORDING,
    STT_MANAGER_STATE_DONE,
    STT_MANAGER_STATE_ERROR,
} stt_manager_state_t;

static QueueHandle_t s_stt_event_queue = NULL;
static TaskHandle_t s_stt_task_handle = NULL;
static TaskHandle_t s_transcribe_task_handle = NULL;

static stt_manager_state_t s_state = STT_MANAGER_STATE_READY;

static char s_transcribe_path[STT_PATH_BUFFER_SIZE] = {0};
static char s_transcribe_result[STT_TRANSCRIPT_BUFFER_SIZE] = {0};
static bool s_transcribe_success = false;

static int64_t s_stt_flow_start_ms = 0;
static int64_t s_stt_release_ms = 0;
static int64_t s_stt_audio_ready_ms = 0;
static int64_t s_stt_api_task_start_ms = 0;

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

/*
 * Stable UI text is restored after temporary Test/Play states finish.
 * Temporary states must not overwrite this cache.
 */
static char s_stable_status[64] = "Ready";
static char s_stable_result[STT_TRANSCRIPT_BUFFER_SIZE] = "Hold TALK to record.";

static void copy_tr_ascii_text(
    const char *input,
    char *output,
    size_t output_size
)
{
    if (output == NULL || output_size == 0) {
        return;
    }

    if (input == NULL) {
        output[0] = '\0';
        return;
    }

    size_t in_i = 0;
    size_t out_i = 0;

    while (input[in_i] != '\0' && out_i + 1 < output_size) {
        unsigned char c0 = (unsigned char)input[in_i];

        if (c0 < 0x80) {
            output[out_i++] = (char)c0;
            in_i++;
            continue;
        }

        unsigned char c1 = (unsigned char)input[in_i + 1];
        char replacement = '?';
        size_t consumed = 1;

        if (c0 == 0xC3) {
            consumed = 2;

            switch (c1) {
                case 0x87: replacement = 'C'; break; /* Ç */
                case 0xA7: replacement = 'c'; break; /* ç */
                case 0x96: replacement = 'O'; break; /* Ö */
                case 0xB6: replacement = 'o'; break; /* ö */
                case 0x9C: replacement = 'U'; break; /* Ü */
                case 0xBC: replacement = 'u'; break; /* ü */
                default: replacement = '?'; break;
            }
        } else if (c0 == 0xC4) {
            consumed = 2;

            switch (c1) {
                case 0x9E: replacement = 'G'; break; /* Ğ */
                case 0x9F: replacement = 'g'; break; /* ğ */
                case 0xB0: replacement = 'I'; break; /* İ */
                case 0xB1: replacement = 'i'; break; /* ı */
                default: replacement = '?'; break;
            }
        } else if (c0 == 0xC5) {
            consumed = 2;

            switch (c1) {
                case 0x9E: replacement = 'S'; break; /* Ş */
                case 0x9F: replacement = 's'; break; /* ş */
                default: replacement = '?'; break;
            }
        } else if ((c0 & 0xE0) == 0xC0) {
            consumed = 2;
        } else if ((c0 & 0xF0) == 0xE0) {
            consumed = 3;
        } else if ((c0 & 0xF8) == 0xF0) {
            consumed = 4;
        }

        output[out_i++] = replacement;
        in_i += consumed;
    }

    output[out_i] = '\0';
}

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

static void show_temporary_status(
    const char *status,
    const char *result,
    bool recording,
    bool processing,
    bool speaker_test_active,
    bool recording_playback_active
)
{
    char display_result[STT_TRANSCRIPT_BUFFER_SIZE];

    copy_tr_ascii_text(
        result != NULL ? result : "",
        display_result,
        sizeof(display_result)
    );

    update_stt_ui(
        status != NULL ? status : "",
        display_result,
        recording,
        processing,
        speaker_test_active,
        recording_playback_active
    );
}

static void show_stable_status(
    const char *status,
    const char *result
)
{
    snprintf(
        s_stable_status,
        sizeof(s_stable_status),
        "%s",
        status != NULL ? status : "Ready"
    );

    copy_tr_ascii_text(
        result != NULL ? result : "",
        s_stable_result,
        sizeof(s_stable_result)
    );

    update_stt_ui(
        s_stable_status,
        s_stable_result,
        false,
        false,
        false,
        false
    );
}

static void restore_stable_status(void)
{
    update_stt_ui(
        s_stable_status,
        s_stable_result,
        false,
        false,
        false,
        false
    );
}

static bool is_busy_for_new_action(void)
{
    return
        s_state == STT_MANAGER_STATE_PROCESSING ||
        s_state == STT_MANAGER_STATE_TRANSCRIBING ||
        s_state == STT_MANAGER_STATE_SPEAKER_TEST ||
        s_state == STT_MANAGER_STATE_PLAYING_RECORDING;
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

static void set_error_message(const char *status, const char *message)
{
    s_state = STT_MANAGER_STATE_ERROR;

    show_stable_status(
        status != NULL ? status : "STT error",
        message != NULL ? message : "Unknown error."
    );
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

    set_error_message("STT error", result);
}

static void transcribe_task(void *arg)
{
    (void)arg;

    char transcript[STT_TRANSCRIPT_BUFFER_SIZE] = {0};

    ESP_LOGI(TAG, "Starting STT transcription task");
    s_stt_api_task_start_ms = timing_now_ms();
    ESP_LOGI(
        TAG,
        "TIMING STT api_task_start since_release_ms=%lld since_audio_ready_ms=%lld total_ms=%lld",
        (long long)(s_stt_release_ms > 0 ? s_stt_api_task_start_ms - s_stt_release_ms : -1),
        (long long)(s_stt_audio_ready_ms > 0 ? s_stt_api_task_start_ms - s_stt_audio_ready_ms : -1),
        (long long)(s_stt_flow_start_ms > 0 ? s_stt_api_task_start_ms - s_stt_flow_start_ms : -1)
    );
    runtime_diag_log("stt_transcribe_task_before_api");

    esp_err_t err = stt_api_client_transcribe_wav(
        s_transcribe_path,
        transcript,
        sizeof(transcript)
    );

    runtime_diag_log("stt_transcribe_task_after_api");
    ESP_LOGI(
        TAG,
        "TIMING STT api_task_done api_ms=%lld total_ms=%lld result=%s",
        (long long)timing_since_ms(s_stt_api_task_start_ms),
        (long long)timing_since_ms(s_stt_flow_start_ms),
        esp_err_to_name(err)
    );

    if (err == ESP_OK) {
        s_transcribe_success = true;
        copy_tr_ascii_text(
            transcript,
            s_transcribe_result,
            sizeof(s_transcribe_result)
        );
    } else {
        const char *api_error = stt_api_client_get_last_error();

        s_transcribe_success = false;
        copy_tr_ascii_text(
            api_error != NULL ? api_error : "Transcription failed.",
            s_transcribe_result,
            sizeof(s_transcribe_result)
        );

        ESP_LOGE(TAG, "STT transcription failed: %s", esp_err_to_name(err));
    }

    s_transcribe_task_handle = NULL;
    post_event(STT_MANAGER_EVENT_TRANSCRIBE_DONE);

    vTaskDelete(NULL);
}

static void handle_press(void)
{
    if (is_busy_for_new_action()) {
        ESP_LOGI(TAG, "Ignoring TALK press while STT/audio is busy");
        return;
    }

    s_stt_flow_start_ms = timing_now_ms();
    s_stt_release_ms = 0;
    s_stt_audio_ready_ms = 0;
    s_stt_api_task_start_ms = 0;

    ESP_LOGI(TAG, "TALK pressed");
    ESP_LOGI(TAG, "TIMING STT record_start total_ms=0");
    runtime_diag_log("stt_press_begin");

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
    ESP_LOGI(
        TAG,
        "TIMING STT recorder_started setup_ms=%lld total_ms=%lld",
        (long long)timing_since_ms(s_stt_flow_start_ms),
        (long long)timing_since_ms(s_stt_flow_start_ms)
    );
}

static void start_transcription_for_path(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        set_error_message("STT error", "Recording path is missing.");
        return;
    }

    if (!wifi_manager_is_connected()) {
        ESP_LOGW(TAG, "Cannot transcribe because Wi-Fi is not connected");
        set_error_message("No Wi-Fi", "Connect to Wi-Fi first.");
        return;
    }

    if (s_transcribe_task_handle != NULL) {
        ESP_LOGW(TAG, "Transcription task is already running");
        set_error_message("STT error", "Transcription is already running.");
        return;
    }

    snprintf(s_transcribe_path, sizeof(s_transcribe_path), "%s", path);
    s_transcribe_result[0] = '\0';
    s_transcribe_success = false;

    s_state = STT_MANAGER_STATE_TRANSCRIBING;
    runtime_diag_log("stt_before_transcribing_ui");
    show_temporary_status(
        "Transcribing",
        "Sending audio to STT.",
        false,
        true,
        false,
        false
    );

    /*
     * The display driver needs DMA-capable internal RAM for SPI flushes.
     * Give LVGL a short window to draw the "Transcribing" state before the
     * temporary HTTP/TLS task allocates its stack and starts TLS setup.
     */
    runtime_diag_log("stt_after_transcribing_ui_before_delay");
    ESP_LOGI(
        TAG,
        "TIMING STT transcribing_ui_ready since_release_ms=%lld since_audio_ready_ms=%lld total_ms=%lld",
        (long long)timing_since_ms(s_stt_release_ms),
        (long long)timing_since_ms(s_stt_audio_ready_ms),
        (long long)timing_since_ms(s_stt_flow_start_ms)
    );
    vTaskDelay(pdMS_TO_TICKS(STT_TRANSCRIBE_START_DELAY_MS));

    runtime_diag_log("stt_before_transcribe_task_create");
    BaseType_t ok = xTaskCreate(
        transcribe_task,
        "stt_transcribe",
        STT_TRANSCRIBE_TASK_STACK_SIZE,
        NULL,
        STT_TRANSCRIBE_TASK_PRIORITY,
        &s_transcribe_task_handle
    );

    runtime_diag_log("stt_after_transcribe_task_create");

    if (ok != pdPASS) {
        s_transcribe_task_handle = NULL;
        ESP_LOGE(TAG, "Failed to create STT transcription task");
        set_error_message("STT error", "Could not start transcription task.");
    }
}

static void handle_release(void)
{
    if (s_state != STT_MANAGER_STATE_RECORDING) {
        ESP_LOGI(TAG, "Ignoring release because STT is not recording");
        return;
    }

    s_stt_release_ms = timing_now_ms();

    ESP_LOGI(TAG, "TALK released");
    ESP_LOGI(
        TAG,
        "TIMING STT record_release held_ms=%lld total_ms=%lld",
        (long long)(s_stt_flow_start_ms > 0 ? s_stt_release_ms - s_stt_flow_start_ms : -1),
        (long long)timing_since_ms(s_stt_flow_start_ms)
    );
    runtime_diag_log("stt_release_begin");

    s_state = STT_MANAGER_STATE_PROCESSING;
    show_temporary_status("Saving", "Preparing recording.", false, true, false, false);

    audio_recorder_result_t result = {0};
    esp_err_t err = audio_recorder_stop(&result);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop audio recorder: %s", esp_err_to_name(err));
        set_error_status("Could not save recording.", err);
        return;
    }

    s_stt_audio_ready_ms = timing_now_ms();
    ESP_LOGI(
        TAG,
        "TIMING STT audio_ready stop_ms=%lld recorded_ms=%lu pcm_bytes=%lu file_bytes=%lu total_ms=%lld",
        (long long)(s_stt_release_ms > 0 ? s_stt_audio_ready_ms - s_stt_release_ms : -1),
        (unsigned long)result.duration_ms,
        (unsigned long)result.pcm_bytes,
        (unsigned long)result.wav_bytes,
        (long long)timing_since_ms(s_stt_flow_start_ms)
    );

    const char *path = result.path[0] != '\0' ? result.path : audio_recorder_get_path();
    start_transcription_for_path(path);
}

static void handle_transcribe_done(void)
{
    if (s_state != STT_MANAGER_STATE_TRANSCRIBING) {
        ESP_LOGI(TAG, "Ignoring transcription result because STT is not transcribing");
        return;
    }

    ESP_LOGI(
        TAG,
        "TIMING STT transcribe_done_event total_ms=%lld success=%d text_len=%u",
        (long long)timing_since_ms(s_stt_flow_start_ms),
        s_transcribe_success ? 1 : 0,
        (unsigned int)strlen(s_transcribe_result)
    );

    if (!s_transcribe_success) {
        set_error_message(
            "STT error",
            s_transcribe_result[0] != '\0' ?
                s_transcribe_result :
                "Transcription failed."
        );
        return;
    }

    if (s_transcribe_result[0] == '\0') {
        s_state = STT_MANAGER_STATE_DONE;
        show_stable_status(
            "Transcript empty",
            "No speech was recognized."
        );
        return;
    }

    s_state = STT_MANAGER_STATE_DONE;
    show_stable_status(
        "Transcript ready",
        s_transcribe_result
    );
}

static void handle_toggle_speaker_test(void)
{
    if (s_state == STT_MANAGER_STATE_RECORDING) {
        ESP_LOGI(TAG, "Ignoring speaker test while recording");
        return;
    }

    if (
        s_state == STT_MANAGER_STATE_PROCESSING ||
        s_state == STT_MANAGER_STATE_TRANSCRIBING ||
        s_state == STT_MANAGER_STATE_PLAYING_RECORDING
    ) {
        ESP_LOGI(TAG, "Ignoring speaker test while busy");
        return;
    }

    if (s_state == STT_MANAGER_STATE_SPEAKER_TEST) {
        ESP_LOGI(TAG, "Stopping 440 Hz speaker test");

        show_temporary_status(
            "Stopping",
            "Stopping speaker test.",
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
        restore_stable_status();
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
    show_temporary_status(
        "Testing",
        "Speaker test is playing.",
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
        s_state == STT_MANAGER_STATE_TRANSCRIBING ||
        s_state == STT_MANAGER_STATE_SPEAKER_TEST ||
        s_state == STT_MANAGER_STATE_PLAYING_RECORDING
    ) {
        ESP_LOGI(TAG, "Ignoring recording playback while busy");
        return;
    }

    const char *path = audio_recorder_get_path();

    ESP_LOGI(TAG, "Playing saved recording: %s", path);

    struct stat st = {0};
    uint32_t file_bytes = 0;
    uint32_t pcm_bytes = 0;
    uint32_t duration_ms = 0;

    if (stat(path, &st) == 0 && st.st_size > 0) {
        file_bytes = (uint32_t)st.st_size;
#if CONFIG_LOOKAI_STT_AUDIO_FORMAT_PCM
        pcm_bytes = file_bytes;
#elif CONFIG_LOOKAI_STT_AUDIO_FORMAT_WAV
        if (file_bytes > 44U) {
            pcm_bytes = file_bytes - 44U;
        }
#endif
        duration_ms = (uint32_t)(((uint64_t)pcm_bytes * 1000ULL) / 32000ULL);
    }

    float seconds = (float)duration_ms / 1000.0f;
    float kb = (float)file_bytes / 1024.0f;

    char playing_details[256];
    snprintf(
        playing_details,
        sizeof(playing_details),
        "File: %s\nLength: %.1f sec\nSize: %.1f KB",
        path,
        seconds,
        kb
    );

    s_state = STT_MANAGER_STATE_PLAYING_RECORDING;
    show_temporary_status(
        "Playing",
        playing_details,
        false,
        false,
        false,
        true
    );

    audio_playback_result_t result = {0};
#if CONFIG_LOOKAI_STT_AUDIO_FORMAT_PCM
    esp_err_t err = audio_playback_play_pcm_file(
        path,
        AUDIO_RECORDER_SAMPLE_RATE,
        AUDIO_RECORDER_CHANNELS,
        AUDIO_RECORDER_BITS_PER_SAMPLE,
        &result
    );
#elif CONFIG_LOOKAI_STT_AUDIO_FORMAT_WAV
    esp_err_t err = audio_playback_play_wav_file(path, &result);
#else
#error "No LookAI STT audio format selected."
#endif

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to play recording: %s", esp_err_to_name(err));
        set_error_status("Could not play recording.", err);
        return;
    }

    s_state = STT_MANAGER_STATE_DONE;
    restore_stable_status();
}

static void stt_task(void *arg)
{
    (void)arg;

    stt_manager_event_t event;

    show_stable_status("Ready", "Hold TALK to record.");

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

            case STT_MANAGER_EVENT_TRANSCRIBE_DONE:
                handle_transcribe_done();
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
