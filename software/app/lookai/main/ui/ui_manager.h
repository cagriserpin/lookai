/**
 * @file ui/ui_manager.h
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
    ui_manager_action_cb_t stt_press;             /**< Start push-to-talk recording flow. */
    ui_manager_action_cb_t stt_release;           /**< Stop push-to-talk recording flow. */
} ui_manager_callbacks_t;

/**
 * @brief Saved-network item shown in the UI.
 */
typedef struct {
    char ssid[33];   /**< Saved SSID. */
    bool connected;  /**< True if this SSID is currently connected. */
} ui_manager_saved_network_t;

/**
 * @brief Mutable UI state used by screen renderers.
 */
typedef struct {
    char wifi_status[64];                                                  /**< Human-readable Wi-Fi state. */
    char wifi_ssid[33];                                                    /**< Current or target SSID. */
    char wifi_ip[16];                                                      /**< Current IPv4 address. */
    int saved_count;                                                       /**< Saved network count. */
    bool portal_active;                                                    /**< True when setup portal is active. */
    int brightness_percent;                                                /**< Display brightness, 10-100. */
    char stt_status[64];                                                   /**< Speech-to-text state label. */
    char stt_result[256];                                                  /**< Latest speech-to-text result text. */
    bool stt_recording;                                                    /**< True while push-to-talk is pressed. */
    bool stt_processing;                                                   /**< True while fake/real STT processing is running. */
    ui_manager_saved_network_t saved_items[UI_MANAGER_MAX_SAVED_NETWORKS]; /**< Saved networks shown in UI. */
    int saved_items_count;                                                 /**< Number of saved items. */
} ui_manager_state_t;

esp_err_t ui_manager_init(void);

void ui_manager_set_callbacks(const ui_manager_callbacks_t *callbacks);

void ui_manager_show_settings(void);
void ui_manager_show_wifi(void);

void ui_manager_update_wifi_status(
    const char *status,
    const char *ssid,
    const char *ip,
    int saved_count,
    bool portal_active
);

void ui_manager_set_saved_networks(
    const ui_manager_saved_network_t *items,
    int count
);

void ui_manager_update_stt_status(
    const char *status,
    const char *result,
    bool recording,
    bool processing
);
