#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define WIFI_AP_SCAN_MAX_RESULTS 20

typedef enum {
    WIFI_AP_EVENT_CLIENT_CONNECTED,
    WIFI_AP_EVENT_CLIENT_DISCONNECTED,

    WIFI_AP_EVENT_SCAN_STARTED,
    WIFI_AP_EVENT_SCAN_DONE,
    WIFI_AP_EVENT_SCAN_FAILED,

    WIFI_AP_EVENT_STA_CONNECTING,
    WIFI_AP_EVENT_STA_CONNECTED,
    WIFI_AP_EVENT_STA_FAILED,
    WIFI_AP_EVENT_STA_DISCONNECTED,
} wifi_ap_event_t;

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secure;
} wifi_ap_scan_result_t;

typedef void (*wifi_ap_event_cb_t)(wifi_ap_event_t event);

void wifi_ap_set_event_cb(wifi_ap_event_cb_t cb);

esp_err_t wifi_ap_start(void);
esp_err_t wifi_ap_start_sta_only(void);
esp_err_t wifi_ap_stop_setup_ap(void);

esp_err_t wifi_ap_scan_refresh_async(void);
esp_err_t wifi_ap_scan_refresh_blocking(void);
esp_err_t wifi_ap_get_scan_results(wifi_ap_scan_result_t *results, uint16_t *count);

esp_err_t wifi_ap_connect_sta(const char *ssid, const char *password);
esp_err_t wifi_ap_disconnect_sta(void);

const char *wifi_ap_get_ssid(void);
const char *wifi_ap_get_password(void);
const char *wifi_ap_get_ip(void);

const char *wifi_ap_get_sta_ssid(void);
const char *wifi_ap_get_sta_ip(void);
bool wifi_ap_is_sta_connected(void);
