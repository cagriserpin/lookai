/**
 * @file main.c
 * @brief ESP-IDF application entry point.
 */

#include "esp_err.h"

#include "app_controller.h"

/**
 * @brief ESP-IDF application entry point.
 *
 * All subsystem initialization is delegated to app_controller_start() so the
 * application entry point stays intentionally small.
 */
void app_main(void)
{
    ESP_ERROR_CHECK(app_controller_start());
}