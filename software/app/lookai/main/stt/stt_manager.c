/**
 * @file stt/stt_manager.c
 * @brief Speech-to-text backend state machine implementation.
 */

#include "stt_manager.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "ui_manager.h"

static const char *TAG = "stt_manager";

#define STT_EVENT_QUEUE_LEN 8
#define STT_TASK_STACK_SIZE 4096
#define STT_FAKE_PROCESSING_MS 1500

typedef enum {
    STT_MANAGER_EVENT_PRESS = 0,
    STT_MANAGER_EVENT_RELEASE,
} stt_manager_event_t;

typedef enum {
    STT_MANAGER_STATE_READY = 0,
    STT_MANAGER_STATE_RECORDING,
    STT_MANAGER_STATE_PROCESSING,
    STT_MANAGER_STATE_DONE,
    STT_MANAGER_STATE_ERROR,
} stt_manager_state_t;

static QueueHandle_t s_stt_event_queue = NULL;
static TaskHandle_t s_stt_task_handle = NULL;
static stt_manager_state_t s_state = STT_MANAGER_STATE_READY;

static void post_event(stt_manager_event_t event)
{
    if (s_stt_event_queue == NULL) {
        return;
    }

    xQueueSend(s_stt_event_queue, &event, 0);
}

void stt_manager_press(void)
{
    post_event(STT_MANAGER_EVENT_PRESS);
}

void stt_manager_release(void)
{
    post_event(STT_MANAGER_EVENT_RELEASE);
}

static void handle_press(void)
{
    if (s_state == STT_MANAGER_STATE_PROCESSING) {
        ESP_LOGI(TAG, "Ignoring press while STT is processing");
        return;
    }

    ESP_LOGI(TAG, "Push to Talk pressed");

    /*
     * Do not repaint the STT screen here.
     *
     * If the screen is deleted/recreated while the finger is still pressing the
     * button, LVGL can lose the matching RELEASED event. That made the flow feel
     * like "tap once to record, tap again to process". The release event must be
     * delivered to the same button object.
     */
    s_state = STT_MANAGER_STATE_RECORDING;
}

static void handle_release(void)
{
    if (s_state != STT_MANAGER_STATE_RECORDING) {
        ESP_LOGI(TAG, "Ignoring release because STT is not recording");
        return;
    }

    ESP_LOGI(TAG, "Push to Talk released");

    s_state = STT_MANAGER_STATE_PROCESSING;
    ui_manager_update_stt_status("Processing...", "", false, true);

    /*
     * Stage 2 only: fake processing result.
     * The real audio recorder + WAV builder + cloud/client STT will replace
     * this delay/result in the next implementation stage.
     */
    vTaskDelay(pdMS_TO_TICKS(STT_FAKE_PROCESSING_MS));

    s_state = STT_MANAGER_STATE_DONE;
    ui_manager_update_stt_status(
        "Done",
        "Fake transcript: audio backend will be connected next.",
        false,
        false
    );
}

static void stt_task(void *arg)
{
    (void)arg;

    stt_manager_event_t event;

    ui_manager_update_stt_status("Ready", "", false, false);

    while (true) {
        if (xQueueReceive(s_stt_event_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (event) {
            case STT_MANAGER_EVENT_PRESS:
                handle_press();
                break;

            case STT_MANAGER_EVENT_RELEASE:
                handle_release();
                break;

            default:
                break;
        }
    }
}

esp_err_t stt_manager_start(void)
{
    if (s_stt_event_queue != NULL) {
        return ESP_OK;
    }

    s_stt_event_queue = xQueueCreate(STT_EVENT_QUEUE_LEN, sizeof(stt_manager_event_t));
    if (s_stt_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create STT event queue");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ok = xTaskCreate(
        stt_task,
        "stt_task",
        STT_TASK_STACK_SIZE,
        NULL,
        5,
        &s_stt_task_handle
    );

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create STT task");
        vQueueDelete(s_stt_event_queue);
        s_stt_event_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "STT manager started");

    return ESP_OK;
}
