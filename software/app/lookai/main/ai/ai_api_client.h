/**
 * @file ai/ai_api_client.h
 * @brief OpenAI-compatible text prompt API client.
 */

#pragma once

#include <stddef.h>

#include "esp_err.h"

/**
 * @brief Send a user prompt to the configured AI endpoint.
 *
 * The default project configuration uses the OpenAI chat completions endpoint.
 *
 * @param prompt User prompt text.
 * @param out_response Destination buffer for the assistant response.
 * @param out_response_size Size of out_response in bytes.
 *
 * @return ESP_OK on success.
 */
esp_err_t ai_api_client_generate_response(
    const char *prompt,
    char *out_response,
    size_t out_response_size
);

/**
 * @brief Return the last human-readable AI API client error.
 */
const char *ai_api_client_get_last_error(void);
