/**
 * @file wifi/wifi_ap.c
 * @brief Low-level ESP-IDF Wi-Fi AP/STA wrapper implementation.
 */

#include "wifi_ap.h"

#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "runtime_diag.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "wifi_ap";

#define WIFI_AP_SSID      "LookAI-Setup"
#define WIFI_AP_PASSWORD  "12345678"
#define WIFI_AP_CHANNEL   6
#define WIFI_AP_MAX_CONN  4

#define WIFI_STA_MAX_RETRY 5
#define WIFI_SCAN_TASK_STACK_SIZE 4096

static esp_netif_t *s_ap_netif = NULL;
static esp_netif_t *s_sta_netif = NULL;

static wifi_ap_event_cb_t s_event_cb = NULL;

static bool s_wifi_initialized = false;
static bool s_wifi_started = false;
static bool s_setup_ap_enabled = false;

static bool s_sta_connecting = false;
static bool s_sta_connected = false;
static bool s_sta_reconfiguring = false;
static int s_sta_retry_count = 0;

static char s_sta_ssid[33] = {0};
static char s_sta_ip[16] = {0};

static wifi_ap_scan_result_t s_scan_cache[WIFI_AP_SCAN_MAX_RESULTS] = {0};
static uint16_t s_scan_cache_count = 0;
static bool s_scan_cache_valid = false;
static bool s_scan_in_progress = false;
static TaskHandle_t s_scan_task_handle = NULL;

static void wifi_scan_task(void *arg);

void wifi_ap_set_event_cb(wifi_ap_event_cb_t cb)
{
    s_event_cb = cb;
}

static void emit_event(wifi_ap_event_t event)
{
    if (s_event_cb != NULL) {
        s_event_cb(event);
    }
}

static void disable_wifi_power_save(const char *point)
{
    esp_err_t err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "%s: Wi-Fi power save disabled", point != NULL ? point : "wifi");
    } else {
        ESP_LOGW(
            TAG,
            "%s: could not disable Wi-Fi power save: %s",
            point != NULL ? point : "wifi",
            esp_err_to_name(err)
        );
    }
}

void wifi_ap_log_sta_status(const char *point)
{
    const char *name = point != NULL ? point : "wifi_sta_status";

    esp_netif_ip_info_t ip_info = {0};
    esp_err_t ip_err = ESP_ERR_INVALID_STATE;
    if (s_sta_netif != NULL) {
        ip_err = esp_netif_get_ip_info(s_sta_netif, &ip_info);
    }

    wifi_ap_record_t ap_info = {0};
    esp_err_t ap_err = ESP_FAIL;
    if (s_wifi_started) {
        ap_err = esp_wifi_sta_get_ap_info(&ap_info);
    }

    wifi_ps_type_t ps_type = WIFI_PS_NONE;
    esp_err_t ps_err = ESP_FAIL;
    if (s_wifi_started) {
        ps_err = esp_wifi_get_ps(&ps_type);
    }

    ESP_LOGI(
        TAG,
        "%s: started=%d connected=%d connecting=%d ssid=%s ip=" IPSTR " gw=" IPSTR " ap_info=%s rssi=%d ps=%s(%d)",
        name,
        s_wifi_started ? 1 : 0,
        s_sta_connected ? 1 : 0,
        s_sta_connecting ? 1 : 0,
        s_sta_ssid[0] != '\0' ? s_sta_ssid : "-",
        IP2STR(&ip_info.ip),
        IP2STR(&ip_info.gw),
        ap_err == ESP_OK ? "ok" : esp_err_to_name(ap_err),
        ap_err == ESP_OK ? ap_info.rssi : 0,
        ps_err == ESP_OK ? "ok" : esp_err_to_name(ps_err),
        ps_err == ESP_OK ? (int)ps_type : -1
    );

    if (ip_err != ESP_OK) {
        ESP_LOGW(TAG, "%s: esp_netif_get_ip_info failed: %s", name, esp_err_to_name(ip_err));
    }
}

static esp_err_t init_nvs(void)
{
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    return err;
}

static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_AP_STACONNECTED:
                ESP_LOGI(TAG, "Phone/client connected to SoftAP");
                emit_event(WIFI_AP_EVENT_CLIENT_CONNECTED);
                break;

            case WIFI_EVENT_AP_STADISCONNECTED:
                ESP_LOGI(TAG, "Phone/client disconnected from SoftAP");
                emit_event(WIFI_AP_EVENT_CLIENT_DISCONNECTED);
                break;

            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGW(TAG, "STA disconnected");

                if (s_sta_reconfiguring) {
                    ESP_LOGI(TAG, "STA disconnect is part of network reconfiguration");
                    s_sta_reconfiguring = false;
                    s_sta_connected = false;
                    s_sta_ip[0] = '\0';
                    break;
                }

                if (s_sta_connected) {
                    s_sta_connected = false;
                    s_sta_connecting = false;
                    s_sta_ip[0] = '\0';
                    emit_event(WIFI_AP_EVENT_STA_DISCONNECTED);
                    break;
                }

                if (s_sta_connecting && s_sta_retry_count < WIFI_STA_MAX_RETRY) {
                    s_sta_retry_count++;
                    ESP_LOGI(TAG, "Retrying STA connection, attempt %d/%d", s_sta_retry_count, WIFI_STA_MAX_RETRY);
                    esp_wifi_connect();
                    emit_event(WIFI_AP_EVENT_STA_CONNECTING);
                } else if (s_sta_connecting) {
                    ESP_LOGE(TAG, "STA connection failed");
                    s_sta_connecting = false;
                    s_sta_connected = false;
                    s_sta_ip[0] = '\0';
                    emit_event(WIFI_AP_EVENT_STA_FAILED);
                }

                break;

            default:
                break;
        }
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        snprintf(s_sta_ip, sizeof(s_sta_ip), IPSTR, IP2STR(&event->ip_info.ip));

        ESP_LOGI(TAG, "STA got IP: %s", s_sta_ip);

        s_sta_retry_count = 0;
        s_sta_connecting = false;
        s_sta_connected = true;
        s_sta_reconfiguring = false;

        disable_wifi_power_save("wifi_sta_got_ip");
        wifi_ap_log_sta_status("wifi_sta_got_ip_status");

        emit_event(WIFI_AP_EVENT_STA_CONNECTED);
    }
}

static esp_err_t wifi_common_init(void)
{
    if (s_wifi_initialized) {
        return ESP_OK;
    }

    ESP_ERROR_CHECK(init_nvs());
    ESP_ERROR_CHECK(esp_netif_init());

    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    if (s_ap_netif == NULL) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
        if (s_ap_netif == NULL) {
            ESP_LOGE(TAG, "Failed to create AP netif");
            return ESP_FAIL;
        }
    }

    if (s_sta_netif == NULL) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
        if (s_sta_netif == NULL) {
            ESP_LOGE(TAG, "Failed to create STA netif");
            return ESP_FAIL;
        }
    }

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();

    runtime_diag_log("wifi_before_esp_wifi_init");
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
    runtime_diag_log("wifi_after_esp_wifi_init");

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    s_wifi_initialized = true;

    return ESP_OK;
}

static void fill_ap_config(wifi_config_t *ap_config)
{
    memset(ap_config, 0, sizeof(*ap_config));

    strncpy((char *)ap_config->ap.ssid, WIFI_AP_SSID, sizeof(ap_config->ap.ssid) - 1);
    strncpy((char *)ap_config->ap.password, WIFI_AP_PASSWORD, sizeof(ap_config->ap.password) - 1);

    ap_config->ap.ssid_len = strlen(WIFI_AP_SSID);
    ap_config->ap.channel = WIFI_AP_CHANNEL;
    ap_config->ap.max_connection = WIFI_AP_MAX_CONN;
    ap_config->ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    ap_config->ap.pmf_cfg.required = false;

    if (strlen(WIFI_AP_PASSWORD) == 0) {
        ap_config->ap.authmode = WIFI_AUTH_OPEN;
    }
}

esp_err_t wifi_ap_start(void)
{
    ESP_LOGI(TAG, "Starting Wi-Fi APSTA setup mode");

    ESP_ERROR_CHECK(wifi_common_init());

    wifi_config_t ap_config;
    fill_ap_config(&ap_config);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    if (!s_wifi_started) {
        runtime_diag_log("wifi_sta_before_esp_wifi_start");
        ESP_ERROR_CHECK(esp_wifi_start());
        s_wifi_started = true;
        runtime_diag_log("wifi_sta_after_esp_wifi_start");
    }

    disable_wifi_power_save("wifi_ap_start");

    s_setup_ap_enabled = true;

    ESP_LOGI(TAG, "SoftAP started");
    ESP_LOGI(TAG, "SSID: %s", WIFI_AP_SSID);
    ESP_LOGI(TAG, "Password: %s", WIFI_AP_PASSWORD);
    ESP_LOGI(TAG, "IP: %s", wifi_ap_get_ip());

    /*
     * Do not start a scan immediately when APSTA setup mode starts.
     *
     * Starting SoftAP + HTTP server + DNS + Wi-Fi scan + LVGL redraw at the
     * same moment can exhaust internal DMA-capable memory. The portal can use
     * the existing scan cache, and the web UI can still trigger a fresh scan
     * through /refresh.
     */
    return ESP_OK;
}

esp_err_t wifi_ap_start_sta_only(void)
{
    ESP_LOGI(TAG, "Starting Wi-Fi STA-only mode");

    ESP_ERROR_CHECK(wifi_common_init());
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    if (!s_wifi_started) {
        ESP_ERROR_CHECK(esp_wifi_start());
        s_wifi_started = true;
    }

    disable_wifi_power_save("wifi_sta_only_start");

    s_setup_ap_enabled = false;

    return ESP_OK;
}

esp_err_t wifi_ap_stop_setup_ap(void)
{
    if (!s_wifi_started || !s_setup_ap_enabled) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping setup SoftAP and switching to STA-only mode");

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    s_setup_ap_enabled = false;

    return ESP_OK;
}

esp_err_t wifi_ap_stop_all(void)
{
    if (!s_wifi_started) {
        s_setup_ap_enabled = false;
        s_sta_connecting = false;
        s_sta_connected = false;
        s_sta_ip[0] = '\0';
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping Wi-Fi radio");

    s_sta_reconfiguring = true;
    s_sta_connecting = false;
    s_sta_connected = false;
    s_setup_ap_enabled = false;
    s_sta_ip[0] = '\0';

    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED && err != ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGW(TAG, "esp_wifi_disconnect before stop failed: %s", esp_err_to_name(err));
    }

    err = esp_wifi_stop();
    if (err == ESP_ERR_WIFI_NOT_STARTED || err == ESP_ERR_WIFI_NOT_INIT) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        s_wifi_started = false;
        s_sta_reconfiguring = false;
    }

    return err;
}

static esp_err_t wifi_ap_scan_sync(wifi_ap_scan_result_t *results, uint16_t *count)
{
    if (results == NULL || count == NULL || *count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    runtime_diag_log("wifi_before_scan_start");
    ESP_LOGI(TAG, "Scanning Wi-Fi networks");

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
    };

    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Scan failed: %s", esp_err_to_name(err));
        return err;
    }

    uint16_t ap_count = 0;
    err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_scan_get_ap_num failed: %s", esp_err_to_name(err));
        return err;
    }

    if (ap_count > *count) {
        ap_count = *count;
    }

    if (ap_count > WIFI_AP_SCAN_MAX_RESULTS) {
        ap_count = WIFI_AP_SCAN_MAX_RESULTS;
    }

    static wifi_ap_record_t records[WIFI_AP_SCAN_MAX_RESULTS];
    memset(records, 0, sizeof(records));

    err = esp_wifi_scan_get_ap_records(&ap_count, records);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_scan_get_ap_records failed: %s", esp_err_to_name(err));
        return err;
    }

    for (uint16_t i = 0; i < ap_count; i++) {
        strncpy(results[i].ssid, (const char *)records[i].ssid, sizeof(results[i].ssid) - 1);
        results[i].ssid[sizeof(results[i].ssid) - 1] = '\0';
        results[i].rssi = records[i].rssi;
        results[i].secure = records[i].authmode != WIFI_AUTH_OPEN;
    }

    *count = ap_count;

    ESP_LOGI(TAG, "Scan done, found %u networks", ap_count);
    runtime_diag_log("wifi_after_scan_done");

    return ESP_OK;
}

static esp_err_t wifi_ap_scan_refresh_sync(void)
{
    static wifi_ap_scan_result_t temp[WIFI_AP_SCAN_MAX_RESULTS];
    memset(temp, 0, sizeof(temp));

    uint16_t count = WIFI_AP_SCAN_MAX_RESULTS;

    esp_err_t err = wifi_ap_scan_sync(temp, &count);
    if (err != ESP_OK) {
        s_scan_cache_valid = false;
        s_scan_cache_count = 0;
        return err;
    }

    memcpy(s_scan_cache, temp, sizeof(s_scan_cache));
    s_scan_cache_count = count;
    s_scan_cache_valid = true;

    ESP_LOGI(TAG, "Scan cache updated with %u networks", s_scan_cache_count);

    return ESP_OK;
}

esp_err_t wifi_ap_scan_refresh_blocking(void)
{
    return wifi_ap_scan_refresh_sync();
}

esp_err_t wifi_ap_scan_refresh_async(void)
{
    if (!s_wifi_started) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_scan_task_handle != NULL || s_scan_in_progress) {
        ESP_LOGW(TAG, "Scan already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Scheduling Wi-Fi scan task");

    BaseType_t task_ok = xTaskCreate(wifi_scan_task, "wifi_scan_task", WIFI_SCAN_TASK_STACK_SIZE, NULL, 5, &s_scan_task_handle);

    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Wi-Fi scan task");
        s_scan_task_handle = NULL;
        s_scan_in_progress = false;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static void wifi_scan_task(void *arg)
{
    s_scan_in_progress = true;
    emit_event(WIFI_AP_EVENT_SCAN_STARTED);

    ESP_LOGI(TAG, "Wi-Fi scan task started");

    vTaskDelay(pdMS_TO_TICKS(3000));

    esp_err_t scan_err = wifi_ap_scan_refresh_sync();

    if (scan_err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi scan failed: %s", esp_err_to_name(scan_err));
        emit_event(WIFI_AP_EVENT_SCAN_FAILED);
    } else {
        ESP_LOGI(TAG, "Wi-Fi scan completed");
        emit_event(WIFI_AP_EVENT_SCAN_DONE);
    }

    s_scan_in_progress = false;
    s_scan_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t wifi_ap_get_scan_results(wifi_ap_scan_result_t *results, uint16_t *count)
{
    if (results == NULL || count == NULL || *count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_scan_cache_valid) {
        *count = 0;
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t copy_count = s_scan_cache_count;

    if (copy_count > *count) {
        copy_count = *count;
    }

    memcpy(results, s_scan_cache, copy_count * sizeof(wifi_ap_scan_result_t));
    *count = copy_count;

    return ESP_OK;
}

esp_err_t wifi_ap_connect_sta(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    runtime_diag_log("wifi_before_connect_sta");
    ESP_LOGI(TAG, "Connecting STA to SSID: %s", ssid);

    wifi_config_t sta_config = {0};

    strncpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
    strncpy((char *)sta_config.sta.password, password != NULL ? password : "", sizeof(sta_config.sta.password) - 1);

    sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    sta_config.sta.pmf_cfg.capable = true;
    sta_config.sta.pmf_cfg.required = false;

    strncpy(s_sta_ssid, ssid, sizeof(s_sta_ssid) - 1);
    s_sta_ssid[sizeof(s_sta_ssid) - 1] = '\0';

    s_sta_ip[0] = '\0';
    s_sta_retry_count = 0;
    s_sta_connecting = true;

    if (s_sta_connected) {
        s_sta_reconfiguring = true;
    }

    esp_wifi_disconnect();
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));

    emit_event(WIFI_AP_EVENT_STA_CONNECTING);

    runtime_diag_log("wifi_before_esp_wifi_connect");
    esp_err_t connect_err = esp_wifi_connect();
    runtime_diag_log("wifi_after_esp_wifi_connect");

    return connect_err;
}

esp_err_t wifi_ap_disconnect_sta(void)
{
    if (!s_wifi_started) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Disconnecting STA");

    s_sta_reconfiguring = true;
    s_sta_connecting = false;
    s_sta_connected = false;
    s_sta_ip[0] = '\0';

    esp_err_t err = esp_wifi_disconnect();
    if (err == ESP_ERR_WIFI_NOT_STARTED || err == ESP_ERR_WIFI_NOT_INIT) {
        return ESP_OK;
    }

    return err;
}

const char *wifi_ap_get_ssid(void)
{
    return WIFI_AP_SSID;
}

const char *wifi_ap_get_password(void)
{
    return WIFI_AP_PASSWORD;
}

const char *wifi_ap_get_ip(void)
{
    return "192.168.4.1";
}

const char *wifi_ap_get_sta_ssid(void)
{
    return s_sta_ssid;
}

const char *wifi_ap_get_sta_ip(void)
{
    return s_sta_ip;
}

bool wifi_ap_is_sta_connected(void)
{
    return s_sta_connected;
}

int wifi_ap_get_sta_rssi(void)
{
    if (!s_wifi_started || !s_sta_connected) {
        return 0;
    }

    wifi_ap_record_t ap_info = {0};
    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
        return 0;
    }

    return (int)ap_info.rssi;
}
