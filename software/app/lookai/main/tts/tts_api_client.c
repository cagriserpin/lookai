/**
 * @file tts/tts_api_client.c
 * @brief OpenAI text-to-speech API client.
 */

#include "tts_api_client.h"
#include "runtime_diag.h"
#include "wifi_ap.h"

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
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "tts_api_client";

#define TTS_API_CLIENT_TIMEOUT_MS 60000
#define TTS_API_CLIENT_CHUNK_SIZE 1024
#define TTS_API_CLIENT_ERROR_RESPONSE_MAX_BYTES 2048
#define TTS_API_CLIENT_MAX_INPUT_CHARS 800
#define TTS_API_CLIENT_CONNECT_RETRY_COUNT 3
#define TTS_API_CLIENT_CONNECT_RETRY_DELAY_MS 1200

static char s_last_error[192] = "";

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

static esp_err_t build_request_body(const char *text, char **out_body)
{
    if (text == NULL || out_body == NULL) {
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
    cJSON_AddStringToObject(root, "response_format", CONFIG_LOOKAI_TTS_RESPONSE_FORMAT);

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
    uint32_t *out_wav_bytes
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

    return ESP_OK;
}

esp_err_t tts_api_client_generate_wav(
    const char *text,
    const char *out_wav_path,
    uint32_t *out_wav_bytes
)
{
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

    if (config_string_is_empty(CONFIG_LOOKAI_TTS_RESPONSE_FORMAT)) {
        set_last_error("TTS response format is not configured.");
        return ESP_ERR_INVALID_STATE;
    }

    char *request_body = NULL;
    esp_err_t err = build_request_body(text, &request_body);
    if (err != ESP_OK) {
        return err;
    }

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

    esp_http_client_handle_t client = NULL;
    err = ESP_FAIL;

    for (int attempt = 1; attempt <= TTS_API_CLIENT_CONNECT_RETRY_COUNT; attempt++) {
        client = esp_http_client_init(&config);
        if (client == NULL) {
            err = ESP_FAIL;
            set_last_error("Could not initialize TTS HTTP client.");
            break;
        }

        esp_http_client_set_header(client, "Authorization", auth_header);
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_header(client, "Accept", "audio/wav");
        esp_http_client_set_header(client, "Connection", "close");

        wifi_ap_log_sta_status("tts_api_before_http_open_link");
        ESP_LOGI(TAG, "Opening TTS HTTP connection, attempt %d/%d", attempt, TTS_API_CLIENT_CONNECT_RETRY_COUNT);
        runtime_diag_log("tts_api_before_http_open");
        err = esp_http_client_open(client, strlen(request_body));
        runtime_diag_log("tts_api_after_http_open");

        if (err == ESP_OK) {
            break;
        }

        ESP_LOGW(
            TAG,
            "TTS HTTP open attempt %d/%d failed: %s",
            attempt,
            TTS_API_CLIENT_CONNECT_RETRY_COUNT,
            esp_err_to_name(err)
        );
        wifi_ap_log_sta_status("tts_api_http_open_failed_link");

        esp_http_client_cleanup(client);
        client = NULL;

        if (attempt < TTS_API_CLIENT_CONNECT_RETRY_COUNT) {
            vTaskDelay(pdMS_TO_TICKS(TTS_API_CLIENT_CONNECT_RETRY_DELAY_MS * attempt));
        }
    }

    if (err != ESP_OK) {
        free(request_body);
        set_last_error("Could not open TTS HTTP connection after retries: %s", esp_err_to_name(err));
        return err;
    }

    err = write_all(client, request_body, strlen(request_body));
    free(request_body);
    runtime_diag_log("tts_api_after_upload");

    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);

    if (status_code != 200) {
        char response[TTS_API_CLIENT_ERROR_RESPONSE_MAX_BYTES + 1];
        response[0] = '\0';
        read_error_response(client, response, sizeof(response));
        set_error_from_api_response(status_code, response);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    err = save_wav_response(client, out_wav_path, out_wav_bytes);
    runtime_diag_log("tts_api_after_save_wav");

    esp_http_client_cleanup(client);
    runtime_diag_log("tts_api_after_http_cleanup");

    return err;
}
