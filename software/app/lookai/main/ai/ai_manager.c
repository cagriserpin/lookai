/**
 * @file ai/ai_manager.c
 * @brief Voice assistant backend state machine implementation.
 */

#include "ai_manager.h"

#include "ai_api_client.h"
#include "audio_playback.h"
#include "audio_recorder.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "runtime_diag.h"
#include "sdkconfig.h"
#include "stt_api_client.h"
#include "tts_api_client.h"
#include "ui_manager.h"
#include "wifi_manager.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "ai_manager";

#define AI_EVENT_QUEUE_LEN 8
#define AI_MANAGER_TASK_STACK_SIZE 6144
#define AI_FLOW_TASK_STACK_SIZE 10240
#define AI_FLOW_TASK_PRIORITY 4
#define AI_FLOW_START_DELAY_MS 100
#define AI_TRANSCRIPT_BUFFER_SIZE 256
#define AI_RESPONSE_BUFFER_SIZE 768
#define AI_DISPLAY_BUFFER_SIZE 1024
#define AI_PATH_BUFFER_SIZE 96
#define AI_TTS_WAV_PATH "/spiffs/ai_tts_last.wav"

typedef enum {
    AI_MANAGER_EVENT_PRESS = 0,
    AI_MANAGER_EVENT_RELEASE,
    AI_MANAGER_EVENT_STREAM_PLAYBACK_STARTED,
    AI_MANAGER_EVENT_FLOW_DONE,
} ai_manager_event_t;

typedef enum {
    AI_MANAGER_STATE_READY = 0,
    AI_MANAGER_STATE_RECORDING,
    AI_MANAGER_STATE_SAVING,
    AI_MANAGER_STATE_TRANSCRIBING,
    AI_MANAGER_STATE_THINKING,
    AI_MANAGER_STATE_SPEAKING,
    AI_MANAGER_STATE_DONE,
    AI_MANAGER_STATE_ERROR,
} ai_manager_state_t;

static QueueHandle_t s_ai_event_queue = NULL;
static TaskHandle_t s_ai_task_handle = NULL;
static TaskHandle_t s_flow_task_handle = NULL;

static ai_manager_state_t s_state = AI_MANAGER_STATE_READY;

static char s_audio_path[AI_PATH_BUFFER_SIZE] = {0};
static char s_transcript[AI_TRANSCRIPT_BUFFER_SIZE] = {0};
static char s_response[AI_RESPONSE_BUFFER_SIZE] = {0};
static char s_display_text[AI_DISPLAY_BUFFER_SIZE] = {0};
static char s_flow_error[AI_DISPLAY_BUFFER_SIZE] = {0};
static bool s_flow_success = false;
static uint32_t s_generated_audio_bytes = 0;
static audio_playback_result_t s_playback_result = {0};

static int64_t s_ai_flow_start_ms = 0;
static int64_t s_ai_release_ms = 0;
static int64_t s_ai_audio_ready_ms = 0;
static int64_t s_ai_flow_task_start_ms = 0;
static int64_t s_ai_playback_start_ms = 0;
static volatile uint32_t s_flow_generation = 0;

typedef struct {
    uint32_t generation;
    char audio_path[AI_PATH_BUFFER_SIZE];
} ai_flow_context_t;

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
                case 0x87: replacement = 'C'; break;
                case 0xA7: replacement = 'c'; break;
                case 0x96: replacement = 'O'; break;
                case 0xB6: replacement = 'o'; break;
                case 0x9C: replacement = 'U'; break;
                case 0xBC: replacement = 'u'; break;
                default: replacement = '?'; break;
            }
        } else if (c0 == 0xC4) {
            consumed = 2;

            switch (c1) {
                case 0x9E: replacement = 'G'; break;
                case 0x9F: replacement = 'g'; break;
                case 0xB0: replacement = 'I'; break;
                case 0xB1: replacement = 'i'; break;
                default: replacement = '?'; break;
            }
        } else if (c0 == 0xC5) {
            consumed = 2;

            switch (c1) {
                case 0x9E: replacement = 'S'; break;
                case 0x9F: replacement = 's'; break;
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

static void update_ai_ui(
    const char *status,
    const char *result,
    bool recording,
    bool busy,
    bool speaking
)
{
    char display_result[AI_DISPLAY_BUFFER_SIZE];

    copy_tr_ascii_text(
        result != NULL ? result : "",
        display_result,
        sizeof(display_result)
    );

    ui_manager_update_ai_status(
        status,
        display_result,
        recording,
        busy,
        speaking
    );
}

static void build_conversation_display(
    char *buffer,
    size_t buffer_size,
    const char *transcript,
    const char *response
)
{
    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    if (transcript != NULL && transcript[0] != '\0' && response != NULL && response[0] != '\0') {
        snprintf(buffer, buffer_size, "You: %s\nAI: %s", transcript, response);
    } else if (transcript != NULL && transcript[0] != '\0') {
        snprintf(buffer, buffer_size, "You: %s", transcript);
    } else {
        snprintf(buffer, buffer_size, "%s", response != NULL ? response : "");
    }
}

static void append_display_line(char *buffer, size_t buffer_size, const char *line)
{
    if (buffer == NULL || buffer_size == 0 || line == NULL || line[0] == '\0') {
        return;
    }

    size_t len = strlen(buffer);
    if (len >= buffer_size - 1) {
        return;
    }

    snprintf(buffer + len, buffer_size - len, "%s%s", len > 0 ? "\n" : "", line);
}

static bool build_stt_token_line(
    char *out,
    size_t out_size
)
{
    if (out == NULL || out_size == 0) {
        return false;
    }

    out[0] = '\0';

    int input_tokens = 0;
    int output_tokens = 0;
    int total_tokens = 0;
    bool has_usage = stt_api_client_get_last_token_usage(
        &input_tokens,
        &output_tokens,
        &total_tokens
    );
    (void)total_tokens;

    if (!has_usage) {
        return false;
    }

    snprintf(
        out,
        out_size,
        "(STT: in %d / out %d)",
        input_tokens,
        output_tokens
    );
    return true;
}

static void build_final_token_line(
    char *out,
    size_t out_size,
    const char *stt_token_line
)
{
    if (out == NULL || out_size == 0) {
        return;
    }

    out[0] = '\0';

    int prompt_tokens = 0;
    int completion_tokens = 0;
    int ai_total_tokens = 0;
    bool has_ai_usage = ai_api_client_get_last_token_usage(
        &prompt_tokens,
        &completion_tokens,
        &ai_total_tokens
    );
    (void)ai_total_tokens;

    bool has_stt_usage = stt_token_line != NULL && stt_token_line[0] != '\0';

    char ai_token_line[64] = {0};
    if (has_ai_usage) {
        snprintf(
            ai_token_line,
            sizeof(ai_token_line),
            "(AI: in %d / out %d)",
            prompt_tokens,
            completion_tokens
        );
    }

    if (has_stt_usage && has_ai_usage) {
        snprintf(out, out_size, "%s %s", stt_token_line, ai_token_line);
    } else if (has_stt_usage) {
        snprintf(out, out_size, "%s", stt_token_line);
    } else if (has_ai_usage) {
        snprintf(out, out_size, "%s", ai_token_line);
    }
}

static void set_error_message(const char *status, const char *message)
{
    s_state = AI_MANAGER_STATE_ERROR;

    update_ai_ui(
        status != NULL ? status : "AI error",
        message != NULL ? message : "Unknown AI error.",
        false,
        false,
        false
    );
}

static void set_error_status(const char *message, esp_err_t err)
{
    char result[192];

    snprintf(
        result,
        sizeof(result),
        "%s\n%s",
        message != NULL ? message : "AI error",
        esp_err_to_name(err)
    );

    set_error_message("AI error", result);
}

static bool is_busy_for_new_action(void)
{
    return
        s_state == AI_MANAGER_STATE_RECORDING ||
        s_state == AI_MANAGER_STATE_SAVING ||
        s_state == AI_MANAGER_STATE_TRANSCRIBING ||
        s_state == AI_MANAGER_STATE_THINKING ||
        s_state == AI_MANAGER_STATE_SPEAKING ||
        s_flow_task_handle != NULL;
}

static bool flow_is_current(uint32_t generation)
{
    return generation == s_flow_generation;
}

static void post_event(ai_manager_event_t event)
{
    if (s_ai_event_queue == NULL) {
        return;
    }

    xQueueSend(s_ai_event_queue, &event, 0);
}

void ai_manager_press(void)
{
    post_event(AI_MANAGER_EVENT_PRESS);
}

void ai_manager_release(void)
{
    post_event(AI_MANAGER_EVENT_RELEASE);
}

static void ai_stream_playback_started_callback(void *user_ctx)
{
    uint32_t generation = (uint32_t)(uintptr_t)user_ctx;
    if (!flow_is_current(generation)) {
        return;
    }

    post_event(AI_MANAGER_EVENT_STREAM_PLAYBACK_STARTED);
}

static void record_flow_error(const char *message, const char *detail)
{
    snprintf(
        s_flow_error,
        sizeof(s_flow_error),
        "%s%s%s",
        message != NULL ? message : "AI flow failed.",
        detail != NULL && detail[0] != '\0' ? "\n" : "",
        detail != NULL ? detail : ""
    );
}

static void flow_task(void *arg)
{
    ai_flow_context_t *ctx = (ai_flow_context_t *)arg;
    uint32_t generation = ctx != NULL ? ctx->generation : s_flow_generation;
    char audio_path[AI_PATH_BUFFER_SIZE] = {0};

    if (ctx != NULL) {
        snprintf(audio_path, sizeof(audio_path), "%s", ctx->audio_path);
    }

    char transcript_raw[AI_TRANSCRIPT_BUFFER_SIZE] = {0};
    char response_raw[AI_RESPONSE_BUFFER_SIZE] = {0};
    char stt_token_line[96] = {0};
    char final_token_line[128] = {0};

    s_ai_flow_task_start_ms = timing_now_ms();
    ESP_LOGI(
        TAG,
        "TIMING AI flow_task_start since_release_ms=%lld since_audio_ready_ms=%lld total_ms=%lld",
        (long long)(s_ai_release_ms > 0 ? s_ai_flow_task_start_ms - s_ai_release_ms : -1),
        (long long)(s_ai_audio_ready_ms > 0 ? s_ai_flow_task_start_ms - s_ai_audio_ready_ms : -1),
        (long long)timing_since_ms(s_ai_flow_start_ms)
    );

    if (!flow_is_current(generation)) {
        goto stale_done;
    }

    s_flow_success = false;
    s_flow_error[0] = '\0';
    s_transcript[0] = '\0';
    s_response[0] = '\0';
    s_display_text[0] = '\0';
    s_generated_audio_bytes = 0;
    memset(&s_playback_result, 0, sizeof(s_playback_result));

    if (!wifi_manager_is_connected()) {
        record_flow_error("No Wi-Fi", "Connect to Wi-Fi first.");
        goto done;
    }

    s_state = AI_MANAGER_STATE_TRANSCRIBING;
    update_ai_ui("Transcribing", "Sending audio to STT.", false, true, false);
    runtime_diag_log("ai_before_stt_api");

    esp_err_t err = stt_api_client_transcribe_wav(
        audio_path,
        transcript_raw,
        sizeof(transcript_raw)
    );

    if (!flow_is_current(generation)) {
        goto stale_done;
    }

    runtime_diag_log("ai_after_stt_api");
    ESP_LOGI(
        TAG,
        "TIMING AI stt_done total_ms=%lld text_len=%u result=%s",
        (long long)timing_since_ms(s_ai_flow_start_ms),
        (unsigned int)strlen(transcript_raw),
        esp_err_to_name(err)
    );

    if (err != ESP_OK) {
        record_flow_error("STT failed", stt_api_client_get_last_error());
        goto done;
    }

    if (transcript_raw[0] == '\0') {
        record_flow_error("Transcript empty", "No speech was recognized.");
        goto done;
    }

    snprintf(s_transcript, sizeof(s_transcript), "%s", transcript_raw);
    (void)build_stt_token_line(stt_token_line, sizeof(stt_token_line));
    build_conversation_display(s_display_text, sizeof(s_display_text), s_transcript, NULL);
    append_display_line(s_display_text, sizeof(s_display_text), stt_token_line);

    s_state = AI_MANAGER_STATE_THINKING;
    update_ai_ui("Thinking", s_display_text, false, true, false);
    runtime_diag_log("ai_before_prompt_api");

    err = ai_api_client_generate_response(
        transcript_raw,
        response_raw,
        sizeof(response_raw)
    );

    if (!flow_is_current(generation)) {
        goto stale_done;
    }

    runtime_diag_log("ai_after_prompt_api");
    ESP_LOGI(
        TAG,
        "TIMING AI prompt_done total_ms=%lld response_len=%u result=%s",
        (long long)timing_since_ms(s_ai_flow_start_ms),
        (unsigned int)strlen(response_raw),
        esp_err_to_name(err)
    );

    if (err != ESP_OK) {
        record_flow_error("AI prompt failed", ai_api_client_get_last_error());
        goto done;
    }

    if (response_raw[0] == '\0') {
        record_flow_error("AI response empty", "Assistant returned no text.");
        goto done;
    }

    snprintf(s_response, sizeof(s_response), "%s", response_raw);
    build_conversation_display(s_display_text, sizeof(s_display_text), s_transcript, s_response);
    build_final_token_line(
        final_token_line,
        sizeof(final_token_line),
        stt_token_line
    );
    append_display_line(s_display_text, sizeof(s_display_text), final_token_line);

    if (!flow_is_current(generation)) {
        goto stale_done;
    }

    s_state = AI_MANAGER_STATE_SPEAKING;
    update_ai_ui("Speaking", s_display_text, false, true, false);

    uint32_t audio_bytes = 0;
    audio_playback_result_t playback = {0};
    tts_api_client_stream_callbacks_t stream_callbacks = {
        .on_playback_started = ai_stream_playback_started_callback,
        .user_ctx = (void *)(uintptr_t)generation,
    };

    runtime_diag_log("ai_before_tts_api");
#if CONFIG_LOOKAI_TTS_RESPONSE_FORMAT_PCM
    err = tts_api_client_generate_pcm_streaming(
        s_response,
        &audio_bytes,
        &playback,
        &stream_callbacks
    );
    const char *format_name = "pcm";
#elif CONFIG_LOOKAI_TTS_RESPONSE_FORMAT_WAV
    err = tts_api_client_generate_wav(
        s_response,
        AI_TTS_WAV_PATH,
        &audio_bytes
    );
    const char *format_name = "wav";
    if (err == ESP_OK) {
        if (stream_callbacks.on_playback_started != NULL) {
            stream_callbacks.on_playback_started(stream_callbacks.user_ctx);
        }
        err = audio_playback_play_wav_file(AI_TTS_WAV_PATH, &playback);
    }
#else
#error "No LookAI TTS response format selected."
#endif
    if (!flow_is_current(generation)) {
        goto stale_done;
    }

    runtime_diag_log("ai_after_tts_api");

    ESP_LOGI(
        TAG,
        "TIMING AI tts_done format=%s total_ms=%lld audio_bytes=%lu played_ms=%lu result=%s",
        format_name,
        (long long)timing_since_ms(s_ai_flow_start_ms),
        (unsigned long)audio_bytes,
        (unsigned long)playback.duration_ms,
        esp_err_to_name(err)
    );

    if (err != ESP_OK) {
        record_flow_error("TTS failed", tts_api_client_get_last_error());
        goto done;
    }

    s_generated_audio_bytes = audio_bytes;
    s_playback_result = playback;
    s_flow_success = true;

done:
    if (flow_is_current(generation)) {
        if (s_flow_task_handle == xTaskGetCurrentTaskHandle()) {
            s_flow_task_handle = NULL;
        }
        post_event(AI_MANAGER_EVENT_FLOW_DONE);
    }

stale_done:
    if (ctx != NULL) {
        free(ctx);
    }
    if (s_flow_task_handle == xTaskGetCurrentTaskHandle()) {
        s_flow_task_handle = NULL;
    }
    vTaskDelete(NULL);
}

static void cancel_current_activity_for_restart(void)
{
    if (!is_busy_for_new_action()) {
        return;
    }

    ESP_LOGI(TAG, "Cancelling active AI flow and starting a new recording");
    s_flow_generation++;

    if (s_state == AI_MANAGER_STATE_RECORDING) {
        audio_recorder_result_t discard = {0};
        (void)audio_recorder_stop(&discard);
    }

    audio_playback_stop_current();

    for (int i = 0; i < 8 && audio_playback_is_busy(); i++) {
        vTaskDelay(pdMS_TO_TICKS(25));
    }

    s_state = AI_MANAGER_STATE_READY;
}

static void handle_press(void)
{
    if (!wifi_manager_is_connected()) {
        ESP_LOGI(TAG, "Ignoring AI TALK press because Wi-Fi is not connected");
        update_ai_ui("No Wi-Fi", "Connect Wi-Fi to ask AI.", false, false, false);
        return;
    }

    if (is_busy_for_new_action()) {
        cancel_current_activity_for_restart();
    } else {
        s_flow_generation++;
    }

    s_ai_flow_start_ms = timing_now_ms();
    s_ai_release_ms = 0;
    s_ai_audio_ready_ms = 0;
    s_ai_flow_task_start_ms = 0;
    s_ai_playback_start_ms = 0;

    ESP_LOGI(TAG, "AI TALK pressed");
    ESP_LOGI(TAG, "TIMING AI record_start total_ms=0");
    runtime_diag_log("ai_press_begin");

    esp_err_t err = audio_recorder_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start AI recorder: %s", esp_err_to_name(err));
        set_error_status("Could not start recording.", err);
        return;
    }

    s_state = AI_MANAGER_STATE_RECORDING;
    ESP_LOGI(
        TAG,
        "TIMING AI recorder_started setup_ms=%lld total_ms=%lld",
        (long long)timing_since_ms(s_ai_flow_start_ms),
        (long long)timing_since_ms(s_ai_flow_start_ms)
    );
}

static void start_flow_for_path(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        set_error_message("AI error", "Recording path is missing.");
        return;
    }

    snprintf(s_audio_path, sizeof(s_audio_path), "%s", path);
    s_transcript[0] = '\0';
    s_response[0] = '\0';
    s_display_text[0] = '\0';
    s_flow_error[0] = '\0';
    s_flow_success = false;

    s_state = AI_MANAGER_STATE_TRANSCRIBING;
    update_ai_ui("Transcribing", "Sending audio to STT.", false, true, false);

    vTaskDelay(pdMS_TO_TICKS(AI_FLOW_START_DELAY_MS));

    ai_flow_context_t *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        set_error_message("AI error", "Could not allocate AI task context.");
        return;
    }

    ctx->generation = s_flow_generation;
    snprintf(ctx->audio_path, sizeof(ctx->audio_path), "%s", path);

    TaskHandle_t new_task = NULL;
    BaseType_t ok = xTaskCreate(
        flow_task,
        "ai_flow",
        AI_FLOW_TASK_STACK_SIZE,
        ctx,
        AI_FLOW_TASK_PRIORITY,
        &new_task
    );

    if (ok != pdPASS) {
        free(ctx);
        set_error_message("AI error", "Could not start AI task.");
        return;
    }

    s_flow_task_handle = new_task;
}

static void handle_release(void)
{
    if (s_state != AI_MANAGER_STATE_RECORDING) {
        ESP_LOGI(TAG, "Ignoring AI TALK release because AI is not recording");
        return;
    }

    s_ai_release_ms = timing_now_ms();

    ESP_LOGI(TAG, "AI TALK released");
    ESP_LOGI(
        TAG,
        "TIMING AI record_release held_ms=%lld total_ms=%lld",
        (long long)(s_ai_flow_start_ms > 0 ? s_ai_release_ms - s_ai_flow_start_ms : -1),
        (long long)timing_since_ms(s_ai_flow_start_ms)
    );
    runtime_diag_log("ai_release_begin");

    s_state = AI_MANAGER_STATE_SAVING;
    update_ai_ui("Saving", "Preparing recording.", false, true, false);

    audio_recorder_result_t result = {0};
    esp_err_t err = audio_recorder_stop(&result);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop AI recorder: %s", esp_err_to_name(err));
        set_error_status("Could not save recording.", err);
        return;
    }

    s_ai_audio_ready_ms = timing_now_ms();
    ESP_LOGI(
        TAG,
        "TIMING AI audio_ready stop_ms=%lld recorded_ms=%lu pcm_bytes=%lu file_bytes=%lu total_ms=%lld",
        (long long)(s_ai_release_ms > 0 ? s_ai_audio_ready_ms - s_ai_release_ms : -1),
        (unsigned long)result.duration_ms,
        (unsigned long)result.pcm_bytes,
        (unsigned long)result.wav_bytes,
        (long long)timing_since_ms(s_ai_flow_start_ms)
    );

    const char *path = result.path[0] != '\0' ? result.path : audio_recorder_get_path();
    start_flow_for_path(path);
}

static void handle_stream_playback_started(void)
{
    if (s_state != AI_MANAGER_STATE_SPEAKING && s_state != AI_MANAGER_STATE_THINKING) {
        return;
    }

    s_state = AI_MANAGER_STATE_SPEAKING;
    s_ai_playback_start_ms = timing_now_ms();

    ESP_LOGI(
        TAG,
        "TIMING AI stream_playback_started total_ms=%lld",
        (long long)timing_since_ms(s_ai_flow_start_ms)
    );

    update_ai_ui("Speaking", s_display_text, false, true, true);
}

static void handle_flow_done(void)
{
    if (
        s_state != AI_MANAGER_STATE_TRANSCRIBING &&
        s_state != AI_MANAGER_STATE_THINKING &&
        s_state != AI_MANAGER_STATE_SPEAKING
    ) {
        ESP_LOGI(TAG, "Ignoring AI flow result because state changed");
        return;
    }

    ESP_LOGI(
        TAG,
        "TIMING AI flow_done total_ms=%lld success=%d audio_bytes=%lu played_ms=%lu",
        (long long)timing_since_ms(s_ai_flow_start_ms),
        s_flow_success ? 1 : 0,
        (unsigned long)s_generated_audio_bytes,
        (unsigned long)s_playback_result.duration_ms
    );

    if (!s_flow_success) {
        set_error_message(
            "AI error",
            s_flow_error[0] != '\0' ? s_flow_error : "AI flow failed."
        );
        return;
    }

    s_state = AI_MANAGER_STATE_DONE;
    update_ai_ui("Done", s_display_text, false, false, false);
}

static void ai_task(void *arg)
{
    (void)arg;

    ai_manager_event_t event;

    update_ai_ui("Ready", "Hold TALK to ask AI.", false, false, false);

    while (true) {
        if (xQueueReceive(s_ai_event_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (event) {
            case AI_MANAGER_EVENT_PRESS:
                handle_press();
                break;

            case AI_MANAGER_EVENT_RELEASE:
                handle_release();
                break;

            case AI_MANAGER_EVENT_STREAM_PLAYBACK_STARTED:
                handle_stream_playback_started();
                break;

            case AI_MANAGER_EVENT_FLOW_DONE:
                handle_flow_done();
                break;

            default:
                break;
        }
    }
}

esp_err_t ai_manager_start(void)
{
    if (s_ai_event_queue != NULL) {
        return ESP_OK;
    }

    s_ai_event_queue = xQueueCreate(AI_EVENT_QUEUE_LEN, sizeof(ai_manager_event_t));
    if (s_ai_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create AI event queue");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreate(
        ai_task,
        "ai_task",
        AI_MANAGER_TASK_STACK_SIZE,
        NULL,
        5,
        &s_ai_task_handle
    );

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create AI task");
        vQueueDelete(s_ai_event_queue);
        s_ai_event_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "AI manager started");

    return ESP_OK;
}
