/**
 * @file system/runtime_diag.c
 * @brief Lightweight runtime heap/stack diagnostics.
 */

#include "runtime_diag.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "runtime_diag";

void runtime_diag_log(const char *point)
{
    const char *name = point != NULL ? point : "unknown";

    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

    size_t dma_free = heap_caps_get_free_size(MALLOC_CAP_DMA);
    size_t dma_largest = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);

    size_t spiram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t spiram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

    UBaseType_t stack_words = uxTaskGetStackHighWaterMark(NULL);
    size_t stack_bytes = (size_t)stack_words * sizeof(StackType_t);

    ESP_LOGI(
        TAG,
        "%s | internal_free=%u internal_largest=%u dma_free=%u dma_largest=%u spiram_free=%u spiram_largest=%u stack_free=%u",
        name,
        (unsigned int)internal_free,
        (unsigned int)internal_largest,
        (unsigned int)dma_free,
        (unsigned int)dma_largest,
        (unsigned int)spiram_free,
        (unsigned int)spiram_largest,
        (unsigned int)stack_bytes
    );
}
