/**
 * @file app/app_controller.c
 * @brief Top-level application initialization and module wiring.
 */

#include "app_controller.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"

#include "ui_manager.h"
#include "wifi_manager.h"
#include "wifi_storage.h"

static const char *TAG = "app_controller";

/**
 * @brief Reduce expected captive-portal HTTP log noise.
 *
 * Captive portal clients often open and close sockets aggressively while they
 * probe for internet access. These logs are expected and can slow the device
 * when they spam the serial console.
 */
static void configure_log_levels(void)
{
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_ERROR);
}

esp_err_t app_controller_start(void)
{
    ESP_LOGI(TAG, "Starting application");

    configure_log_levels();

    ESP_RETURN_ON_ERROR(ui_manager_init(), TAG, "Failed to initialize UI");
    ESP_RETURN_ON_ERROR(wifi_storage_init(), TAG, "Failed to initialize Wi-Fi storage");

    ui_manager_callbacks_t callbacks = {
        .connect_another = wifi_manager_open_setup_portal,
        .close_portal = wifi_manager_close_setup_portal,
        .connect_saved = wifi_manager_connect_saved_network,
        .forget_saved = wifi_manager_forget_saved_network,
    };

    ui_manager_set_callbacks(&callbacks);

    ESP_RETURN_ON_ERROR(wifi_manager_start(), TAG, "Failed to start Wi-Fi manager");

    return ESP_OK;
}
