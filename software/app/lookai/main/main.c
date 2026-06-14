/**
 * @file main.c
 * @brief ESP-IDF application entry point.
 */

#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_controller.h"

static const char *TAG = "main";

/*
 * Settings item snapshot generation is intentionally done during UI startup.
 * It can use substantially more stack than the default ESP-IDF main task has,
 * especially while LVGL renders labels into snapshot buffers. Keep app_main()
 * small and move the real application startup to a temporary task with a
 * larger stack. The stack is released after initialization returns.
 */
#define LOOKAI_APP_INIT_TASK_STACK_BYTES 16384
#define LOOKAI_APP_INIT_TASK_PRIORITY    5

static void app_init_task(void *arg)
{
    (void)arg;

    esp_err_t err = app_controller_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Application startup failed: %s", esp_err_to_name(err));
        abort();
    }

    vTaskDelete(NULL);
}

/**
 * @brief ESP-IDF application entry point.
 *
 * The heavy initialization path runs in app_init_task() to avoid exhausting
 * the small default main task stack while building/caching LVGL UI elements.
 */
void app_main(void)
{
    BaseType_t created = xTaskCreatePinnedToCore(
        app_init_task,
        "app_init",
        LOOKAI_APP_INIT_TASK_STACK_BYTES,
        NULL,
        LOOKAI_APP_INIT_TASK_PRIORITY,
        NULL,
        tskNO_AFFINITY
    );

    if (created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create app_init task");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
}
