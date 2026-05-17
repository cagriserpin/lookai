/**
 * @file ui_manager.h
 * @brief LVGL settings and Wi-Fi management UI API.
 */

#pragma once

#include <stdbool.h>

#include "esp_err.h"

#define UI_MANAGER_MAX_SAVED_NETWORKS 5

/**
 * @brief Callback type for UI actions without arguments.
 */
typedef void (*ui_manager_action_cb_t)(void);

/**
 * @brief Callback type for UI actions that target an SSID.
 */
typedef void (*ui_manager_ssid_action_cb_t)(const char *ssid);

/**
 * @brief UI action callbacks provided by the application layer.
 */
typedef struct {
    ui_manager_action_cb_t connect_another;       /**< Open setup portal to add/switch Wi-Fi. */
    ui_manager_action_cb_t close_portal;          /**< Close setup portal when it is active. */
    ui_manager_ssid_action_cb_t connect_saved;    /**< Connect to a selected saved SSID. */
    ui_manager_ssid_action_cb_t forget_saved;     /**< Forget a selected saved SSID. */
} ui_manager_callbacks_t;

/**
 * @brief Saved-network item shown in the UI.
 */
typedef struct {
    char ssid[33];   /**< Saved SSID. */
    bool connected;  /**< True if this SSID is currently connected. */
} ui_manager_saved_network_t;

/**
 * @brief Initialize the LVGL UI.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t ui_manager_init(void);

/**
 * @brief Set UI action callbacks.
 *
 * @param callbacks Callback table. Pass NULL to clear callbacks.
 */
void ui_manager_set_callbacks(const ui_manager_callbacks_t *callbacks);

/**
 * @brief Show the root settings screen.
 */
void ui_manager_show_settings(void);

/**
 * @brief Show the Wi-Fi settings screen.
 */
void ui_manager_show_wifi(void);

/**
 * @brief Update Wi-Fi status information shown on all screens.
 *
 * @param status Human-readable connection state.
 * @param ssid Current or target SSID. Can be NULL to keep previous value.
 * @param ip Current IPv4 address. Can be NULL to keep previous value.
 * @param saved_count Number of saved Wi-Fi networks.
 * @param portal_active True if setup portal is active.
 */
void ui_manager_update_wifi_status(
    const char *status,
    const char *ssid,
    const char *ip,
    int saved_count,
    bool portal_active
);

/**
 * @brief Update saved-network list shown on the device UI.
 *
 * @param items Saved network items. Can be NULL when count is 0.
 * @param count Number of items.
 */
void ui_manager_set_saved_networks(
    const ui_manager_saved_network_t *items,
    int count
);
