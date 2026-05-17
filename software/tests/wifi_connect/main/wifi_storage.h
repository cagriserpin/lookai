/**
 * @file wifi_storage.h
 * @brief NVS-backed multiple saved Wi-Fi credential storage API.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#define WIFI_STORAGE_MAX_NETWORKS 5

/**
 * @brief Saved Wi-Fi credential item.
 */
typedef struct {
    char ssid[33];      /**< Saved SSID. */
    char password[65];  /**< Saved password, empty for open networks. */
    bool valid;         /**< True when this item contains a valid SSID. */
} wifi_storage_credential_t;

/**
 * @brief Initialize NVS and migrate legacy single-network storage if needed.
 */
esp_err_t wifi_storage_init(void);

/**
 * @brief Return true when at least one network is saved.
 */
bool wifi_storage_has_any(void);

/**
 * @brief Add a new network or update an existing network password.
 */
esp_err_t wifi_storage_add_or_update(const char *ssid, const char *password);

/**
 * @brief Load all saved networks.
 */
esp_err_t wifi_storage_get_all(
    wifi_storage_credential_t *items,
    size_t max_items,
    size_t *out_count
);

/**
 * @brief Find a saved network by SSID.
 */
esp_err_t wifi_storage_find_by_ssid(
    const char *ssid,
    wifi_storage_credential_t *out
);

/**
 * @brief Remove a saved network by SSID.
 */
esp_err_t wifi_storage_remove_by_ssid(const char *ssid);

/**
 * @brief Remove all saved networks.
 */
esp_err_t wifi_storage_clear_all(void);
