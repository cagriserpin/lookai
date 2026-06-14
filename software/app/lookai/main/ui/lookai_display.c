/**
 * @file ui/lookai_display.c
 * @brief Custom display initialization for smaller LVGL/SPI flush chunks.
 */

#include "lookai_display.h"

#include <assert.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"

#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "bsp/display.h"
#include "bsp/touch.h"
#include "esp_lv_adapter.h"

/*
 * Stage 3A display profile:
 *
 * BSP default:
 *   buffer_height = 50
 *   use_psram = true
 *   require_double_buffer = true
 *
 * 466 * 50 * 2 = 46600 bytes per flush chunk.
 *
 * During STT/TLS, runtime diagnostics showed dma_largest around 31-35 KB.
 * With a 50-line PSRAM buffer, the SPI driver may need a large temporary
 * DMA-capable private TX buffer and fail. Reducing the partial buffer height
 * lowers each color transfer.
 */
#define LOOKAI_DISPLAY_BUFFER_LINES            16
#define LOOKAI_DISPLAY_TRANSFER_GUARD_LINES     2
#define LOOKAI_DISPLAY_MAX_TRANSFER_LINES      (LOOKAI_DISPLAY_BUFFER_LINES + LOOKAI_DISPLAY_TRANSFER_GUARD_LINES)

static const char *TAG = "lookai_display";

#if LVGL_VERSION_MAJOR >= 9
static void lookai_rounder_event_cb(lv_event_t *e)
{
    lv_area_t *area = (lv_area_t *)lv_event_get_param(e);
    if (area == NULL) {
        return;
    }

    uint16_t x1 = area->x1;
    uint16_t x2 = area->x2;
    uint16_t y1 = area->y1;
    uint16_t y2 = area->y2;

    /*
     * CO5300 QSPI writes are more stable with even-aligned areas. This mirrors
     * the Waveshare BSP rounder callback.
     */
    area->x1 = (x1 >> 1) << 1;
    area->y1 = (y1 >> 1) << 1;
    area->x2 = ((x2 >> 1) << 1) + 1;
    area->y2 = ((y2 >> 1) << 1) + 1;
}
#else
static void lookai_rounder_cb(lv_disp_drv_t *disp_drv, lv_area_t *area)
{
    (void)disp_drv;

    if (area == NULL) {
        return;
    }

    uint16_t x1 = area->x1;
    uint16_t x2 = area->x2;
    uint16_t y1 = area->y1;
    uint16_t y2 = area->y2;

    area->x1 = (x1 >> 1) << 1;
    area->y1 = (y1 >> 1) << 1;
    area->x2 = ((x2 >> 1) << 1) + 1;
    area->y2 = ((y2 >> 1) << 1) + 1;
}
#endif

lv_display_t *lookai_display_start(void)
{
    ESP_LOGI(
        TAG,
        "Starting display with %d-line PSRAM double buffer, max_transfer_lines=%d",
        LOOKAI_DISPLAY_BUFFER_LINES,
        LOOKAI_DISPLAY_MAX_TRANSFER_LINES
    );

    bsp_display_cfg_t bsp_cfg = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_0,
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
        .touch_flags = {
            .swap_xy = 0,
            .mirror_x = 1,
            .mirror_y = 1,
        },
    };

    esp_err_t err = esp_lv_adapter_init(&bsp_cfg.lv_adapter_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LVGL adapter init failed: %s", esp_err_to_name(err));
        return NULL;
    }

    const bsp_display_config_t display_config = {
        .max_transfer_sz =
            BSP_LCD_H_RES *
            LOOKAI_DISPLAY_MAX_TRANSFER_LINES *
            BSP_LCD_BITS_PER_PIXEL / 8,
    };

    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_io_handle_t panel_io = NULL;

    err = bsp_display_new(&display_config, &panel, &panel_io);
    if (err != ESP_OK || panel == NULL || panel_io == NULL) {
        ESP_LOGE(TAG, "LCD panel init failed: %s", esp_err_to_name(err));
        return NULL;
    }

    esp_lv_adapter_display_config_t display_adapter_config = {
        .panel = panel,
        .panel_io = panel_io,
        .profile = {
            .interface = ESP_LV_ADAPTER_PANEL_IF_OTHER,
            .rotation = bsp_cfg.rotation,
            .hor_res = BSP_LCD_H_RES,
            .ver_res = BSP_LCD_V_RES,
            .buffer_height = LOOKAI_DISPLAY_BUFFER_LINES,
            .use_psram = true,
            .enable_ppa_accel = false,
            .require_double_buffer = true,
        },
        .tear_avoid_mode = bsp_cfg.tear_avoid_mode,
        .te_sync = ESP_LV_ADAPTER_TE_SYNC_DISABLED(),
    };

    lv_display_t *display = esp_lv_adapter_register_display(&display_adapter_config);
    if (display == NULL) {
        ESP_LOGE(TAG, "LVGL display registration failed");
        return NULL;
    }

#if LVGL_VERSION_MAJOR >= 9
    lv_display_add_event_cb(display, lookai_rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);
#else
    lv_disp_t *display_v8 = (lv_disp_t *)display;
    if (display_v8 != NULL && display_v8->driver != NULL) {
        display_v8->driver->rounder_cb = lookai_rounder_cb;
    }
#endif

    esp_lcd_touch_handle_t touch = NULL;
    err = bsp_touch_new(&bsp_cfg, &touch);
    if (err != ESP_OK || touch == NULL) {
        ESP_LOGE(TAG, "Touch init failed: %s", esp_err_to_name(err));
        return NULL;
    }

    const esp_lv_adapter_touch_config_t touch_config =
        ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(display, touch);

    lv_indev_t *input = esp_lv_adapter_register_touch(&touch_config);
    if (input == NULL) {
        ESP_LOGE(TAG, "LVGL touch registration failed");
        return NULL;
    }

    err = bsp_display_brightness_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Display brightness init failed: %s", esp_err_to_name(err));
        return NULL;
    }

    err = esp_lv_adapter_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LVGL adapter start failed: %s", esp_err_to_name(err));
        return NULL;
    }

    return display;
}
