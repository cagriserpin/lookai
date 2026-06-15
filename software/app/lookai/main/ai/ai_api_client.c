/**
 * @file ai/ai_api_client.c
 * @brief OpenAI-compatible text prompt API client implementation.
 */

#include "ai_api_client.h"

#include <limits.h>
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

#include "runtime_diag.h"

static const char *TAG = "ai_api_client";

#define AI_API_CLIENT_TIMEOUT_MS 60000
#define AI_API_CLIENT_RESPONSE_MAX_BYTES 4096
#define AI_API_CLIENT_TX_BUFFER_SIZE 1024
#define AI_API_CLIENT_RX_BUFFER_SIZE 1024

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

const char *ai_api_client_get_last_error(void)
{
    return s_last_error[0] != '\0' ? s_last_error : "No AI API client error.";
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
    if (!config_string_is_empty(CONFIG_LOOKAI_AI_API_KEY)) {
        return CONFIG_LOOKAI_AI_API_KEY;
    }

    return CONFIG_LOOKAI_STT_API_KEY;
}

static size_t json_escaped_len(const char *text)
{
    if (text == NULL) {
        return 0;
    }

    size_t len = 0;
    const unsigned char *cursor = (const unsigned char *)text;
    while (*cursor != '\0') {
        unsigned char c = *cursor++;
        switch (c) {
        case '"':
        case '\\':
        case '\b':
        case '\f':
        case '\n':
        case '\r':
        case '\t':
            len += 2;
            break;
        default:
            len += c < 0x20 ? 6 : 1;
            break;
        }
    }
    return len;
}

static char *json_write_escaped(char *out, const char *text)
{
    static const char hex[] = "0123456789abcdef";

    if (text == NULL) {
        return out;
    }

    const unsigned char *cursor = (const unsigned char *)text;
    while (*cursor != '\0') {
        unsigned char c = *cursor++;
        switch (c) {
        case '"':
            *out++ = '\\';
            *out++ = '"';
            break;
        case '\\':
            *out++ = '\\';
            *out++ = '\\';
            break;
        case '\b':
            *out++ = '\\';
            *out++ = 'b';
            break;
        case '\f':
            *out++ = '\\';
            *out++ = 'f';
            break;
        case '\n':
            *out++ = '\\';
            *out++ = 'n';
            break;
        case '\r':
            *out++ = '\\';
            *out++ = 'r';
            break;
        case '\t':
            *out++ = '\\';
            *out++ = 't';
            break;
        default:
            if (c < 0x20) {
                *out++ = '\\';
                *out++ = 'u';
                *out++ = '0';
                *out++ = '0';
                *out++ = hex[(c >> 4) & 0x0f];
                *out++ = hex[c & 0x0f];
            } else {
                *out++ = (char)c;
            }
            break;
        }
    }
    return out;
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
            set_last_error("AI HTTP write failed.");
            return ESP_FAIL;
        }

        if (written == 0) {
            set_last_error("AI HTTP write stalled.");
            return ESP_ERR_TIMEOUT;
        }

        cursor += written;
        remaining -= (size_t)written;
    }

    return ESP_OK;
}

static esp_err_t read_response_body(
    esp_http_client_handle_t client,
    char **out_response
)
{
    if (client == NULL || out_response == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char *response = (char *)calloc(1, AI_API_CLIENT_RESPONSE_MAX_BYTES + 1);
    if (response == NULL) {
        set_last_error("Could not allocate AI response buffer.");
        return ESP_ERR_NO_MEM;
    }

    size_t total = 0;

    while (total < AI_API_CLIENT_RESPONSE_MAX_BYTES) {
        int read_len = esp_http_client_read(
            client,
            response + total,
            AI_API_CLIENT_RESPONSE_MAX_BYTES - total
        );

        if (read_len < 0) {
            free(response);
            set_last_error("AI HTTP response read failed.");
            return ESP_FAIL;
        }

        if (read_len == 0) {
            break;
        }

        total += (size_t)read_len;
    }

    response[total] = '\0';
    *out_response = response;

    if (total == AI_API_CLIENT_RESPONSE_MAX_BYTES) {
        ESP_LOGW(TAG, "AI HTTP response reached local buffer limit");
    }

    return ESP_OK;
}

static esp_err_t build_request_body(const char *prompt, char **out_body)
{
    if (prompt == NULL || prompt[0] == '\0' || out_body == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char max_tokens_text[16];
    int max_tokens_written = snprintf(
        max_tokens_text,
        sizeof(max_tokens_text),
        "%d",
        CONFIG_LOOKAI_AI_MAX_TOKENS
    );
    if (max_tokens_written < 0 || (size_t)max_tokens_written >= sizeof(max_tokens_text)) {
        set_last_error("Could not serialize AI max_tokens.");
        return ESP_ERR_INVALID_SIZE;
    }

    const char *model = CONFIG_LOOKAI_AI_MODEL;
    const char *system_prompt = CONFIG_LOOKAI_AI_SYSTEM_PROMPT;

    size_t body_size =
        160 +
        json_escaped_len(model) +
        json_escaped_len(system_prompt) +
        json_escaped_len(prompt) +
        strlen(max_tokens_text);

    char *body = (char *)malloc(body_size);
    if (body == NULL) {
        set_last_error("Could not allocate AI request JSON.");
        return ESP_ERR_NO_MEM;
    }

    char *cursor = body;
    char *end = body + body_size;

#define APPEND_LITERAL(lit) do { \
        const size_t _len = sizeof(lit) - 1; \
        if ((size_t)(end - cursor) <= _len) { \
            free(body); \
            set_last_error("AI request JSON buffer was too small."); \
            return ESP_ERR_INVALID_SIZE; \
        } \
        memcpy(cursor, (lit), _len); \
        cursor += _len; \
    } while (0)

#define APPEND_ESCAPED(value) do { \
        cursor = json_write_escaped(cursor, (value)); \
        if (cursor >= end) { \
            free(body); \
            set_last_error("AI request JSON buffer overflow."); \
            return ESP_ERR_INVALID_SIZE; \
        } \
    } while (0)

#define APPEND_TEXT(text_value) do { \
        const char *_text = (text_value); \
        const size_t _len = strlen(_text); \
        if ((size_t)(end - cursor) <= _len) { \
            free(body); \
            set_last_error("AI request JSON buffer was too small."); \
            return ESP_ERR_INVALID_SIZE; \
        } \
        memcpy(cursor, _text, _len); \
        cursor += _len; \
    } while (0)

    APPEND_LITERAL("{\"model\":\"");
    APPEND_ESCAPED(model);
    APPEND_LITERAL("\",\"max_tokens\":");
    APPEND_TEXT(max_tokens_text);
    APPEND_LITERAL(",\"temperature\":0.4,\"messages\":[{\"role\":\"system\",\"content\":\"");
    APPEND_ESCAPED(system_prompt);
    APPEND_LITERAL("\"},{\"role\":\"user\",\"content\":\"");
    APPEND_ESCAPED(prompt);
    APPEND_LITERAL("\"}]}");

#undef APPEND_LITERAL
#undef APPEND_ESCAPED
#undef APPEND_TEXT

    *cursor = '\0';
    *out_body = body;
    return ESP_OK;
}

static void set_error_from_api_response(int status_code, const char *response)
{
    if (response == NULL || response[0] == '\0') {
        set_last_error("AI request failed with HTTP %d.", status_code);
        return;
    }

    cJSON *root = cJSON_Parse(response);
    if (root == NULL) {
        set_last_error("AI request failed with HTTP %d.", status_code);
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
        set_last_error("AI request failed with HTTP %d.", status_code);
    }

    cJSON_Delete(root);
}

static const char *extract_response_text(cJSON *root)
{
    if (root == NULL) {
        return NULL;
    }

    cJSON *choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
    if (!cJSON_IsArray(choices) || cJSON_GetArraySize(choices) <= 0) {
        return NULL;
    }

    cJSON *first_choice = cJSON_GetArrayItem(choices, 0);
    if (!cJSON_IsObject(first_choice)) {
        return NULL;
    }

    cJSON *message = cJSON_GetObjectItemCaseSensitive(first_choice, "message");
    if (cJSON_IsObject(message)) {
        cJSON *content = cJSON_GetObjectItemCaseSensitive(message, "content");
        if (cJSON_IsString(content) && content->valuestring != NULL) {
            return content->valuestring;
        }
    }

    cJSON *text = cJSON_GetObjectItemCaseSensitive(first_choice, "text");
    if (cJSON_IsString(text) && text->valuestring != NULL) {
        return text->valuestring;
    }

    return NULL;
}

static esp_err_t parse_ai_response(
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
        set_last_error("Could not parse AI JSON response.");
        return ESP_FAIL;
    }

    const char *content = extract_response_text(root);
    if (content == NULL || content[0] == '\0') {
        cJSON_Delete(root);
        set_last_error("AI response did not include assistant text.");
        return ESP_ERR_NOT_FOUND;
    }

    size_t max_chars = (size_t)CONFIG_LOOKAI_AI_MAX_OUTPUT_CHARS;
    if (max_chars == 0 || max_chars >= out_text_size) {
        max_chars = out_text_size - 1;
    }

    size_t copy_len = strnlen(content, max_chars);
    bool truncated = content[copy_len] != '\0';

    memcpy(out_text, content, copy_len);
    out_text[copy_len] = '\0';

    cJSON_Delete(root);

    if (truncated) {
        ESP_LOGW(TAG, "AI response truncated to %u bytes", (unsigned int)copy_len);
    }

    return ESP_OK;
}

esp_err_t ai_api_client_generate_response(
    const char *prompt,
    char *out_response,
    size_t out_response_size
)
{
    if (prompt == NULL || prompt[0] == '\0' || out_response == NULL || out_response_size == 0) {
        set_last_error("Invalid AI API client arguments.");
        return ESP_ERR_INVALID_ARG;
    }

    int64_t api_start_ms = timing_now_ms();
    int64_t body_start_ms = 0;
    int64_t open_start_ms = 0;
    int64_t upload_start_ms = 0;
    int64_t headers_start_ms = 0;
    int64_t read_start_ms = 0;
    int64_t parse_start_ms = 0;

    out_response[0] = '\0';
    s_last_error[0] = '\0';
    ESP_LOGI(TAG, "TIMING AI_API start total_ms=0");

    const char *api_key = select_api_key();
    if (config_string_is_empty(api_key)) {
        set_last_error("AI API key is not configured.");
        return ESP_ERR_INVALID_STATE;
    }

    if (config_string_is_empty(CONFIG_LOOKAI_AI_ENDPOINT)) {
        set_last_error("AI endpoint is not configured.");
        return ESP_ERR_INVALID_STATE;
    }

    if (config_string_is_empty(CONFIG_LOOKAI_AI_MODEL)) {
        set_last_error("AI model is not configured.");
        return ESP_ERR_INVALID_STATE;
    }

    char *request_body = NULL;
    body_start_ms = timing_now_ms();
    esp_err_t err = build_request_body(prompt, &request_body);
    if (err != ESP_OK) {
        return err;
    }

    size_t request_body_len = strlen(request_body);
    if (request_body_len > INT_MAX) {
        free(request_body);
        set_last_error("AI request body is too large.");
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(
        TAG,
        "TIMING AI_API body_ready body_ms=%lld prompt_len=%u body_bytes=%u total_ms=%lld",
        (long long)timing_since_ms(body_start_ms),
        (unsigned int)strlen(prompt),
        (unsigned int)request_body_len,
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
        set_last_error("AI API key is too long for auth header.");
        return ESP_ERR_INVALID_SIZE;
    }

    esp_http_client_config_t config = {
        .url = CONFIG_LOOKAI_AI_ENDPOINT,
        .method = HTTP_METHOD_POST,
        .timeout_ms = AI_API_CLIENT_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = AI_API_CLIENT_RX_BUFFER_SIZE,
        .buffer_size_tx = AI_API_CLIENT_TX_BUFFER_SIZE,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        free(request_body);
        set_last_error("Could not initialize AI HTTP client.");
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Accept", "application/json");

    runtime_diag_log("ai_api_before_http_open");
    open_start_ms = timing_now_ms();
    err = esp_http_client_open(client, (int)request_body_len);
    runtime_diag_log("ai_api_after_http_open");
    ESP_LOGI(
        TAG,
        "TIMING AI_API http_open open_ms=%lld total_ms=%lld result=%s",
        (long long)timing_since_ms(open_start_ms),
        (long long)timing_since_ms(api_start_ms),
        esp_err_to_name(err)
    );
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        free(request_body);
        set_last_error("Could not open AI HTTP connection: %s", esp_err_to_name(err));
        return err;
    }

    upload_start_ms = timing_now_ms();
    err = write_all(client, request_body, request_body_len);
    free(request_body);
    runtime_diag_log("ai_api_after_upload");
    ESP_LOGI(
        TAG,
        "TIMING AI_API upload upload_ms=%lld body_bytes=%u total_ms=%lld result=%s",
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
        "TIMING AI_API headers headers_ms=%lld status=%d content_length=%lld total_ms=%lld",
        (long long)timing_since_ms(headers_start_ms),
        status_code,
        (long long)content_length,
        (long long)timing_since_ms(api_start_ms)
    );

    char *response = NULL;
    read_start_ms = timing_now_ms();
    err = read_response_body(client, &response);
    runtime_diag_log("ai_api_after_read_response");
    ESP_LOGI(
        TAG,
        "TIMING AI_API read_response read_ms=%lld response_bytes=%u total_ms=%lld result=%s",
        (long long)timing_since_ms(read_start_ms),
        (unsigned int)(response != NULL ? strlen(response) : 0),
        (long long)timing_since_ms(api_start_ms),
        esp_err_to_name(err)
    );

    esp_http_client_cleanup(client);
    runtime_diag_log("ai_api_after_http_cleanup");

    if (err != ESP_OK) {
        return err;
    }

    if (status_code != 200) {
        set_error_from_api_response(status_code, response);
        free(response);
        ESP_LOGI(
            TAG,
            "TIMING AI_API done total_ms=%lld result=HTTP_%d",
            (long long)timing_since_ms(api_start_ms),
            status_code
        );
        return ESP_FAIL;
    }

    parse_start_ms = timing_now_ms();
    err = parse_ai_response(response, out_response, out_response_size);
    free(response);
    ESP_LOGI(
        TAG,
        "TIMING AI_API parse parse_ms=%lld text_len=%u total_ms=%lld result=%s",
        (long long)timing_since_ms(parse_start_ms),
        (unsigned int)strlen(out_response),
        (long long)timing_since_ms(api_start_ms),
        esp_err_to_name(err)
    );

    ESP_LOGI(
        TAG,
        "TIMING AI_API done total_ms=%lld result=%s",
        (long long)timing_since_ms(api_start_ms),
        esp_err_to_name(err)
    );

    return err;
}
