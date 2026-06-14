/**
 * @file ui/lookai_display.h
 * @brief LookAI display startup wrapper with smaller LVGL draw buffers.
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the Waveshare display through the LVGL adapter using a smaller
 *        partial draw-buffer profile than the BSP default.
 *
 * The BSP default uses a 50-line PSRAM double buffer. That profile can force
 * large temporary SPI DMA allocations during full/body redraws. This wrapper
 * keeps PSRAM double buffering, but reduces the LVGL buffer height so each SPI
 * color transfer is much smaller.
 *
 * @return LVGL display handle on success, NULL on failure.
 */
lv_display_t *lookai_display_start(void);

#ifdef __cplusplus
}
#endif
