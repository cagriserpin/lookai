/**
 * @file wifi/wifi_manager.c
 * @brief Application-level Wi-Fi state machine and setup portal orchestration.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "wifi_manager.h"
#include "ui_manager.h"
#include "wifi_ap.h"
#include "captive_portal.h"
#include "dns_server.h"
#include "wifi_storage.h"

static const char *TAG = "main";

#define RECONNECT_INTERVAL_MS 10000
#define RECONNECT_WAIT_SECONDS 20
#define APP_EVENT_QUEUE_LEN 16

typedef enum {
    APP_EVENT_WIFI,
} app_event_type_t;

typedef struct {
    app_event_type_t type;
    wifi_ap_event_t wifi_event;
} app_event_t;

static QueueHandle_t s_app_event_queue = NULL;
static TaskHandle_t s_app_event_task_handle = NULL;

static char s_pending_ssid[33] = {0};
static char s_pending_password[65] = {0};
static bool s_pending_credentials_valid = false;

static bool s_setup_portal_active = false;
static bool s_auto_connect_attempt = false;
static bool s_reconnect_mode = false;
static bool s_manual_setup_requested = false;

static TaskHandle_t s_close_setup_task_handle = NULL;
static TaskHandle_t s_reconnect_task_handle = NULL;
static TaskHandle_t s_start_setup_task_handle = NULL;
static TaskHandle_t s_connect_another_task_handle = NULL;
static TaskHandle_t s_boot_wifi_task_handle = NULL;
static TaskHandle_t s_portal_connect_task_handle = NULL;

typedef struct {
    char ssid[33];
    char password[65];
} portal_connect_request_t;

typedef struct {
    char ssid[33];
} ssid_request_t;

static esp_err_t start_setup_portal_sync(void);
static void update_saved_networks_ui_cache(void);
static void update_wifi_status(const char *status);

static int get_saved_count(void)
{
    wifi_storage_credential_t items[WIFI_STORAGE_MAX_NETWORKS] = {0};
    size_t count = 0;

    if (wifi_storage_get_all(items, WIFI_STORAGE_MAX_NETWORKS, &count) != ESP_OK) {
        return 0;
    }

    return (int)count;
}

static void update_saved_networks_ui_cache(void)
{
    wifi_storage_credential_t items[WIFI_STORAGE_MAX_NETWORKS] = {0};
    ui_manager_saved_network_t ui_items[UI_MANAGER_MAX_SAVED_NETWORKS] = {0};
    size_t count = 0;

    esp_err_t err = wifi_storage_get_all(items, WIFI_STORAGE_MAX_NETWORKS, &count);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load saved networks for UI: %s", esp_err_to_name(err));
        ui_manager_set_saved_networks(NULL, 0);
        return;
    }

    for (size_t i = 0; i < count && i < UI_MANAGER_MAX_SAVED_NETWORKS; i++) {
        strncpy(ui_items[i].ssid, items[i].ssid, sizeof(ui_items[i].ssid) - 1);
        ui_items[i].ssid[sizeof(ui_items[i].ssid) - 1] = '\0';
        ui_items[i].connected = wifi_ap_is_sta_connected() && strcmp(items[i].ssid, wifi_ap_get_sta_ssid()) == 0;
    }

    ui_manager_set_saved_networks(ui_items, (int)count);
}

static void update_wifi_status(const char *status)
{
    ui_manager_update_wifi_status(
        status,
        wifi_ap_get_sta_ssid(),
        wifi_ap_get_sta_ip(),
        get_saved_count(),
        s_setup_portal_active
    );

    update_saved_networks_ui_cache();
}

static bool find_best_saved_network(char *out_ssid, size_t out_ssid_len, char *out_password, size_t out_password_len)
{
    wifi_storage_credential_t saved[WIFI_STORAGE_MAX_NETWORKS] = {0};
    size_t saved_count = 0;

    esp_err_t err = wifi_storage_get_all(saved, WIFI_STORAGE_MAX_NETWORKS, &saved_count);

    if (err != ESP_OK || saved_count == 0) {
        ESP_LOGI(TAG, "No saved networks available");
        return false;
    }

    update_wifi_status("Scanning networks");

    err = wifi_ap_scan_refresh_blocking();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi scan failed while selecting saved network: %s", esp_err_to_name(err));
        return false;
    }

    wifi_ap_scan_result_t scan[WIFI_AP_SCAN_MAX_RESULTS] = {0};
    uint16_t scan_count = WIFI_AP_SCAN_MAX_RESULTS;

    err = wifi_ap_get_scan_results(scan, &scan_count);
    if (err != ESP_OK || scan_count == 0) {
        ESP_LOGW(TAG, "No scan results available");
        return false;
    }

    int best_saved_index = -1;
    int8_t best_rssi = -128;

    for (size_t i = 0; i < saved_count; i++) {
        if (!saved[i].valid) {
            continue;
        }

        for (uint16_t j = 0; j < scan_count; j++) {
            if (strcmp(saved[i].ssid, scan[j].ssid) == 0) {
                if (best_saved_index < 0 || scan[j].rssi > best_rssi) {
                    best_saved_index = (int)i;
                    best_rssi = scan[j].rssi;
                }
            }
        }
    }

    if (best_saved_index < 0) {
        ESP_LOGI(TAG, "No saved network was found in scan results");
        return false;
    }

    strncpy(out_ssid, saved[best_saved_index].ssid, out_ssid_len - 1);
    out_ssid[out_ssid_len - 1] = '\0';

    strncpy(out_password, saved[best_saved_index].password, out_password_len - 1);
    out_password[out_password_len - 1] = '\0';

    ESP_LOGI(TAG, "Best saved network: %s, RSSI: %d", out_ssid, best_rssi);

    return true;
}

static void save_pending_credentials_if_needed(void)
{
    if (!s_pending_credentials_valid || s_pending_ssid[0] == '\0') {
        return;
    }

    esp_err_t err = wifi_storage_add_or_update(s_pending_ssid, s_pending_password);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Saved or updated Wi-Fi credentials");
    } else if (err == ESP_ERR_NO_MEM) {
        ESP_LOGW(TAG, "Saved network list is full; could not save new network");
    } else {
        ESP_LOGE(TAG, "Failed to save credentials: %s", esp_err_to_name(err));
    }

    s_pending_credentials_valid = false;
    update_saved_networks_ui_cache();
}

static void close_setup_portal_task(void *arg)
{
    ESP_LOGI(TAG, "Setup portal will close shortly");

    vTaskDelay(pdMS_TO_TICKS(3000));

    if (s_setup_portal_active) {
        ESP_LOGI(TAG, "Closing setup portal after successful Wi-Fi connection");

        captive_portal_stop();
        dns_server_stop();
        wifi_ap_stop_setup_ap();

        s_setup_portal_active = false;
        s_manual_setup_requested = false;
    }

    if (wifi_ap_is_sta_connected()) {
        update_wifi_status("Connected");
    }

    s_close_setup_task_handle = NULL;
    vTaskDelete(NULL);
}

static void schedule_close_setup_portal(void)
{
    if (s_close_setup_task_handle != NULL) {
        return;
    }

    xTaskCreate(close_setup_portal_task, "close_setup", 4096, NULL, 5, &s_close_setup_task_handle);
}

static void start_setup_portal_task(void *arg)
{
    start_setup_portal_sync();

    s_start_setup_task_handle = NULL;
    vTaskDelete(NULL);
}

static void schedule_start_setup_portal(void)
{
    if (s_setup_portal_active || s_start_setup_task_handle != NULL) {
        return;
    }

    xTaskCreate(start_setup_portal_task, "start_setup", 6144, NULL, 5, &s_start_setup_task_handle);
}

static void connect_another_network_task(void *arg)
{
    ESP_LOGI(TAG, "Connect another network requested from device UI");

    /*
     * This prevents a race where the boot-time saved-network scan/connect flow
     * continues while the setup portal is being opened. That race can make Wi-Fi
     * mode changes, scans, and LVGL display flushes happen at the same time and
     * exhaust DMA/internal buffers.
     */
    s_manual_setup_requested = true;
    s_pending_credentials_valid = false;
    s_auto_connect_attempt = false;
    s_reconnect_mode = false;

    update_wifi_status("Opening setup portal");

    /*
     * If the boot task is in the middle of a blocking scan, wait for it to
     * observe s_manual_setup_requested and exit before switching to APSTA mode.
     */
    for (int i = 0; i < 80 && s_boot_wifi_task_handle != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    esp_err_t err = start_setup_portal_sync();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start setup portal: %s", esp_err_to_name(err));
        update_wifi_status("Setup failed");
    }

    /*
     * Add New Network should return the visible UI context to Wi-Fi status.
     */
    ui_manager_show_wifi();

    s_connect_another_task_handle = NULL;
    vTaskDelete(NULL);
}

static void on_connect_another_pressed(void)
{
    s_manual_setup_requested = true;

    if (s_connect_another_task_handle != NULL) {
        return;
    }

    xTaskCreate(connect_another_network_task, "connect_another", 6144, NULL, 5, &s_connect_another_task_handle);
}

static void connect_saved_task(void *arg)
{
    ssid_request_t *request = (ssid_request_t *)arg;

    if (request != NULL && request->ssid[0] != '\0') {
        wifi_storage_credential_t credential = {0};

        esp_err_t err = wifi_storage_find_by_ssid(request->ssid, &credential);
        if (err == ESP_OK) {
            update_wifi_status("Connecting saved Wi-Fi");
            wifi_ap_start_sta_only();
            wifi_ap_connect_sta(credential.ssid, credential.password);
        } else {
            ESP_LOGW(TAG, "Saved network not found: %s", request->ssid);
            update_wifi_status("Saved network missing");
        }
    }

    free(request);
    vTaskDelete(NULL);
}

static void on_connect_saved_network(const char *ssid)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return;
    }

    ssid_request_t *request = calloc(1, sizeof(ssid_request_t));
    if (request == NULL) {
        return;
    }

    strncpy(request->ssid, ssid, sizeof(request->ssid) - 1);

    xTaskCreate(connect_saved_task, "connect_saved", 8192, request, 5, NULL);
}

static void forget_saved_task(void *arg)
{
    ssid_request_t *request = (ssid_request_t *)arg;

    if (request != NULL && request->ssid[0] != '\0') {
        bool was_current = wifi_ap_is_sta_connected() && strcmp(request->ssid, wifi_ap_get_sta_ssid()) == 0;

        ESP_LOGI(TAG, "Forgetting saved network: %s", request->ssid);

        esp_err_t err = wifi_storage_remove_by_ssid(request->ssid);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to forget saved network: %s", esp_err_to_name(err));
        }

        if (was_current) {
            ESP_LOGI(TAG, "Forgot currently connected network, disconnecting STA");
            wifi_ap_disconnect_sta();
        }

        update_saved_networks_ui_cache();
        update_wifi_status(was_current ? "Current network forgotten" : "Saved network removed");
    }

    free(request);
    vTaskDelete(NULL);
}

static void on_forget_saved_network(const char *ssid)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return;
    }

    ssid_request_t *request = calloc(1, sizeof(ssid_request_t));
    if (request == NULL) {
        return;
    }

    strncpy(request->ssid, ssid, sizeof(request->ssid) - 1);

    xTaskCreate(forget_saved_task, "forget_saved", 4096, request, 5, NULL);
}

static void reconnect_task(void *arg)
{
    ESP_LOGI(TAG, "Reconnect task started");

    while (!wifi_ap_is_sta_connected()) {
        if (s_manual_setup_requested) {
            ESP_LOGI(TAG, "Reconnect task stopped because setup portal was requested");
            break;
        }

        if (s_setup_portal_active) {
            vTaskDelay(pdMS_TO_TICKS(RECONNECT_INTERVAL_MS));
            continue;
        }

        char ssid[33] = {0};
        char password[65] = {0};

        update_wifi_status("Scanning saved Wi-Fi");

        if (!find_best_saved_network(ssid, sizeof(ssid), password, sizeof(password))) {
            update_wifi_status("Saved Wi-Fi not found");
            schedule_start_setup_portal();
            vTaskDelay(pdMS_TO_TICKS(RECONNECT_INTERVAL_MS));
            continue;
        }

        update_wifi_status("Reconnecting");

        s_reconnect_mode = true;
        esp_err_t err = wifi_ap_connect_sta(ssid, password);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Reconnect attempt failed to start: %s", esp_err_to_name(err));
        }

        for (int i = 0; i < RECONNECT_WAIT_SECONDS; i++) {
            if (wifi_ap_is_sta_connected()) {
                break;
            }

            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        if (wifi_ap_is_sta_connected()) {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(RECONNECT_INTERVAL_MS));
    }

    s_reconnect_mode = false;
    ESP_LOGI(TAG, "Reconnect task stopped");

    s_reconnect_task_handle = NULL;
    vTaskDelete(NULL);
}

static void schedule_reconnect_task(void)
{
    if (s_reconnect_task_handle != NULL) {
        return;
    }

    xTaskCreate(reconnect_task, "reconnect_task", 8192, NULL, 5, &s_reconnect_task_handle);
}

/*
 * Very important:
 * wifi_ap.c emits these events from the ESP-IDF Wi-Fi event context.
 * Do not update LVGL, NVS, or start heavy work directly here.
 * Queue the event to a dedicated app task with a larger stack.
 */
static void on_wifi_ap_event(wifi_ap_event_t event)
{
    if (s_app_event_queue == NULL) {
        return;
    }

    app_event_t app_event = {
        .type = APP_EVENT_WIFI,
        .wifi_event = event,
    };

    xQueueSend(s_app_event_queue, &app_event, 0);
}

static void handle_wifi_event(wifi_ap_event_t event)
{
    if (event == WIFI_AP_EVENT_CLIENT_CONNECTED) {
        update_wifi_status("Phone connected");
    }

    if (event == WIFI_AP_EVENT_CLIENT_DISCONNECTED) {
        if (s_setup_portal_active) {
            update_wifi_status("Setup portal active");
        }
    }

    if (event == WIFI_AP_EVENT_SCAN_STARTED) {
        if (s_setup_portal_active) {
            update_wifi_status("Scanning networks");
        }
    }

    if (event == WIFI_AP_EVENT_SCAN_DONE) {
        if (s_setup_portal_active) {
            update_wifi_status("Setup portal active");
        }
    }

    if (event == WIFI_AP_EVENT_SCAN_FAILED) {
        if (s_setup_portal_active) {
            update_wifi_status("Scan failed");
        }
    }

    if (event == WIFI_AP_EVENT_STA_CONNECTING) {
        update_wifi_status("Connecting");
    }

    if (event == WIFI_AP_EVENT_STA_CONNECTED) {
        s_auto_connect_attempt = false;
        s_reconnect_mode = false;
        s_manual_setup_requested = false;

        save_pending_credentials_if_needed();
        update_wifi_status("Connected");
        schedule_close_setup_portal();
    }

    if (event == WIFI_AP_EVENT_STA_FAILED) {
        if (s_pending_credentials_valid) {
            s_pending_credentials_valid = false;
            update_wifi_status("Connection failed");
            return;
        }

        if (s_auto_connect_attempt) {
            s_auto_connect_attempt = false;
            update_wifi_status("Saved Wi-Fi failed");
            schedule_start_setup_portal();
            return;
        }

        if (s_reconnect_mode) {
            update_wifi_status("Reconnect failed");
            return;
        }

        if (!s_setup_portal_active) {
            update_wifi_status("Connection failed");
            schedule_start_setup_portal();
        }
    }

    if (event == WIFI_AP_EVENT_STA_DISCONNECTED) {
        update_wifi_status("Wi-Fi lost");

        if (wifi_storage_has_any()) {
            schedule_reconnect_task();
        } else {
            schedule_start_setup_portal();
        }
    }
}

static void app_event_task(void *arg)
{
    app_event_t event;

    while (true) {
        if (xQueueReceive(s_app_event_queue, &event, portMAX_DELAY) == pdTRUE) {
            if (event.type == APP_EVENT_WIFI) {
                handle_wifi_event(event.wifi_event);
            }
        }
    }
}

static void portal_connect_task(void *arg)
{
    portal_connect_request_t *request = (portal_connect_request_t *)arg;

    if (request == NULL) {
        s_portal_connect_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Portal requested connection to SSID: %s", request->ssid);

    memset(s_pending_ssid, 0, sizeof(s_pending_ssid));
    memset(s_pending_password, 0, sizeof(s_pending_password));

    strncpy(s_pending_ssid, request->ssid, sizeof(s_pending_ssid) - 1);
    strncpy(s_pending_password, request->password, sizeof(s_pending_password) - 1);

    s_pending_credentials_valid = true;

    update_wifi_status("Credentials received");

    esp_err_t err = wifi_ap_connect_sta(request->ssid, request->password);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start STA connection: %s", esp_err_to_name(err));

        s_pending_credentials_valid = false;
        update_wifi_status("Connect error");
    }

    free(request);

    s_portal_connect_task_handle = NULL;
    vTaskDelete(NULL);
}

static void on_portal_connect_request(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return;
    }

    if (s_portal_connect_task_handle != NULL) {
        ESP_LOGW(TAG, "Ignoring portal connect request because another connection is in progress");
        return;
    }

    portal_connect_request_t *request = calloc(1, sizeof(portal_connect_request_t));
    if (request == NULL) {
        ESP_LOGE(TAG, "Failed to allocate portal connect request");
        return;
    }

    strncpy(request->ssid, ssid, sizeof(request->ssid) - 1);
    strncpy(request->password, password != NULL ? password : "", sizeof(request->password) - 1);

    BaseType_t ok = xTaskCreate(portal_connect_task, "portal_connect", 12288, request, 5, &s_portal_connect_task_handle);

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create portal connect task");
        free(request);
        s_portal_connect_task_handle = NULL;
    }
}

static esp_err_t start_setup_portal_sync(void)
{
    if (s_setup_portal_active) {
        update_wifi_status("Setup portal active");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting setup portal");

    update_wifi_status("Starting setup portal");

    esp_err_t err = wifi_ap_start();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start AP: %s", esp_err_to_name(err));
        update_wifi_status("AP failed");
        return err;
    }

    err = captive_portal_start(on_portal_connect_request);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start portal: %s", esp_err_to_name(err));
        update_wifi_status("Portal failed");
        return err;
    }

    err = dns_server_start(wifi_ap_get_ip());

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start DNS redirect: %s", esp_err_to_name(err));
        update_wifi_status("DNS failed");
        return err;
    }

    s_setup_portal_active = true;

    update_wifi_status("Setup portal active");

    return ESP_OK;
}

static bool try_saved_wifi_on_boot(void)
{
    if (s_manual_setup_requested) {
        ESP_LOGI(TAG, "Skipping boot auto-connect because setup portal was requested");
        return false;
    }

    if (!wifi_storage_has_any()) {
        ESP_LOGI(TAG, "No saved Wi-Fi credentials found");
        update_wifi_status("No saved Wi-Fi");
        return false;
    }

    esp_err_t err = wifi_ap_start_sta_only();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start STA mode: %s", esp_err_to_name(err));
        update_wifi_status("Wi-Fi start failed");
        return false;
    }

    if (s_manual_setup_requested) {
        ESP_LOGI(TAG, "Setup portal requested while STA mode was starting");
        return false;
    }

    char ssid[33] = {0};
    char password[65] = {0};

    if (!find_best_saved_network(ssid, sizeof(ssid), password, sizeof(password))) {
        update_wifi_status("Saved Wi-Fi not found");
        return false;
    }

    if (s_manual_setup_requested) {
        ESP_LOGI(TAG, "Setup portal requested while scanning saved networks; skipping saved Wi-Fi connect");
        return false;
    }

    ESP_LOGI(TAG, "Trying saved Wi-Fi on boot: %s", ssid);

    update_wifi_status("Trying saved Wi-Fi");

    s_auto_connect_attempt = true;

    err = wifi_ap_connect_sta(ssid, password);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start saved Wi-Fi connection: %s", esp_err_to_name(err));
        s_auto_connect_attempt = false;
        update_wifi_status("Connect start failed");
        return false;
    }

    return true;
}

static void boot_wifi_task(void *arg)
{
    bool saved_connect_started = try_saved_wifi_on_boot();

    if (!saved_connect_started && !s_manual_setup_requested && !s_setup_portal_active) {
        start_setup_portal_sync();
    }

    s_boot_wifi_task_handle = NULL;
    vTaskDelete(NULL);
}


/**
 * @brief Open the setup portal to add or switch to another Wi-Fi network.
 *
 * Existing saved networks are kept. The captive portal is used only to add or
 * switch to a network, not to manage saved networks.
 */
void wifi_manager_open_setup_portal(void)
{
    on_connect_another_pressed();
}

/**
 * @brief Close the setup portal if it is active.
 *
 * This action is used by the Wi-Fi UI when the portal is already enabled. It
 * keeps all saved credentials intact and only disables the temporary setup
 * services.
 */
void wifi_manager_close_setup_portal(void)
{
    if (!s_setup_portal_active) {
        update_wifi_status(wifi_ap_is_sta_connected() ? "Connected" : "Portal already closed");
        return;
    }

    ESP_LOGI(TAG, "Closing setup portal by user request");

    captive_portal_stop();
    dns_server_stop();
    wifi_ap_stop_setup_ap();

    s_setup_portal_active = false;
    s_manual_setup_requested = false;

    update_wifi_status(wifi_ap_is_sta_connected() ? "Connected" : "Setup portal closed");
}

/**
 * @brief Connect to a saved Wi-Fi network selected from the device UI.
 *
 * @param ssid Saved SSID to connect to.
 */
void wifi_manager_connect_saved_network(const char *ssid)
{
    on_connect_saved_network(ssid);
}

/**
 * @brief Forget a saved Wi-Fi network selected from the device UI.
 *
 * If the forgotten network is the currently connected network, STA is
 * disconnected and the reconnect/setup workflow is allowed to continue.
 *
 * @param ssid Saved SSID to remove from NVS.
 */
void wifi_manager_forget_saved_network(const char *ssid)
{
    on_forget_saved_network(ssid);
}

/**
 * @brief Start the Wi-Fi manager state machine.
 *
 * This creates the app-level event queue, registers low-level Wi-Fi callbacks,
 * updates the UI, and starts the boot-time saved-network connection task.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t wifi_manager_start(void)
{
    update_saved_networks_ui_cache();

    s_app_event_queue = xQueueCreate(APP_EVENT_QUEUE_LEN, sizeof(app_event_t));
    if (s_app_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create app event queue");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreate(
        app_event_task,
        "app_event",
        12288,
        NULL,
        5,
        &s_app_event_task_handle
    );

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create app event task");
        return ESP_ERR_NO_MEM;
    }

    wifi_ap_set_event_cb(on_wifi_ap_event);

    ui_manager_show_settings();
    update_wifi_status("Starting Wi-Fi");

    ok = xTaskCreate(
        boot_wifi_task,
        "boot_wifi",
        8192,
        NULL,
        5,
        &s_boot_wifi_task_handle
    );

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create boot Wi-Fi task");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
