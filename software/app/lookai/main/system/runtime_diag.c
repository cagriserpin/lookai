/**
 * @file system/runtime_diag.c
 * @brief Lightweight runtime heap/stack/timing diagnostics.
 */

#include "runtime_diag.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "runtime_diag";

#ifndef CONFIG_LOOKAI_RUNTIME_DIAG_STACK_REPORT
#define CONFIG_LOOKAI_RUNTIME_DIAG_STACK_REPORT 0
#endif

#ifndef CONFIG_LOOKAI_RUNTIME_DIAG_STACK_REPORT_INTERVAL_MS
#define CONFIG_LOOKAI_RUNTIME_DIAG_STACK_REPORT_INTERVAL_MS 1000
#endif

#ifndef CONFIG_LOOKAI_RUNTIME_DIAG_STACK_REPORT_MAX_TASKS
#define CONFIG_LOOKAI_RUNTIME_DIAG_STACK_REPORT_MAX_TASKS 48
#endif

#ifndef CONFIG_LOOKAI_RUNTIME_DIAG_STACK_REPORT_TASK_STACK_BYTES
#define CONFIG_LOOKAI_RUNTIME_DIAG_STACK_REPORT_TASK_STACK_BYTES 4096
#endif

#ifndef CONFIG_LOOKAI_RUNTIME_DIAG_STACK_REPORT_PRIORITY
#define CONFIG_LOOKAI_RUNTIME_DIAG_STACK_REPORT_PRIORITY 1
#endif

#ifndef CONFIG_LOOKAI_RUNTIME_DIAG_STACK_LOW_WARN_BYTES
#define CONFIG_LOOKAI_RUNTIME_DIAG_STACK_LOW_WARN_BYTES 1024
#endif

typedef struct {
    size_t internal_free;
    size_t internal_largest;
    size_t dma_free;
    size_t dma_largest;
    size_t spiram_free;
    size_t spiram_largest;
    size_t stack_bytes;
} runtime_diag_snapshot_t;

#if CONFIG_LOOKAI_RUNTIME_DIAG_STACK_REPORT
static TaskHandle_t s_stack_report_task_handle = NULL;

#if CONFIG_FREERTOS_USE_TRACE_FACILITY
static TaskStatus_t s_task_status[CONFIG_LOOKAI_RUNTIME_DIAG_STACK_REPORT_MAX_TASKS];
#endif
#endif

static void runtime_diag_take_snapshot(runtime_diag_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    snapshot->internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    snapshot->internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

    snapshot->dma_free = heap_caps_get_free_size(MALLOC_CAP_DMA);
    snapshot->dma_largest = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);

    snapshot->spiram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    snapshot->spiram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

    /* ESP-IDF FreeRTOS stack APIs report stack sizes/high-water marks in bytes. */
    snapshot->stack_bytes = (size_t)uxTaskGetStackHighWaterMark(NULL);
}

#if CONFIG_LOOKAI_RUNTIME_DIAG_STACK_REPORT && CONFIG_FREERTOS_USE_TRACE_FACILITY
static const char *runtime_diag_task_state_name(eTaskState state)
{
    switch (state) {
    case eRunning:
        return "running";
    case eReady:
        return "ready";
    case eBlocked:
        return "blocked";
    case eSuspended:
        return "suspended";
    case eDeleted:
        return "deleted";
    case eInvalid:
    default:
        return "invalid";
    }
}

static int runtime_diag_compare_stack_hwm(const void *left, const void *right)
{
    const TaskStatus_t *a = (const TaskStatus_t *)left;
    const TaskStatus_t *b = (const TaskStatus_t *)right;

    if (a->usStackHighWaterMark < b->usStackHighWaterMark) {
        return -1;
    }
    if (a->usStackHighWaterMark > b->usStackHighWaterMark) {
        return 1;
    }
    const char *a_name = a->pcTaskName != NULL ? a->pcTaskName : "";
    const char *b_name = b->pcTaskName != NULL ? b->pcTaskName : "";
    return strncmp(a_name, b_name, configMAX_TASK_NAME_LEN);
}

static void runtime_diag_log_task_stack_report(void)
{
    const UBaseType_t live_task_count = uxTaskGetNumberOfTasks();
    const UBaseType_t capacity = (UBaseType_t)CONFIG_LOOKAI_RUNTIME_DIAG_STACK_REPORT_MAX_TASKS;
    UBaseType_t captured_count = uxTaskGetSystemState(s_task_status, capacity, NULL);

    if (live_task_count > capacity) {
        ESP_LOGW(
            TAG,
            "task_stack_report_truncated | live_tasks=%u captured_tasks=%u capacity=%u",
            (unsigned int)live_task_count,
            (unsigned int)captured_count,
            (unsigned int)capacity
        );
    }

    qsort(s_task_status, captured_count, sizeof(s_task_status[0]), runtime_diag_compare_stack_hwm);

    ESP_LOGI(
        TAG,
        "task_stack_report_begin | live_tasks=%u captured_tasks=%u interval_ms=%u low_warn_bytes=%u",
        (unsigned int)live_task_count,
        (unsigned int)captured_count,
        (unsigned int)CONFIG_LOOKAI_RUNTIME_DIAG_STACK_REPORT_INTERVAL_MS,
        (unsigned int)CONFIG_LOOKAI_RUNTIME_DIAG_STACK_LOW_WARN_BYTES
    );

    for (UBaseType_t i = 0; i < captured_count; i++) {
        const TaskStatus_t *status = &s_task_status[i];
        const bool low_stack = status->usStackHighWaterMark <= CONFIG_LOOKAI_RUNTIME_DIAG_STACK_LOW_WARN_BYTES;
        const char *task_name = status->pcTaskName != NULL ? status->pcTaskName : "unknown";

        if (low_stack) {
            ESP_LOGW(
                TAG,
                "task_stack | name=%s number=%u state=%s priority=%u stack_free_hwm=%u LOW",
                task_name,
                (unsigned int)status->xTaskNumber,
                runtime_diag_task_state_name(status->eCurrentState),
                (unsigned int)status->uxCurrentPriority,
                (unsigned int)status->usStackHighWaterMark
            );
        } else {
            ESP_LOGI(
                TAG,
                "task_stack | name=%s number=%u state=%s priority=%u stack_free_hwm=%u",
                task_name,
                (unsigned int)status->xTaskNumber,
                runtime_diag_task_state_name(status->eCurrentState),
                (unsigned int)status->uxCurrentPriority,
                (unsigned int)status->usStackHighWaterMark
            );
        }
    }

    ESP_LOGI(TAG, "task_stack_report_end");
}

static void runtime_diag_stack_report_task(void *arg)
{
    (void)arg;

    while (true) {
        runtime_diag_log_task_stack_report();
        vTaskDelay(pdMS_TO_TICKS(CONFIG_LOOKAI_RUNTIME_DIAG_STACK_REPORT_INTERVAL_MS));
    }
}
#endif

int64_t runtime_diag_now_us(void)
{
    return esp_timer_get_time();
}

esp_err_t runtime_diag_start_task_stack_report(void)
{
#if CONFIG_LOOKAI_RUNTIME_DIAG_STACK_REPORT
#if CONFIG_FREERTOS_USE_TRACE_FACILITY
    if (s_stack_report_task_handle != NULL) {
        return ESP_OK;
    }

    BaseType_t created = xTaskCreate(
        runtime_diag_stack_report_task,
        "stack_diag",
        CONFIG_LOOKAI_RUNTIME_DIAG_STACK_REPORT_TASK_STACK_BYTES,
        NULL,
        CONFIG_LOOKAI_RUNTIME_DIAG_STACK_REPORT_PRIORITY,
        &s_stack_report_task_handle
    );

    if (created != pdPASS) {
        s_stack_report_task_handle = NULL;
        ESP_LOGE(TAG, "Failed to create stack report task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(
        TAG,
        "Started FreeRTOS task stack report | interval_ms=%u max_tasks=%u",
        (unsigned int)CONFIG_LOOKAI_RUNTIME_DIAG_STACK_REPORT_INTERVAL_MS,
        (unsigned int)CONFIG_LOOKAI_RUNTIME_DIAG_STACK_REPORT_MAX_TASKS
    );
    return ESP_OK;
#else
    ESP_LOGW(
        TAG,
        "FreeRTOS task stack report is enabled, but CONFIG_FREERTOS_USE_TRACE_FACILITY is not enabled"
    );
    return ESP_ERR_NOT_SUPPORTED;
#endif
#else
    return ESP_OK;
#endif
}

void runtime_diag_log(const char *point)
{
    const char *name = point != NULL ? point : "unknown";
    runtime_diag_snapshot_t snapshot = {0};
    runtime_diag_take_snapshot(&snapshot);

    ESP_LOGI(
        TAG,
        "%s | internal_free=%u internal_largest=%u dma_free=%u dma_largest=%u spiram_free=%u spiram_largest=%u stack_free=%u",
        name,
        (unsigned int)snapshot.internal_free,
        (unsigned int)snapshot.internal_largest,
        (unsigned int)snapshot.dma_free,
        (unsigned int)snapshot.dma_largest,
        (unsigned int)snapshot.spiram_free,
        (unsigned int)snapshot.spiram_largest,
        (unsigned int)snapshot.stack_bytes
    );
}

void runtime_diag_log_duration(const char *point, int64_t start_us)
{
    const char *name = point != NULL ? point : "unknown";
    int64_t now_us = runtime_diag_now_us();
    int64_t duration_us = now_us - start_us;
    if (duration_us < 0) {
        duration_us = 0;
    }

    runtime_diag_snapshot_t snapshot = {0};
    runtime_diag_take_snapshot(&snapshot);

    ESP_LOGI(
        TAG,
        "%s | duration_us=%lld duration_ms=%lld internal_free=%u internal_largest=%u dma_free=%u dma_largest=%u spiram_free=%u spiram_largest=%u stack_free=%u",
        name,
        (long long)duration_us,
        (long long)(duration_us / 1000),
        (unsigned int)snapshot.internal_free,
        (unsigned int)snapshot.internal_largest,
        (unsigned int)snapshot.dma_free,
        (unsigned int)snapshot.dma_largest,
        (unsigned int)snapshot.spiram_free,
        (unsigned int)snapshot.spiram_largest,
        (unsigned int)snapshot.stack_bytes
    );
}

void runtime_diag_log_sample(
    const char *point,
    int64_t elapsed_us,
    int32_t value_a,
    int32_t value_b,
    uint32_t count
)
{
    const char *name = point != NULL ? point : "unknown";
    if (elapsed_us < 0) {
        elapsed_us = 0;
    }

    ESP_LOGI(
        TAG,
        "%s | elapsed_us=%lld elapsed_ms=%lld value_a=%ld value_b=%ld count=%u",
        name,
        (long long)elapsed_us,
        (long long)(elapsed_us / 1000),
        (long)value_a,
        (long)value_b,
        (unsigned int)count
    );
}
