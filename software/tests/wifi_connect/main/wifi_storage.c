/**
 * @file wifi_storage.c
 * @brief NVS-backed multiple saved Wi-Fi credential storage implementation.
 */

#include "wifi_storage.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "wifi_storage";

#define WIFI_STORAGE_NAMESPACE "wifi_creds"

static esp_err_t ensure_nvs_ready(void)
{
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    return err;
}

static void make_key(char *out, size_t out_len, const char *prefix, int index)
{
    snprintf(out, out_len, "%s%d", prefix, index);
}

static esp_err_t load_slot(
    nvs_handle_t handle,
    int index,
    wifi_storage_credential_t *out
)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    char ssid_key[16];
    char pass_key[16];

    make_key(ssid_key, sizeof(ssid_key), "ssid", index);
    make_key(pass_key, sizeof(pass_key), "pass", index);

    size_t ssid_len = sizeof(out->ssid);
    esp_err_t err = nvs_get_str(handle, ssid_key, out->ssid, &ssid_len);
    if (err != ESP_OK) {
        return err;
    }

    size_t pass_len = sizeof(out->password);
    err = nvs_get_str(handle, pass_key, out->password, &pass_len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        out->password[0] = '\0';
        err = ESP_OK;
    }

    if (err == ESP_OK && out->ssid[0] != '\0') {
        out->valid = true;
    }

    return err;
}

static esp_err_t save_slot(
    nvs_handle_t handle,
    int index,
    const char *ssid,
    const char *password
)
{
    char ssid_key[16];
    char pass_key[16];

    make_key(ssid_key, sizeof(ssid_key), "ssid", index);
    make_key(pass_key, sizeof(pass_key), "pass", index);

    esp_err_t err = nvs_set_str(handle, ssid_key, ssid);
    if (err != ESP_OK) {
        return err;
    }

    return nvs_set_str(handle, pass_key, password != NULL ? password : "");
}

static esp_err_t erase_slot(nvs_handle_t handle, int index)
{
    char ssid_key[16];
    char pass_key[16];

    make_key(ssid_key, sizeof(ssid_key), "ssid", index);
    make_key(pass_key, sizeof(pass_key), "pass", index);

    esp_err_t ssid_err = nvs_erase_key(handle, ssid_key);
    esp_err_t pass_err = nvs_erase_key(handle, pass_key);

    if (ssid_err == ESP_ERR_NVS_NOT_FOUND) {
        ssid_err = ESP_OK;
    }

    if (pass_err == ESP_ERR_NVS_NOT_FOUND) {
        pass_err = ESP_OK;
    }

    if (ssid_err != ESP_OK) {
        return ssid_err;
    }

    return pass_err;
}

static void compact_saved_networks(nvs_handle_t handle)
{
    wifi_storage_credential_t items[WIFI_STORAGE_MAX_NETWORKS] = {0};
    int count = 0;

    for (int i = 0; i < WIFI_STORAGE_MAX_NETWORKS; i++) {
        wifi_storage_credential_t item;
        if (load_slot(handle, i, &item) == ESP_OK && item.valid) {
            items[count++] = item;
        }
    }

    for (int i = 0; i < WIFI_STORAGE_MAX_NETWORKS; i++) {
        erase_slot(handle, i);
    }

    for (int i = 0; i < count; i++) {
        save_slot(handle, i, items[i].ssid, items[i].password);
    }
}

static void migrate_legacy_single_network(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_STORAGE_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return;
    }

    wifi_storage_credential_t first;
    if (load_slot(handle, 0, &first) == ESP_OK && first.valid) {
        nvs_close(handle);
        return;
    }

    char legacy_ssid[33] = {0};
    char legacy_password[65] = {0};
    size_t ssid_len = sizeof(legacy_ssid);

    err = nvs_get_str(handle, "ssid", legacy_ssid, &ssid_len);
    if (err != ESP_OK || legacy_ssid[0] == '\0') {
        nvs_close(handle);
        return;
    }

    size_t pass_len = sizeof(legacy_password);
    err = nvs_get_str(handle, "password", legacy_password, &pass_len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        legacy_password[0] = '\0';
        err = ESP_OK;
    }

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Migrating legacy saved Wi-Fi credential: %s", legacy_ssid);
        save_slot(handle, 0, legacy_ssid, legacy_password);
        nvs_erase_key(handle, "ssid");
        nvs_erase_key(handle, "password");
        nvs_commit(handle);
    }

    nvs_close(handle);
}

esp_err_t wifi_storage_init(void)
{
    esp_err_t err = ensure_nvs_ready();
    if (err == ESP_OK) {
        migrate_legacy_single_network();
    }

    return err;
}

bool wifi_storage_has_any(void)
{
    wifi_storage_credential_t items[WIFI_STORAGE_MAX_NETWORKS] = {0};
    size_t count = 0;

    esp_err_t err = wifi_storage_get_all(
        items,
        WIFI_STORAGE_MAX_NETWORKS,
        &count
    );

    return err == ESP_OK && count > 0;
}

esp_err_t wifi_storage_add_or_update(
    const char *ssid,
    const char *password
)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_ERROR_CHECK(ensure_nvs_ready());

    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_STORAGE_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    int first_empty = -1;

    for (int i = 0; i < WIFI_STORAGE_MAX_NETWORKS; i++) {
        wifi_storage_credential_t item;

        err = load_slot(handle, i, &item);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            if (first_empty < 0) {
                first_empty = i;
            }
            continue;
        }

        if (err != ESP_OK) {
            nvs_close(handle);
            return err;
        }

        if (item.valid && strcmp(item.ssid, ssid) == 0) {
            err = save_slot(handle, i, ssid, password);
            if (err == ESP_OK) {
                err = nvs_commit(handle);
            }
            nvs_close(handle);

            ESP_LOGI(TAG, "Updated saved Wi-Fi network: %s", ssid);
            return err;
        }
    }

    if (first_empty < 0) {
        nvs_close(handle);
        ESP_LOGW(TAG, "Saved Wi-Fi list is full");
        return ESP_ERR_NO_MEM;
    }

    err = save_slot(handle, first_empty, ssid, password);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Added saved Wi-Fi network: %s", ssid);
    }

    return err;
}

esp_err_t wifi_storage_get_all(
    wifi_storage_credential_t *items,
    size_t max_items,
    size_t *out_count
)
{
    if (items == NULL || out_count == NULL || max_items == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_count = 0;
    memset(items, 0, sizeof(wifi_storage_credential_t) * max_items);

    ESP_ERROR_CHECK(ensure_nvs_ready());

    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_STORAGE_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            return ESP_OK;
        }

        return err;
    }

    for (int i = 0; i < WIFI_STORAGE_MAX_NETWORKS && *out_count < max_items; i++) {
        wifi_storage_credential_t item;
        err = load_slot(handle, i, &item);

        if (err == ESP_ERR_NVS_NOT_FOUND) {
            continue;
        }

        if (err != ESP_OK) {
            nvs_close(handle);
            return err;
        }

        if (item.valid) {
            items[*out_count] = item;
            (*out_count)++;
        }
    }

    nvs_close(handle);

    return ESP_OK;
}

esp_err_t wifi_storage_find_by_ssid(
    const char *ssid,
    wifi_storage_credential_t *out
)
{
    if (ssid == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_storage_credential_t items[WIFI_STORAGE_MAX_NETWORKS] = {0};
    size_t count = 0;

    esp_err_t err = wifi_storage_get_all(
        items,
        WIFI_STORAGE_MAX_NETWORKS,
        &count
    );

    if (err != ESP_OK) {
        return err;
    }

    for (size_t i = 0; i < count; i++) {
        if (strcmp(items[i].ssid, ssid) == 0) {
            *out = items[i];
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t wifi_storage_remove_by_ssid(const char *ssid)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_ERROR_CHECK(ensure_nvs_ready());

    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_STORAGE_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    bool removed = false;

    for (int i = 0; i < WIFI_STORAGE_MAX_NETWORKS; i++) {
        wifi_storage_credential_t item;
        err = load_slot(handle, i, &item);

        if (err == ESP_ERR_NVS_NOT_FOUND) {
            continue;
        }

        if (err != ESP_OK) {
            nvs_close(handle);
            return err;
        }

        if (item.valid && strcmp(item.ssid, ssid) == 0) {
            err = erase_slot(handle, i);
            if (err != ESP_OK) {
                nvs_close(handle);
                return err;
            }

            removed = true;
            break;
        }
    }

    if (removed) {
        compact_saved_networks(handle);
        err = nvs_commit(handle);
        ESP_LOGI(TAG, "Removed saved Wi-Fi network: %s", ssid);
    } else {
        err = ESP_ERR_NOT_FOUND;
    }

    nvs_close(handle);

    return err;
}

esp_err_t wifi_storage_clear_all(void)
{
    ESP_ERROR_CHECK(ensure_nvs_ready());

    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_STORAGE_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    for (int i = 0; i < WIFI_STORAGE_MAX_NETWORKS; i++) {
        erase_slot(handle, i);
    }

    nvs_erase_key(handle, "ssid");
    nvs_erase_key(handle, "password");

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Cleared all saved Wi-Fi networks");
    }

    return err;
}
