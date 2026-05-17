/**
 * @file wifi/wifi_manager.h
 * @brief Application-level Wi-Fi manager API.
 */

#pragma once

#include "esp_err.h"

/**
 * @brief Start the Wi-Fi manager workflow.
 *
 * The workflow scans for saved networks, tries to connect to the best visible
 * saved network, and opens the setup portal if no saved network can be used.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t wifi_manager_start(void);

/**
 * @brief Open the setup portal for adding or switching Wi-Fi networks.
 */
void wifi_manager_open_setup_portal(void);

/**
 * @brief Close the setup portal if it is active.
 *
 * This stops the HTTP portal, DNS redirect server, and setup SoftAP. Saved
 * networks are not modified.
 */
void wifi_manager_close_setup_portal(void);

/**
 * @brief Connect to an already saved network.
 *
 * @param ssid Saved SSID to connect to.
 */
void wifi_manager_connect_saved_network(const char *ssid);

/**
 * @brief Forget a saved network.
 *
 * @param ssid Saved SSID to remove.
 */
void wifi_manager_forget_saved_network(const char *ssid);
