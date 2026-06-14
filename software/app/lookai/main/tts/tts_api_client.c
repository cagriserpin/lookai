/**
 * @file tts/tts_api_client.c
 * @brief OpenAI text-to-speech API client.
 */

#include "tts_api_client.h"
#include "runtime_diag.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

static const char *TAG = "tts_api_client";

#define TTS_API_CLIENT_TIMEOUT_MS 60000
#define TTS_API_CLIENT_CHUNK_SIZE 1024
#define TTS_API_CLIENT_STREAM_CHUNK_SIZE 4096
#define TTS_API_CLIENT_PCM_SAMPLE_RATE 24000
#define TTS_API_CLIENT_PCM_CHANNELS 1
#define TTS_API_CLIENT_PCM_BITS_PER_SAMPLE 16
#define TTS_API_CLIENT_ERROR_RESPONSE_MAX_BYTES 2048
#define TTS_API_CLIENT_MAX_INPUT_CHARS 800

static char s_last_error[192] = "";

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

const char *tts_api_client_get_last_error(void)
{
    return s_last_error[0] != '\0' ? s_last_error : "No TTS API client error.";
}

static void set_last_error(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vsnprintf(s_last_error, sizeof(s_last_error), fmt, args);
    va_end(args);

    ESP_LOGW(TAG, "%s", s_last_error);
}

static bool config_string_is_empty(const char *value)
{
    return value == NULL || value[0] == '\0';
}

static const char *select_api_key(void)
{
    if (!config_string_is_empty(CONFIG_LOOKAI_TTS_API_KEY)) {
        return CONFIG_LOOKAI_TTS_API_KEY;
    }

    return CONFIG_LOOKAI_STT_API_KEY;
}

static bool tts_model_supports_instructions(void)
{
    return strcmp(CONFIG_LOOKAI_TTS_MODEL, "gpt-4o-mini-tts") == 0;
}

static esp_err_t build_request_body_with_format(const char *text, const char *response_format, char **out_body)
{
    if (text == NULL || response_format == NULL || response_format[0] == '\0' || out_body == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(text) > TTS_API_CLIENT_MAX_INPUT_CHARS) {
        set_last_error("TTS input is longer than 800 characters.");
        return ESP_ERR_INVALID_SIZE;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        set_last_error("Could not allocate TTS request JSON.");
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "model", CONFIG_LOOKAI_TTS_MODEL);
    cJSON_AddStringToObject(root, "voice", CONFIG_LOOKAI_TTS_VOICE);
    cJSON_AddStringToObject(root, "input", text);
    cJSON_AddStringToObject(root, "response_format", response_format);

    if (tts_model_supports_instructions()) {
        cJSON_AddStringToObject(
            root,
            "instructions",
            "Turkceyi dogal, net ve sicak bir tonda konus. Cumleleri sakin ve anlasilir oku."
        );
    }

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (body == NULL) {
        set_last_error("Could not serialize TTS request JSON.");
        return ESP_ERR_NO_MEM;
    }

    *out_body = body;
    return ESP_OK;
}

static esp_err_t build_request_body_wav(const char *text, char **out_body)
{
    return build_request_body_with_format(text, "wav", out_body);
}

static esp_err_t build_request_body_pcm(const char *text, char **out_body)
{
    return build_request_body_with_format(text, "pcm", out_body);
}

static esp_err_t write_all(
    esp_http_client_handle_t client,
    const void *data,
    size_t len
)
{
    if (client == NULL || (data == NULL && len > 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *cursor = (const char *)data;
    size_t remaining = len;

    while (remaining > 0) {
        int written = esp_http_client_write(client, cursor, remaining);
        if (written < 0) {
            set_last_error("HTTP write failed.");
            return ESP_FAIL;
        }

        if (written == 0) {
            set_last_error("HTTP write stalled.");
            return ESP_ERR_TIMEOUT;
        }

        cursor += written;
        remaining -= (size_t)written;
    }

    return ESP_OK;
}

static esp_err_t read_error_response(
    esp_http_client_handle_t client,
    char *buffer,
    size_t buffer_size
)
{
    if (client == NULL || buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t total = 0;

    while (total + 1 < buffer_size) {
        int read_len = esp_http_client_read(
            client,
            buffer + total,
            buffer_size - total - 1
        );

        if (read_len < 0) {
            buffer[total] = '\0';
            return ESP_FAIL;
        }

        if (read_len == 0) {
            break;
        }

        total += (size_t)read_len;
    }

    buffer[total] = '\0';

    return ESP_OK;
}

static void set_error_from_api_response(int status_code, const char *response)
{
    if (response == NULL || response[0] == '\0') {
        set_last_error("TTS request failed with HTTP %d.", status_code);
        return;
    }

    cJSON *root = cJSON_Parse(response);
    if (root == NULL) {
        set_last_error("TTS request failed with HTTP %d.", status_code);
        return;
    }

    cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
    cJSON *message = NULL;

    if (cJSON_IsObject(error)) {
        message = cJSON_GetObjectItemCaseSensitive(error, "message");
    }

    if (cJSON_IsString(message) && message->valuestring != NULL) {
        set_last_error("HTTP %d: %s", status_code, message->valuestring);
    } else {
        set_last_error("TTS request failed with HTTP %d.", status_code);
    }

    cJSON_Delete(root);
}

static esp_err_t save_wav_response(
    esp_http_client_handle_t client,
    const char *out_wav_path,
    uint32_t *out_wav_bytes,
    int64_t api_start_ms
)
{
    FILE *file = fopen(out_wav_path, "wb");
    if (file == NULL) {
        set_last_error("Could not open TTS output file: %s", out_wav_path);
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t *chunk = (uint8_t *)malloc(TTS_API_CLIENT_CHUNK_SIZE);
    if (chunk == NULL) {
        fclose(file);
        set_last_error("Could not allocate TTS response buffer.");
        return ESP_ERR_NO_MEM;
    }

    uint32_t total = 0;
    esp_err_t err = ESP_OK;
    int64_t download_start_ms = timing_now_ms();
    int64_t first_byte_ms = 0;

    while (true) {
        int read_len = esp_http_client_read(
            client,
            (char *)chunk,
            TTS_API_CLIENT_CHUNK_SIZE
        );

        if (read_len < 0) {
            set_last_error("TTS audio response read failed.");
            err = ESP_FAIL;
            break;
        }

        if (read_len == 0) {
            break;
        }

        if (first_byte_ms == 0) {
            first_byte_ms = timing_now_ms();
            ESP_LOGI(
                TAG,
                "TIMING TTS_API first_audio_byte first_byte_ms=%lld total_ms=%lld",
                (long long)(first_byte_ms - download_start_ms),
                (long long)timing_since_ms(api_start_ms)
            );
        }

        size_t written = fwrite(chunk, 1, (size_t)read_len, file);
        if (written != (size_t)read_len) {
            set_last_error("Could not write TTS WAV file.");
            err = ESP_FAIL;
            break;
        }

        total += (uint32_t)read_len;
    }

    free(chunk);
    fclose(file);

    if (err != ESP_OK) {
        remove(out_wav_path);
        return err;
    }

    if (total == 0) {
        remove(out_wav_path);
        set_last_error("TTS response was empty.");
        return ESP_FAIL;
    }

    if (out_wav_bytes != NULL) {
        *out_wav_bytes = total;
    }

    ESP_LOGI(TAG, "Generated TTS WAV: %s, bytes=%lu", out_wav_path, (unsigned long)total);
    ESP_LOGI(
        TAG,
        "TIMING TTS_API download_done download_ms=%lld audio_bytes=%lu total_ms=%lld",
        (long long)timing_since_ms(download_start_ms),
        (unsigned long)total,
        (long long)timing_since_ms(api_start_ms)
    );

    return ESP_OK;
}

typedef struct {
    int64_t api_start_ms;
    const tts_api_client_stream_callbacks_t *callbacks;
} tts_stream_callback_ctx_t;

static void tts_stream_audio_callback(
    audio_playback_stream_event_t event,
    const audio_playback_result_t *result,
    void *user_ctx
)
{
    tts_stream_callback_ctx_t *ctx = (tts_stream_callback_ctx_t *)user_ctx;

    if (ctx == NULL) {
        return;
    }

    if (event == AUDIO_PLAYBACK_STREAM_EVENT_STARTED) {
        ESP_LOGI(
            TAG,
            "TIMING TTS_API stream_playback_started total_ms=%lld",
            (long long)timing_since_ms(ctx->api_start_ms)
        );

        if (ctx->callbacks != NULL && ctx->callbacks->on_playback_started != NULL) {
            ctx->callbacks->on_playback_started(ctx->callbacks->user_ctx);
        }
    } else if (event == AUDIO_PLAYBACK_STREAM_EVENT_DONE && result != NULL) {
        ESP_LOGI(
            TAG,
            "TIMING TTS_API stream_playback_done played_ms=%lu pcm_bytes=%lu total_ms=%lld",
            (unsigned long)result->duration_ms,
            (unsigned long)result->pcm_bytes,
            (long long)timing_since_ms(ctx->api_start_ms)
        );
    }
}

static esp_err_t save_wav_response_streaming(
    esp_http_client_handle_t client,
    const char *out_wav_path,
    uint32_t *out_wav_bytes,
    audio_playback_result_t *out_playback_result,
    const tts_api_client_stream_callbacks_t *callbacks,
    int64_t api_start_ms
)
{
    FILE *file = fopen(out_wav_path, "wb");
    if (file == NULL) {
        set_last_error("Could not open TTS output file: %s", out_wav_path);
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t *chunk = (uint8_t *)malloc(TTS_API_CLIENT_CHUNK_SIZE);
    if (chunk == NULL) {
        fclose(file);
        set_last_error("Could not allocate TTS response buffer.");
        return ESP_ERR_NO_MEM;
    }

    audio_playback_stream_t *stream = NULL;
    tts_stream_callback_ctx_t callback_ctx = {
        .api_start_ms = api_start_ms,
        .callbacks = callbacks,
    };

    esp_err_t err = audio_playback_stream_wav_start(
        &stream,
        tts_stream_audio_callback,
        &callback_ctx
    );
    if (err != ESP_OK) {
        free(chunk);
        fclose(file);
        set_last_error("Could not start streaming TTS playback: %s", esp_err_to_name(err));
        return err;
    }

    uint32_t total = 0;
    int64_t download_start_ms = timing_now_ms();
    int64_t first_byte_ms = 0;

    while (true) {
        int read_len = esp_http_client_read(
            client,
            (char *)chunk,
            TTS_API_CLIENT_CHUNK_SIZE
        );

        if (read_len < 0) {
            set_last_error("TTS audio response read failed.");
            err = ESP_FAIL;
            break;
        }

        if (read_len == 0) {
            break;
        }

        if (first_byte_ms == 0) {
            first_byte_ms = timing_now_ms();
            ESP_LOGI(
                TAG,
                "TIMING TTS_API first_audio_byte first_byte_ms=%lld total_ms=%lld",
                (long long)(first_byte_ms - download_start_ms),
                (long long)timing_since_ms(api_start_ms)
            );
        }

        size_t written = fwrite(chunk, 1, (size_t)read_len, file);
        if (written != (size_t)read_len) {
            set_last_error("Could not write TTS WAV file.");
            err = ESP_FAIL;
            break;
        }

        err = audio_playback_stream_wav_write(
            stream,
            chunk,
            (size_t)read_len,
            5000
        );
        if (err != ESP_OK) {
            set_last_error("Could not feed TTS audio stream: %s", esp_err_to_name(err));
            break;
        }

        total += (uint32_t)read_len;
    }

    free(chunk);
    fclose(file);

    if (total == 0 && err == ESP_OK) {
        set_last_error("TTS response was empty.");
        err = ESP_FAIL;
    }

    audio_playback_stream_wav_finish(stream, err);

    audio_playback_result_t playback = {0};
    esp_err_t playback_err = audio_playback_stream_wav_wait(
        stream,
        UINT32_MAX,
        &playback
    );

    if (out_playback_result != NULL) {
        *out_playback_result = playback;
    }

    if (err != ESP_OK) {
        remove(out_wav_path);
        return err;
    }

    if (playback_err != ESP_OK) {
        set_last_error("Streaming TTS playback failed: %s", esp_err_to_name(playback_err));
        return playback_err;
    }

    if (out_wav_bytes != NULL) {
        *out_wav_bytes = total;
    }

    ESP_LOGI(TAG, "Generated streaming TTS WAV: %s, bytes=%lu", out_wav_path, (unsigned long)total);
    ESP_LOGI(
        TAG,
        "TIMING TTS_API download_done download_ms=%lld audio_bytes=%lu total_ms=%lld",
        (long long)timing_since_ms(download_start_ms),
        (unsigned long)total,
        (long long)timing_since_ms(api_start_ms)
    );

    return ESP_OK;
}



static esp_err_t stream_pcm_response_direct(
    esp_http_client_handle_t client,
    uint32_t *out_pcm_bytes,
    audio_playback_result_t *out_playback_result,
    const tts_api_client_stream_callbacks_t *callbacks,
    int64_t api_start_ms
)
{
    uint8_t *chunk = (uint8_t *)malloc(TTS_API_CLIENT_STREAM_CHUNK_SIZE);
    if (chunk == NULL) {
        set_last_error("Could not allocate TTS PCM response buffer.");
        return ESP_ERR_NO_MEM;
    }

    audio_playback_pcm_stream_t *stream = NULL;
    audio_playback_stream_metrics_t stream_metrics = {0};
    tts_stream_callback_ctx_t callback_ctx = {
        .api_start_ms = api_start_ms,
        .callbacks = callbacks,
    };

    esp_err_t err = audio_playback_stream_pcm_start(
        &stream,
        TTS_API_CLIENT_PCM_SAMPLE_RATE,
        TTS_API_CLIENT_PCM_CHANNELS,
        TTS_API_CLIENT_PCM_BITS_PER_SAMPLE,
        tts_stream_audio_callback,
        &callback_ctx
    );
    if (err != ESP_OK) {
        free(chunk);
        set_last_error("Could not start direct PCM TTS playback: %s", esp_err_to_name(err));
        return err;
    }

    uint32_t total = 0;
    uint32_t read_calls = 0;
    uint32_t max_read_ms = 0;
    int64_t download_start_ms = timing_now_ms();
    int64_t first_byte_ms = 0;

    while (true) {
        int64_t read_start_ms = timing_now_ms();
        int read_len = esp_http_client_read(
            client,
            (char *)chunk,
            TTS_API_CLIENT_STREAM_CHUNK_SIZE
        );
        int64_t read_done_ms = timing_now_ms();
        uint32_t read_ms = (uint32_t)(read_done_ms - read_start_ms);
        if (read_ms > max_read_ms) {
            max_read_ms = read_ms;
        }

        if (read_len < 0) {
            set_last_error("TTS PCM response read failed.");
            err = ESP_FAIL;
            break;
        }

        if (read_len == 0) {
            break;
        }

        read_calls++;

        if (first_byte_ms == 0) {
            first_byte_ms = read_done_ms;
            ESP_LOGI(
                TAG,
                "TIMING TTS_API first_pcm_byte first_byte_ms=%lld total_ms=%lld",
                (long long)(first_byte_ms - download_start_ms),
                (long long)timing_since_ms(api_start_ms)
            );
        }

        err = audio_playback_stream_pcm_write(
            stream,
            chunk,
            (size_t)read_len,
            5000
        );
        if (err != ESP_OK) {
            set_last_error("Could not feed direct TTS PCM stream: %s", esp_err_to_name(err));
            break;
        }

        total += (uint32_t)read_len;
    }

    free(chunk);

    if (total == 0 && err == ESP_OK) {
        set_last_error("TTS PCM response was empty.");
        err = ESP_FAIL;
    }

    audio_playback_stream_pcm_finish(stream, err);

    audio_playback_result_t playback = {0};
    esp_err_t playback_err = audio_playback_stream_pcm_wait(
        stream,
        UINT32_MAX,
        &playback,
        &stream_metrics
    );

    if (out_playback_result != NULL) {
        *out_playback_result = playback;
    }

    if (err != ESP_OK) {
        return err;
    }

    if (playback_err != ESP_OK) {
        set_last_error("Direct PCM TTS playback failed: %s", esp_err_to_name(playback_err));
        return playback_err;
    }

    if (out_pcm_bytes != NULL) {
        *out_pcm_bytes = total;
    }

    int64_t download_ms_i64 = timing_since_ms(download_start_ms);
    uint32_t download_ms = download_ms_i64 > 0 ? (uint32_t)download_ms_i64 : 1U;
    uint32_t avg_bps = (uint32_t)(((uint64_t)total * 1000ULL) / download_ms);

    ESP_LOGI(
        TAG,
        "TIMING TTS_API download_done download_ms=%lu pcm_bytes=%lu total_ms=%lld",
        (unsigned long)download_ms,
        (unsigned long)total,
        (long long)timing_since_ms(api_start_ms)
    );

    ESP_LOGI(
        TAG,
        "TIMING TTS_STREAM_METRICS http_read_calls=%lu http_read_bytes=%lu http_avg_bps=%lu http_read_max_ms=%lu producer_bytes=%lu consumer_bytes=%lu full_waits=%lu empty_waits=%lu underruns=%lu max_fill_bytes=%lu max_ready_blocks=%lu block_count=%lu block_size=%lu played_ms=%lu total_ms=%lld",
        (unsigned long)read_calls,
        (unsigned long)total,
        (unsigned long)avg_bps,
        (unsigned long)max_read_ms,
        (unsigned long)stream_metrics.producer_bytes,
        (unsigned long)stream_metrics.consumer_bytes,
        (unsigned long)stream_metrics.ring_full_wait_count,
        (unsigned long)stream_metrics.ring_empty_wait_count,
        (unsigned long)stream_metrics.underrun_count,
        (unsigned long)stream_metrics.max_fill_bytes,
        (unsigned long)stream_metrics.max_ready_blocks,
        (unsigned long)stream_metrics.block_count,
        (unsigned long)stream_metrics.block_size,
        (unsigned long)playback.duration_ms,
        (long long)timing_since_ms(api_start_ms)
    );

    return ESP_OK;
}



esp_err_t tts_api_client_generate_wav(
    const char *text,
    const char *out_wav_path,
    uint32_t *out_wav_bytes
)
{
    int64_t api_start_ms = timing_now_ms();
    int64_t body_start_ms = 0;
    int64_t open_start_ms = 0;
    int64_t upload_start_ms = 0;
    int64_t headers_start_ms = 0;

    ESP_LOGI(TAG, "TIMING TTS_API start total_ms=0");

    if (text == NULL || text[0] == '\0' || out_wav_path == NULL || out_wav_path[0] == '\0') {
        set_last_error("Invalid TTS API client arguments.");
        return ESP_ERR_INVALID_ARG;
    }

    s_last_error[0] = '\0';
    if (out_wav_bytes != NULL) {
        *out_wav_bytes = 0;
    }

    const char *api_key = select_api_key();
    if (config_string_is_empty(api_key)) {
        set_last_error("TTS API key is not configured.");
        return ESP_ERR_INVALID_STATE;
    }

    if (config_string_is_empty(CONFIG_LOOKAI_TTS_ENDPOINT)) {
        set_last_error("TTS endpoint is not configured.");
        return ESP_ERR_INVALID_STATE;
    }

    if (config_string_is_empty(CONFIG_LOOKAI_TTS_MODEL)) {
        set_last_error("TTS model is not configured.");
        return ESP_ERR_INVALID_STATE;
    }

    if (config_string_is_empty(CONFIG_LOOKAI_TTS_VOICE)) {
        set_last_error("TTS voice is not configured.");
        return ESP_ERR_INVALID_STATE;
    }

    char *request_body = NULL;
    body_start_ms = timing_now_ms();
    esp_err_t err = build_request_body_wav(text, &request_body);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(
        TAG,
        "TIMING TTS_API body_ready body_ms=%lld text_len=%u body_bytes=%u total_ms=%lld",
        (long long)timing_since_ms(body_start_ms),
        (unsigned int)strlen(text),
        (unsigned int)strlen(request_body),
        (long long)timing_since_ms(api_start_ms)
    );

    char auth_header[512];
    int auth_written = snprintf(
        auth_header,
        sizeof(auth_header),
        "Bearer %s",
        api_key
    );

    if (auth_written < 0 || (size_t)auth_written >= sizeof(auth_header)) {
        free(request_body);
        set_last_error("TTS API key is too long for auth header.");
        return ESP_ERR_INVALID_SIZE;
    }

    esp_http_client_config_t config = {
        .url = CONFIG_LOOKAI_TTS_ENDPOINT,
        .method = HTTP_METHOD_POST,
        .timeout_ms = TTS_API_CLIENT_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        free(request_body);
        set_last_error("Could not initialize TTS HTTP client.");
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Accept", "audio/wav");

    runtime_diag_log("tts_api_before_http_open");
    open_start_ms = timing_now_ms();
    err = esp_http_client_open(client, strlen(request_body));
    runtime_diag_log("tts_api_after_http_open");
    ESP_LOGI(
        TAG,
        "TIMING TTS_API http_open open_ms=%lld total_ms=%lld result=%s",
        (long long)timing_since_ms(open_start_ms),
        (long long)timing_since_ms(api_start_ms),
        esp_err_to_name(err)
    );
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        free(request_body);
        set_last_error("Could not open TTS HTTP connection: %s", esp_err_to_name(err));
        return err;
    }

    upload_start_ms = timing_now_ms();
    size_t request_body_len = strlen(request_body);
    err = write_all(client, request_body, request_body_len);
    free(request_body);
    runtime_diag_log("tts_api_after_upload");
    ESP_LOGI(
        TAG,
        "TIMING TTS_API upload upload_ms=%lld body_bytes=%u total_ms=%lld result=%s",
        (long long)timing_since_ms(upload_start_ms),
        (unsigned int)request_body_len,
        (long long)timing_since_ms(api_start_ms),
        esp_err_to_name(err)
    );

    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    headers_start_ms = timing_now_ms();
    int64_t content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(
        TAG,
        "TIMING TTS_API headers headers_ms=%lld status=%d content_length=%lld total_ms=%lld",
        (long long)timing_since_ms(headers_start_ms),
        status_code,
        (long long)content_length,
        (long long)timing_since_ms(api_start_ms)
    );

    if (status_code != 200) {
        char response[TTS_API_CLIENT_ERROR_RESPONSE_MAX_BYTES + 1];
        response[0] = '\0';
        read_error_response(client, response, sizeof(response));
        set_error_from_api_response(status_code, response);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    err = save_wav_response(client, out_wav_path, out_wav_bytes, api_start_ms);
    runtime_diag_log("tts_api_after_save_wav");

    esp_http_client_cleanup(client);
    runtime_diag_log("tts_api_after_http_cleanup");
    ESP_LOGI(
        TAG,
        "TIMING TTS_API done total_ms=%lld result=%s",
        (long long)timing_since_ms(api_start_ms),
        esp_err_to_name(err)
    );

    return err;
}

esp_err_t tts_api_client_generate_wav_streaming(
    const char *text,
    const char *out_wav_path,
    uint32_t *out_wav_bytes,
    audio_playback_result_t *out_playback_result,
    const tts_api_client_stream_callbacks_t *callbacks
)
{
    int64_t api_start_ms = timing_now_ms();
    int64_t body_start_ms = 0;
    int64_t open_start_ms = 0;
    int64_t upload_start_ms = 0;
    int64_t headers_start_ms = 0;

    ESP_LOGI(TAG, "TIMING TTS_API start total_ms=0");

    if (text == NULL || text[0] == '\0' || out_wav_path == NULL || out_wav_path[0] == '\0') {
        set_last_error("Invalid TTS API client arguments.");
        return ESP_ERR_INVALID_ARG;
    }

    s_last_error[0] = '\0';
    if (out_wav_bytes != NULL) {
        *out_wav_bytes = 0;
    }
    if (out_playback_result != NULL) {
        memset(out_playback_result, 0, sizeof(*out_playback_result));
    }

    const char *api_key = select_api_key();
    if (config_string_is_empty(api_key)) {
        set_last_error("TTS API key is not configured.");
        return ESP_ERR_INVALID_STATE;
    }

    if (config_string_is_empty(CONFIG_LOOKAI_TTS_ENDPOINT)) {
        set_last_error("TTS endpoint is not configured.");
        return ESP_ERR_INVALID_STATE;
    }

    if (config_string_is_empty(CONFIG_LOOKAI_TTS_MODEL)) {
        set_last_error("TTS model is not configured.");
        return ESP_ERR_INVALID_STATE;
    }

    if (config_string_is_empty(CONFIG_LOOKAI_TTS_VOICE)) {
        set_last_error("TTS voice is not configured.");
        return ESP_ERR_INVALID_STATE;
    }

    char *request_body = NULL;
    body_start_ms = timing_now_ms();
    esp_err_t err = build_request_body_wav(text, &request_body);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(
        TAG,
        "TIMING TTS_API body_ready body_ms=%lld text_len=%u body_bytes=%u total_ms=%lld",
        (long long)timing_since_ms(body_start_ms),
        (unsigned int)strlen(text),
        (unsigned int)strlen(request_body),
        (long long)timing_since_ms(api_start_ms)
    );

    char auth_header[512];
    int auth_written = snprintf(
        auth_header,
        sizeof(auth_header),
        "Bearer %s",
        api_key
    );

    if (auth_written < 0 || (size_t)auth_written >= sizeof(auth_header)) {
        free(request_body);
        set_last_error("TTS API key is too long for auth header.");
        return ESP_ERR_INVALID_SIZE;
    }

    esp_http_client_config_t config = {
        .url = CONFIG_LOOKAI_TTS_ENDPOINT,
        .method = HTTP_METHOD_POST,
        .timeout_ms = TTS_API_CLIENT_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        free(request_body);
        set_last_error("Could not initialize TTS HTTP client.");
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Accept", "audio/wav");

    runtime_diag_log("tts_api_before_http_open");
    open_start_ms = timing_now_ms();
    err = esp_http_client_open(client, strlen(request_body));
    runtime_diag_log("tts_api_after_http_open");
    ESP_LOGI(
        TAG,
        "TIMING TTS_API http_open open_ms=%lld total_ms=%lld result=%s",
        (long long)timing_since_ms(open_start_ms),
        (long long)timing_since_ms(api_start_ms),
        esp_err_to_name(err)
    );
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        free(request_body);
        set_last_error("Could not open TTS HTTP connection: %s", esp_err_to_name(err));
        return err;
    }

    upload_start_ms = timing_now_ms();
    size_t request_body_len = strlen(request_body);
    err = write_all(client, request_body, request_body_len);
    free(request_body);
    runtime_diag_log("tts_api_after_upload");
    ESP_LOGI(
        TAG,
        "TIMING TTS_API upload upload_ms=%lld body_bytes=%u total_ms=%lld result=%s",
        (long long)timing_since_ms(upload_start_ms),
        (unsigned int)request_body_len,
        (long long)timing_since_ms(api_start_ms),
        esp_err_to_name(err)
    );

    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    headers_start_ms = timing_now_ms();
    int64_t content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(
        TAG,
        "TIMING TTS_API headers headers_ms=%lld status=%d content_length=%lld total_ms=%lld",
        (long long)timing_since_ms(headers_start_ms),
        status_code,
        (long long)content_length,
        (long long)timing_since_ms(api_start_ms)
    );

    if (status_code != 200) {
        char response[TTS_API_CLIENT_ERROR_RESPONSE_MAX_BYTES + 1];
        response[0] = '\0';
        read_error_response(client, response, sizeof(response));
        set_error_from_api_response(status_code, response);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    err = save_wav_response_streaming(
        client,
        out_wav_path,
        out_wav_bytes,
        out_playback_result,
        callbacks,
        api_start_ms
    );
    runtime_diag_log("tts_api_after_save_wav");

    esp_http_client_cleanup(client);
    runtime_diag_log("tts_api_after_http_cleanup");
    ESP_LOGI(
        TAG,
        "TIMING TTS_API done total_ms=%lld result=%s",
        (long long)timing_since_ms(api_start_ms),
        esp_err_to_name(err)
    );

    return err;
}

esp_err_t tts_api_client_generate_pcm_streaming(
    const char *text,
    uint32_t *out_pcm_bytes,
    audio_playback_result_t *out_playback_result,
    const tts_api_client_stream_callbacks_t *callbacks
)
{
    int64_t api_start_ms = timing_now_ms();
    int64_t body_start_ms = 0;
    int64_t open_start_ms = 0;
    int64_t upload_start_ms = 0;
    int64_t headers_start_ms = 0;

    ESP_LOGI(TAG, "TIMING TTS_API start total_ms=0 mode=pcm_ring");

    if (text == NULL || text[0] == '\0') {
        set_last_error("Invalid TTS API client arguments.");
        return ESP_ERR_INVALID_ARG;
    }

    s_last_error[0] = '\0';
    if (out_pcm_bytes != NULL) {
        *out_pcm_bytes = 0;
    }
    if (out_playback_result != NULL) {
        memset(out_playback_result, 0, sizeof(*out_playback_result));
    }

    const char *api_key = select_api_key();
    if (config_string_is_empty(api_key)) {
        set_last_error("TTS API key is not configured.");
        return ESP_ERR_INVALID_STATE;
    }

    if (config_string_is_empty(CONFIG_LOOKAI_TTS_ENDPOINT)) {
        set_last_error("TTS endpoint is not configured.");
        return ESP_ERR_INVALID_STATE;
    }

    if (config_string_is_empty(CONFIG_LOOKAI_TTS_MODEL)) {
        set_last_error("TTS model is not configured.");
        return ESP_ERR_INVALID_STATE;
    }

    if (config_string_is_empty(CONFIG_LOOKAI_TTS_VOICE)) {
        set_last_error("TTS voice is not configured.");
        return ESP_ERR_INVALID_STATE;
    }

    char *request_body = NULL;
    body_start_ms = timing_now_ms();
    esp_err_t err = build_request_body_pcm(text, &request_body);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(
        TAG,
        "TIMING TTS_API body_ready body_ms=%lld text_len=%u body_bytes=%u total_ms=%lld response_format=pcm",
        (long long)timing_since_ms(body_start_ms),
        (unsigned int)strlen(text),
        (unsigned int)strlen(request_body),
        (long long)timing_since_ms(api_start_ms)
    );

    char auth_header[512];
    int auth_written = snprintf(
        auth_header,
        sizeof(auth_header),
        "Bearer %s",
        api_key
    );

    if (auth_written < 0 || (size_t)auth_written >= sizeof(auth_header)) {
        free(request_body);
        set_last_error("TTS API key is too long for auth header.");
        return ESP_ERR_INVALID_SIZE;
    }

    esp_http_client_config_t config = {
        .url = CONFIG_LOOKAI_TTS_ENDPOINT,
        .method = HTTP_METHOD_POST,
        .timeout_ms = TTS_API_CLIENT_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = TTS_API_CLIENT_STREAM_CHUNK_SIZE,
        .buffer_size_tx = 1024,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        free(request_body);
        set_last_error("Could not initialize TTS HTTP client.");
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Accept", "audio/pcm");
    esp_http_client_set_header(client, "Connection", "close");

    runtime_diag_log("tts_api_before_http_open");
    open_start_ms = timing_now_ms();
    err = esp_http_client_open(client, strlen(request_body));
    runtime_diag_log("tts_api_after_http_open");
    ESP_LOGI(
        TAG,
        "TIMING TTS_API http_open open_ms=%lld total_ms=%lld result=%s",
        (long long)timing_since_ms(open_start_ms),
        (long long)timing_since_ms(api_start_ms),
        esp_err_to_name(err)
    );
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        free(request_body);
        set_last_error("Could not open TTS HTTP connection: %s", esp_err_to_name(err));
        return err;
    }

    upload_start_ms = timing_now_ms();
    size_t request_body_len = strlen(request_body);
    err = write_all(client, request_body, request_body_len);
    free(request_body);
    runtime_diag_log("tts_api_after_upload");
    ESP_LOGI(
        TAG,
        "TIMING TTS_API upload upload_ms=%lld body_bytes=%u total_ms=%lld result=%s",
        (long long)timing_since_ms(upload_start_ms),
        (unsigned int)request_body_len,
        (long long)timing_since_ms(api_start_ms),
        esp_err_to_name(err)
    );

    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    headers_start_ms = timing_now_ms();
    int64_t content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(
        TAG,
        "TIMING TTS_API headers headers_ms=%lld status=%d content_length=%lld total_ms=%lld",
        (long long)timing_since_ms(headers_start_ms),
        status_code,
        (long long)content_length,
        (long long)timing_since_ms(api_start_ms)
    );

    if (status_code != 200) {
        char response[TTS_API_CLIENT_ERROR_RESPONSE_MAX_BYTES + 1];
        response[0] = '\0';
        read_error_response(client, response, sizeof(response));
        set_error_from_api_response(status_code, response);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    err = stream_pcm_response_direct(
        client,
        out_pcm_bytes,
        out_playback_result,
        callbacks,
        api_start_ms
    );
    runtime_diag_log("tts_api_after_pcm_stream");

    esp_http_client_cleanup(client);
    runtime_diag_log("tts_api_after_http_cleanup");
    ESP_LOGI(
        TAG,
        "TIMING TTS_API done total_ms=%lld result=%s",
        (long long)timing_since_ms(api_start_ms),
        esp_err_to_name(err)
    );

    return err;
}
