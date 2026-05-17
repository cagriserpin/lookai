/**
 * @file app/app_controller.h
 * @brief Top-level application controller API.
 */

#pragma once

#include "esp_err.h"

/**
 * @brief Initialize and start the application.
 *
 * This initializes the display UI, saved Wi-Fi storage, callback wiring, and
 * the Wi-Fi manager background workflow.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t app_controller_start(void);
