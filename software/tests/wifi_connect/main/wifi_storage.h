#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#define WIFI_STORAGE_MAX_NETWORKS 5

typedef struct {
    char ssid[33];
    char password[65];
    bool valid;
} wifi_storage_credential_t;

esp_err_t wifi_storage_init(void);

bool wifi_storage_has_any(void);

esp_err_t wifi_storage_add_or_update(
    const char *ssid,
    const char *password
);

esp_err_t wifi_storage_get_all(
    wifi_storage_credential_t *items,
    size_t max_items,
    size_t *out_count
);

esp_err_t wifi_storage_find_by_ssid(
    const char *ssid,
    wifi_storage_credential_t *out
);

esp_err_t wifi_storage_remove_by_ssid(const char *ssid);
esp_err_t wifi_storage_clear_all(void);
