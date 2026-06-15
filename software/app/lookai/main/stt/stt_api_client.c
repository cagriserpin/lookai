/**
 * @file stt/stt_api_client.c
 * @brief OpenAI-compatible speech-to-text API client.
 */

#include "stt_api_client.h"

#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "runtime_diag.h"
#include "app_settings.h"

static const char *TAG = "stt_api_client";

#define STT_API_CLIENT_BOUNDARY "----LookAIFormBoundary7MA4YWxkTrZu0gW"
#define STT_API_CLIENT_TIMEOUT_MS 60000
#define STT_API_CLIENT_FILE_CHUNK_SIZE 1024
#define STT_API_CLIENT_RESPONSE_MAX_BYTES 4096

static char s_last_error[192] = "";
static bool s_last_usage_valid = false;
static int s_last_input_tokens = 0;
static int s_last_output_tokens = 0;
static int s_last_total_tokens = 0;

static void reset_token_usage(void)
{
    s_last_usage_valid = false;
    s_last_input_tokens = 0;
    s_last_output_tokens = 0;
    s_last_total_tokens = 0;
}

bool stt_api_client_get_last_token_usage(
    int *input_tokens,
    int *output_tokens,
    int *total_tokens
)
{
    if (!s_last_usage_valid) {
        return false;
    }

    if (input_tokens != NULL) {
        *input_tokens = s_last_input_tokens;
    }
    if (output_tokens != NULL) {
        *output_tokens = s_last_output_tokens;
    }
    if (total_tokens != NULL) {
        *total_tokens = s_last_total_tokens;
    }

    return true;
}

static bool json_get_int(cJSON *object, const char *name, int *out_value)
{
    if (object == NULL || name == NULL || out_value == NULL) {
        return false;
    }

    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item)) {
        return false;
    }

    *out_value = item->valueint;
    return true;
}

static void parse_token_usage(cJSON *root)
{
    reset_token_usage();

    cJSON *usage = cJSON_GetObjectItemCaseSensitive(root, "usage");
    if (!cJSON_IsObject(usage)) {
        return;
    }

    bool has_input = json_get_int(usage, "input_tokens", &s_last_input_tokens);
    bool has_output = json_get_int(usage, "output_tokens", &s_last_output_tokens);
    bool has_total = json_get_int(usage, "total_tokens", &s_last_total_tokens);

    if (!has_input) {
        has_input = json_get_int(usage, "prompt_tokens", &s_last_input_tokens);
    }
    if (!has_output) {
        has_output = json_get_int(usage, "completion_tokens", &s_last_output_tokens);
    }
    if (!has_total && (has_input || has_output)) {
        s_last_total_tokens = s_last_input_tokens + s_last_output_tokens;
        has_total = true;
    }

    s_last_usage_valid = has_input || has_output || has_total;
}

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

const char *stt_api_client_get_last_error(void)
{
    return s_last_error[0] != '\0' ? s_last_error : "No STT API client error.";
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

static const char *stt_audio_format_label(void)
{
#if CONFIG_LOOKAI_STT_AUDIO_FORMAT_PCM
    return "pcm";
#elif CONFIG_LOOKAI_STT_AUDIO_FORMAT_WAV
    return "wav";
#else
    return "unknown";
#endif
}

static const char *stt_audio_upload_filename(void)
{
#if CONFIG_LOOKAI_STT_AUDIO_FORMAT_PCM
    return "stt_last.pcm";
#elif CONFIG_LOOKAI_STT_AUDIO_FORMAT_WAV
    return "stt_last.wav";
#else
    return "stt_last.bin";
#endif
}

static const char *stt_audio_content_type(void)
{
#if CONFIG_LOOKAI_STT_AUDIO_FORMAT_PCM
    return "audio/pcm";
#elif CONFIG_LOOKAI_STT_AUDIO_FORMAT_WAV
    return "audio/wav";
#else
    return "application/octet-stream";
#endif
}

static esp_err_t build_form_field(
    char *buffer,
    size_t buffer_size,
    const char *name,
    const char *value
)
{
    if (buffer == NULL || name == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int written = snprintf(
        buffer,
        buffer_size,
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"%s\"\r\n"
        "\r\n"
        "%s\r\n",
        STT_API_CLIENT_BOUNDARY,
        name,
        value
    );

    if (written < 0 || (size_t)written >= buffer_size) {
        set_last_error("Multipart field too large: %s", name);
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

static esp_err_t build_file_header(
    char *buffer,
    size_t buffer_size
)
{
    if (buffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int written = snprintf(
        buffer,
        buffer_size,
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"
        "Content-Type: %s\r\n"
        "\r\n",
        STT_API_CLIENT_BOUNDARY,
        stt_audio_upload_filename(),
        stt_audio_content_type()
    );

    if (written < 0 || (size_t)written >= buffer_size) {
        set_last_error("Multipart file header too large.");
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

static esp_err_t build_closing_boundary(
    char *buffer,
    size_t buffer_size
)
{
    if (buffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int written = snprintf(
        buffer,
        buffer_size,
        "\r\n--%s--\r\n",
        STT_API_CLIENT_BOUNDARY
    );

    if (written < 0 || (size_t)written >= buffer_size) {
        set_last_error("Multipart closing boundary too large.");
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
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

static esp_err_t parse_transcript_response(
    const char *response,
    char *out_text,
    size_t out_text_size
)
{
    if (response == NULL || out_text == NULL || out_text_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_Parse(response);
    if (root == NULL) {
        set_last_error("Could not parse transcription JSON response.");
        return ESP_FAIL;
    }

    parse_token_usage(root);

    cJSON *text = cJSON_GetObjectItemCaseSensitive(root, "text");
    if (!cJSON_IsString(text) || text->valuestring == NULL) {
        cJSON_Delete(root);
        set_last_error("Transcription response did not include text.");
        return ESP_ERR_NOT_FOUND;
    }

    int written = snprintf(out_text, out_text_size, "%s", text->valuestring);
    cJSON_Delete(root);

    if (written < 0) {
        set_last_error("Could not copy transcript text.");
        return ESP_FAIL;
    }

    if ((size_t)written >= out_text_size) {
        set_last_error("Transcript was truncated.");
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

static void set_error_from_api_response(int status_code, const char *response)
{
    if (response == NULL || response[0] == '\0') {
        set_last_error("Transcription request failed with HTTP %d.", status_code);
        return;
    }

    cJSON *root = cJSON_Parse(response);
    if (root == NULL) {
        set_last_error("Transcription request failed with HTTP %d.", status_code);
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
        set_last_error("Transcription request failed with HTTP %d.", status_code);
    }

    cJSON_Delete(root);
}

static esp_err_t read_response_body(
    esp_http_client_handle_t client,
    char **out_response
)
{
    if (client == NULL || out_response == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char *response = (char *)calloc(1, STT_API_CLIENT_RESPONSE_MAX_BYTES + 1);
    if (response == NULL) {
        set_last_error("Could not allocate HTTP response buffer.");
        return ESP_ERR_NO_MEM;
    }

    size_t total = 0;

    while (total < STT_API_CLIENT_RESPONSE_MAX_BYTES) {
        int read_len = esp_http_client_read(
            client,
            response + total,
            STT_API_CLIENT_RESPONSE_MAX_BYTES - total
        );

        if (read_len < 0) {
            free(response);
            set_last_error("HTTP response read failed.");
            return ESP_FAIL;
        }

        if (read_len == 0) {
            break;
        }

        total += (size_t)read_len;
    }

    response[total] = '\0';
    *out_response = response;

    if (total == STT_API_CLIENT_RESPONSE_MAX_BYTES) {
        ESP_LOGW(TAG, "HTTP response reached local buffer limit");
    }

    return ESP_OK;
}

esp_err_t stt_api_client_transcribe_wav(
    const char *wav_path,
    char *out_text,
    size_t out_text_size
)
{
    if (wav_path == NULL || out_text == NULL || out_text_size == 0) {
        set_last_error("Invalid STT API client arguments.");
        return ESP_ERR_INVALID_ARG;
    }

    int64_t api_start_ms = timing_now_ms();
    int64_t open_start_ms = 0;
    int64_t upload_start_ms = 0;
    int64_t headers_start_ms = 0;
    int64_t read_start_ms = 0;
    int64_t parse_start_ms = 0;

    out_text[0] = '\0';
    s_last_error[0] = '\0';
    reset_token_usage();
    ESP_LOGI(TAG, "TIMING STT_API start total_ms=0");

    if (config_string_is_empty(CONFIG_LOOKAI_STT_API_KEY)) {
        set_last_error("STT API key is not configured.");
        return ESP_ERR_INVALID_STATE;
    }

    if (config_string_is_empty(CONFIG_LOOKAI_STT_ENDPOINT)) {
        set_last_error("STT endpoint is not configured.");
        return ESP_ERR_INVALID_STATE;
    }

    if (config_string_is_empty(app_settings_get_stt_model())) {
        set_last_error("STT model is not configured.");
        return ESP_ERR_INVALID_STATE;
    }

    struct stat st = {0};
    if (stat(wav_path, &st) != 0 || st.st_size <= 0) {
        set_last_error("WAV file is missing or empty: %s", wav_path);
        return ESP_ERR_NOT_FOUND;
    }

    FILE *file = fopen(wav_path, "rb");
    if (file == NULL) {
        set_last_error("Could not open WAV file: %s", wav_path);
        return ESP_ERR_NOT_FOUND;
    }

    char model_part[256];
    char language_part[128] = "";
    char response_format_part[128];
    char file_header[256];
    char closing_boundary[96];

    esp_err_t err = build_form_field(
        model_part,
        sizeof(model_part),
        "model",
        app_settings_get_stt_model()
    );

    if (err != ESP_OK) {
        fclose(file);
        return err;
    }

    if (!config_string_is_empty(app_settings_get_stt_language_code())) {
        err = build_form_field(
            language_part,
            sizeof(language_part),
            "language",
            app_settings_get_stt_language_code()
        );

        if (err != ESP_OK) {
            fclose(file);
            return err;
        }
    }

    err = build_form_field(
        response_format_part,
        sizeof(response_format_part),
        "response_format",
        "json"
    );

    if (err != ESP_OK) {
        fclose(file);
        return err;
    }

    err = build_file_header(file_header, sizeof(file_header));
    if (err != ESP_OK) {
        fclose(file);
        return err;
    }

    err = build_closing_boundary(closing_boundary, sizeof(closing_boundary));
    if (err != ESP_OK) {
        fclose(file);
        return err;
    }

    size_t total_len =
        strlen(model_part) +
        strlen(language_part) +
        strlen(response_format_part) +
        strlen(file_header) +
        (size_t)st.st_size +
        strlen(closing_boundary);

    if (total_len > INT_MAX) {
        fclose(file);
        set_last_error("WAV upload is too large.");
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(
        TAG,
        "TIMING STT_API request_prepared format=%s audio_bytes=%ld multipart_bytes=%u prepare_ms=%lld total_ms=%lld",
        stt_audio_format_label(),
        (long)st.st_size,
        (unsigned int)total_len,
        (long long)timing_since_ms(api_start_ms),
        (long long)timing_since_ms(api_start_ms)
    );

    char auth_header[512];
    int auth_written = snprintf(
        auth_header,
        sizeof(auth_header),
        "Bearer %s",
        CONFIG_LOOKAI_STT_API_KEY
    );

    if (auth_written < 0 || (size_t)auth_written >= sizeof(auth_header)) {
        fclose(file);
        set_last_error("STT API key is too long for auth header.");
        return ESP_ERR_INVALID_SIZE;
    }

    char content_type[128];
    int content_type_written = snprintf(
        content_type,
        sizeof(content_type),
        "multipart/form-data; boundary=%s",
        STT_API_CLIENT_BOUNDARY
    );

    if (content_type_written < 0 || (size_t)content_type_written >= sizeof(content_type)) {
        fclose(file);
        set_last_error("Could not build multipart Content-Type header.");
        return ESP_ERR_INVALID_SIZE;
    }


    esp_http_client_config_t config = {
        .url = CONFIG_LOOKAI_STT_ENDPOINT,
        .method = HTTP_METHOD_POST,
        .timeout_ms = STT_API_CLIENT_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        fclose(file);
        set_last_error("Could not initialize HTTP client.");
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", content_type);
    esp_http_client_set_header(client, "Accept", "application/json");

    runtime_diag_log("stt_api_before_http_open");
    open_start_ms = timing_now_ms();
    err = esp_http_client_open(client, (int)total_len);
    runtime_diag_log("stt_api_after_http_open");
    ESP_LOGI(
        TAG,
        "TIMING STT_API http_open open_ms=%lld total_ms=%lld result=%s",
        (long long)timing_since_ms(open_start_ms),
        (long long)timing_since_ms(api_start_ms),
        esp_err_to_name(err)
    );
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        fclose(file);
        set_last_error("Could not open HTTP connection: %s", esp_err_to_name(err));
        return err;
    }

    upload_start_ms = timing_now_ms();
    err = write_all(client, model_part, strlen(model_part));
    if (err == ESP_OK && language_part[0] != '\0') {
        err = write_all(client, language_part, strlen(language_part));
    }
    if (err == ESP_OK) {
        err = write_all(client, response_format_part, strlen(response_format_part));
    }
    if (err == ESP_OK) {
        err = write_all(client, file_header, strlen(file_header));
    }

    uint8_t *chunk = NULL;
    if (err == ESP_OK) {
        chunk = (uint8_t *)malloc(STT_API_CLIENT_FILE_CHUNK_SIZE);
        if (chunk == NULL) {
            err = ESP_ERR_NO_MEM;
            set_last_error("Could not allocate WAV upload buffer.");
        }
    }

    uint32_t uploaded_file_bytes = 0;

    while (err == ESP_OK) {
        size_t bytes_read = fread(chunk, 1, STT_API_CLIENT_FILE_CHUNK_SIZE, file);

        if (bytes_read > 0) {
            err = write_all(client, chunk, bytes_read);
            if (err != ESP_OK) {
                break;
            }
            uploaded_file_bytes += (uint32_t)bytes_read;
        }

        if (bytes_read < STT_API_CLIENT_FILE_CHUNK_SIZE) {
            if (ferror(file)) {
                err = ESP_FAIL;
                set_last_error("Could not read WAV file during upload.");
            }
            break;
        }
    }

    if (chunk != NULL) {
        free(chunk);
    }

    if (err == ESP_OK) {
        err = write_all(client, closing_boundary, strlen(closing_boundary));
    }

    fclose(file);
    runtime_diag_log("stt_api_after_upload");
    ESP_LOGI(
        TAG,
        "TIMING STT_API upload upload_ms=%lld audio_uploaded=%lu total_ms=%lld result=%s",
        (long long)timing_since_ms(upload_start_ms),
        (unsigned long)uploaded_file_bytes,
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
        "TIMING STT_API headers headers_ms=%lld status=%d content_length=%lld total_ms=%lld",
        (long long)timing_since_ms(headers_start_ms),
        status_code,
        (long long)content_length,
        (long long)timing_since_ms(api_start_ms)
    );

    runtime_diag_log("stt_api_before_read_response");
    char *response = NULL;
    read_start_ms = timing_now_ms();
    err = read_response_body(client, &response);
    runtime_diag_log("stt_api_after_read_response");
    ESP_LOGI(
        TAG,
        "TIMING STT_API read_response read_ms=%lld response_bytes=%u total_ms=%lld result=%s",
        (long long)timing_since_ms(read_start_ms),
        (unsigned int)(response != NULL ? strlen(response) : 0),
        (long long)timing_since_ms(api_start_ms),
        esp_err_to_name(err)
    );

    esp_http_client_cleanup(client);
    runtime_diag_log("stt_api_after_http_cleanup");

    if (err != ESP_OK) {
        return err;
    }

    if (status_code != 200) {
        set_error_from_api_response(status_code, response);
        free(response);
        ESP_LOGI(
            TAG,
            "TIMING STT_API done total_ms=%lld result=HTTP_%d",
            (long long)timing_since_ms(api_start_ms),
            status_code
        );
        return ESP_FAIL;
    }

    parse_start_ms = timing_now_ms();
    err = parse_transcript_response(response, out_text, out_text_size);
    free(response);
    ESP_LOGI(
        TAG,
        "TIMING STT_API parse parse_ms=%lld text_len=%u total_ms=%lld result=%s",
        (long long)timing_since_ms(parse_start_ms),
        (unsigned int)strlen(out_text),
        (long long)timing_since_ms(api_start_ms),
        esp_err_to_name(err)
    );

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Transcription request completed");
    }

    ESP_LOGI(
        TAG,
        "TIMING STT_API done total_ms=%lld result=%s",
        (long long)timing_since_ms(api_start_ms),
        esp_err_to_name(err)
    );

    return err;
}
