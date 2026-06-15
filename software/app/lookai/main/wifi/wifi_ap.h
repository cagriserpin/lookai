/**
 * @file wifi/wifi_ap.h
 * @brief Low-level ESP-IDF Wi-Fi AP/STA wrapper API.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define WIFI_AP_SCAN_MAX_RESULTS 20

/**
 * @brief Events emitted by the low-level Wi-Fi wrapper.
 */
typedef enum {
    WIFI_AP_EVENT_CLIENT_CONNECTED,      /**< A phone/client connected to setup SoftAP. */
    WIFI_AP_EVENT_CLIENT_DISCONNECTED,   /**< A phone/client disconnected from setup SoftAP. */

    WIFI_AP_EVENT_SCAN_STARTED,          /**< Wi-Fi scan started. */
    WIFI_AP_EVENT_SCAN_DONE,             /**< Wi-Fi scan completed. */
    WIFI_AP_EVENT_SCAN_FAILED,           /**< Wi-Fi scan failed. */

    WIFI_AP_EVENT_STA_CONNECTING,        /**< STA connection attempt is in progress. */
    WIFI_AP_EVENT_STA_CONNECTED,         /**< STA got an IP address. */
    WIFI_AP_EVENT_STA_FAILED,            /**< STA connection failed after retries. */
    WIFI_AP_EVENT_STA_DISCONNECTED,      /**< STA disconnected after being connected. */
} wifi_ap_event_t;

/**
 * @brief Single Wi-Fi scan result.
 */
typedef struct {
    char ssid[33];  /**< SSID, null terminated. */
    int8_t rssi;    /**< Signal strength in dBm. */
    bool secure;    /**< True when AP uses authentication. */
} wifi_ap_scan_result_t;

/**
 * @brief Wi-Fi event callback type.
 */
typedef void (*wifi_ap_event_cb_t)(wifi_ap_event_t event);

/**
 * @brief Register a Wi-Fi event callback.
 */
void wifi_ap_set_event_cb(wifi_ap_event_cb_t cb);

/**
 * @brief Start setup mode using AP+STA.
 */
esp_err_t wifi_ap_start(void);

/**
 * @brief Start STA-only mode.
 */
esp_err_t wifi_ap_start_sta_only(void);

/**
 * @brief Disable setup SoftAP and keep STA mode.
 */
esp_err_t wifi_ap_stop_setup_ap(void);

/**
 * @brief Stop all Wi-Fi operation and clear STA/AP runtime state.
 */
esp_err_t wifi_ap_stop_all(void);

/**
 * @brief Start an asynchronous Wi-Fi scan.
 */
esp_err_t wifi_ap_scan_refresh_async(void);

/**
 * @brief Run a blocking Wi-Fi scan and refresh cached scan results.
 */
esp_err_t wifi_ap_scan_refresh_blocking(void);

/**
 * @brief Read cached Wi-Fi scan results.
 */
esp_err_t wifi_ap_get_scan_results(wifi_ap_scan_result_t *results, uint16_t *count);

/**
 * @brief Connect STA to an SSID.
 */
esp_err_t wifi_ap_connect_sta(const char *ssid, const char *password);

/**
 * @brief Disconnect STA if Wi-Fi is running.
 */
esp_err_t wifi_ap_disconnect_sta(void);

/**
 * @brief Get setup SoftAP SSID.
 */
const char *wifi_ap_get_ssid(void);

/**
 * @brief Get setup SoftAP password.
 */
const char *wifi_ap_get_password(void);

/**
 * @brief Get setup SoftAP IP address.
 */
const char *wifi_ap_get_ip(void);

/**
 * @brief Get current/target STA SSID.
 */
const char *wifi_ap_get_sta_ssid(void);

/**
 * @brief Get STA IPv4 address.
 */
const char *wifi_ap_get_sta_ip(void);

/**
 * @brief Return whether STA is connected.
 */
bool wifi_ap_is_sta_connected(void);

/**
 * @brief Get current STA RSSI in dBm, or 0 when unavailable.
 */
int wifi_ap_get_sta_rssi(void);

/**
 * @brief Log current STA link/IP state for diagnostics.
 *
 * This function does not change Wi-Fi state.
 */
void wifi_ap_log_sta_status(const char *point);
