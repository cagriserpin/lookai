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
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "runtime_diag.h"
#include "wifi_ap.h"

static const char *TAG = "stt_api_client";

#define STT_API_CLIENT_BOUNDARY "----LookAIFormBoundary7MA4YWxkTrZu0gW"
#define STT_API_CLIENT_TIMEOUT_MS 60000
#define STT_API_CLIENT_FILE_CHUNK_SIZE 1024
#define STT_API_CLIENT_RESPONSE_MAX_BYTES 8192
#define STT_API_CLIENT_CONNECT_RETRY_COUNT 3
#define STT_API_CLIENT_CONNECT_RETRY_DELAY_MS 1200

static char s_last_error[192] = "";

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
        "Content-Disposition: form-data; name=\"file\"; filename=\"stt_last.wav\"\r\n"
        "Content-Type: audio/wav\r\n"
        "\r\n",
        STT_API_CLIENT_BOUNDARY
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

    out_text[0] = '\0';
    s_last_error[0] = '\0';

    if (config_string_is_empty(CONFIG_LOOKAI_STT_API_KEY)) {
        set_last_error("STT API key is not configured.");
        return ESP_ERR_INVALID_STATE;
    }

    if (config_string_is_empty(CONFIG_LOOKAI_STT_ENDPOINT)) {
        set_last_error("STT endpoint is not configured.");
        return ESP_ERR_INVALID_STATE;
    }

    if (config_string_is_empty(CONFIG_LOOKAI_STT_MODEL)) {
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
        CONFIG_LOOKAI_STT_MODEL
    );

    if (err != ESP_OK) {
        fclose(file);
        return err;
    }

    if (!config_string_is_empty(CONFIG_LOOKAI_STT_LANGUAGE)) {
        err = build_form_field(
            language_part,
            sizeof(language_part),
            "language",
            CONFIG_LOOKAI_STT_LANGUAGE
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

    esp_http_client_handle_t client = NULL;
    err = ESP_FAIL;

    for (int attempt = 1; attempt <= STT_API_CLIENT_CONNECT_RETRY_COUNT; attempt++) {
        client = esp_http_client_init(&config);
        if (client == NULL) {
            err = ESP_FAIL;
            set_last_error("Could not initialize HTTP client.");
            break;
        }

        esp_http_client_set_header(client, "Authorization", auth_header);
        esp_http_client_set_header(client, "Content-Type", content_type);
        esp_http_client_set_header(client, "Accept", "application/json");
        esp_http_client_set_header(client, "Connection", "close");

        wifi_ap_log_sta_status("stt_api_before_http_open_link");
        ESP_LOGI(TAG, "Opening STT HTTP connection, attempt %d/%d", attempt, STT_API_CLIENT_CONNECT_RETRY_COUNT);
        runtime_diag_log("stt_api_before_http_open");
        err = esp_http_client_open(client, (int)total_len);
        runtime_diag_log("stt_api_after_http_open");

        if (err == ESP_OK) {
            break;
        }

        ESP_LOGW(
            TAG,
            "STT HTTP open attempt %d/%d failed: %s",
            attempt,
            STT_API_CLIENT_CONNECT_RETRY_COUNT,
            esp_err_to_name(err)
        );
        wifi_ap_log_sta_status("stt_api_http_open_failed_link");

        esp_http_client_cleanup(client);
        client = NULL;

        if (attempt < STT_API_CLIENT_CONNECT_RETRY_COUNT) {
            vTaskDelay(pdMS_TO_TICKS(STT_API_CLIENT_CONNECT_RETRY_DELAY_MS * attempt));
        }
    }

    if (err != ESP_OK) {
        fclose(file);
        set_last_error("Could not open HTTP connection after retries: %s", esp_err_to_name(err));
        return err;
    }

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

    while (err == ESP_OK) {
        size_t bytes_read = fread(chunk, 1, STT_API_CLIENT_FILE_CHUNK_SIZE, file);

        if (bytes_read > 0) {
            err = write_all(client, chunk, bytes_read);
            if (err != ESP_OK) {
                break;
            }
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

    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);

    runtime_diag_log("stt_api_before_read_response");
    char *response = NULL;
    err = read_response_body(client, &response);
    runtime_diag_log("stt_api_after_read_response");

    esp_http_client_cleanup(client);
    runtime_diag_log("stt_api_after_http_cleanup");

    if (err != ESP_OK) {
        return err;
    }

    if (status_code != 200) {
        set_error_from_api_response(status_code, response);
        free(response);
        return ESP_FAIL;
    }

    err = parse_transcript_response(response, out_text, out_text_size);
    free(response);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Transcription request completed");
    }

    return err;
}
