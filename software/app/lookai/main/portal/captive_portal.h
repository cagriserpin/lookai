/**
 * @file portal/captive_portal.h
 * @brief HTTP captive portal API for adding/switching Wi-Fi networks.
 */

#pragma once

#include "esp_err.h"

/**
 * @brief Callback invoked when the portal receives Wi-Fi credentials.
 */
typedef void (*captive_portal_connect_cb_t)(const char *ssid, const char *password);

/**
 * @brief Start the HTTP captive portal.
 */
esp_err_t captive_portal_start(captive_portal_connect_cb_t connect_cb);

/**
 * @brief Stop the HTTP captive portal.
 */
esp_err_t captive_portal_stop(void);
