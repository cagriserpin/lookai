/**
 * @file system/runtime_diag.h
 * @brief Lightweight runtime heap/stack/timing diagnostics.
 */

#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Return a monotonic timestamp in microseconds.
 */
int64_t runtime_diag_now_us(void);


/**
 * @brief Start the optional periodic FreeRTOS task stack high-water-mark report.
 *
 * When enabled from menuconfig, this creates a small diagnostics task that logs
 * all live FreeRTOS tasks every configured interval. It is safe to call more
 * than once.
 *
 * @return ESP_OK if started or disabled, otherwise an ESP-IDF error code.
 */
esp_err_t runtime_diag_start_task_stack_report(void);

/**
 * @brief Log internal, DMA-capable, PSRAM heap and current task stack headroom.
 *
 * This function only logs; it does not change runtime behavior.
 *
 * @param point Short ASCII label describing the measurement point.
 */
void runtime_diag_log(const char *point);

/**
 * @brief Log elapsed time since start_us plus heap/stack headroom.
 *
 * @param point Short ASCII label describing the measured section.
 * @param start_us Timestamp returned by runtime_diag_now_us().
 */
void runtime_diag_log_duration(const char *point, int64_t start_us);

/**
 * @brief Log a compact UI event/sample with elapsed time and counters.
 *
 * @param point Short ASCII label describing the sample.
 * @param elapsed_us Elapsed time in microseconds, or 0 if not applicable.
 * @param value_a First signed metric value.
 * @param value_b Second signed metric value.
 * @param count Event/sample count.
 */
void runtime_diag_log_sample(
    const char *point,
    int64_t elapsed_us,
    int32_t value_a,
    int32_t value_b,
    uint32_t count
);

#ifdef __cplusplus
}
#endif
