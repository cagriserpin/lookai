/**
 * @file system/runtime_diag.c
 * @brief Lightweight runtime heap/stack/timing diagnostics.
 */

#include "runtime_diag.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "runtime_diag";

typedef struct {
    size_t internal_free;
    size_t internal_largest;
    size_t dma_free;
    size_t dma_largest;
    size_t spiram_free;
    size_t spiram_largest;
    size_t stack_bytes;
} runtime_diag_snapshot_t;

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

    UBaseType_t stack_words = uxTaskGetStackHighWaterMark(NULL);
    snapshot->stack_bytes = (size_t)stack_words * sizeof(StackType_t);
}

int64_t runtime_diag_now_us(void)
{
    return esp_timer_get_time();
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
