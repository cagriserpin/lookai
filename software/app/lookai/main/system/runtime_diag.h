/**
 * @file system/runtime_diag.h
 * @brief Lightweight runtime heap/stack diagnostics.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Log internal, DMA-capable, PSRAM heap and current task stack headroom.
 *
 * This function only logs; it does not change runtime behavior.
 *
 * @param point Short ASCII label describing the measurement point.
 */
void runtime_diag_log(const char *point);

#ifdef __cplusplus
}
#endif
