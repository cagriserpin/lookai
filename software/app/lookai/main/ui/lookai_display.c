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
#include "runtime_diag.h"

/*
 * Display draw-buffer profile.
 *
 * Keep the permanent DMA-capable internal-RAM reservation small. Larger
 * internal double buffers improved scroll FPS, but they also reduced the
 * largest internal heap block enough for mbedTLS setup to fail during STT/TTS
 * HTTPS connections. Settings-menu render cost is now attacked with PSRAM-backed
 * item snapshot caching instead of reserving a large internal display buffer.
 */
#define LOOKAI_DISPLAY_BUFFER_LINES             64
#define LOOKAI_DISPLAY_TRANSFER_GUARD_LINES      2
#define LOOKAI_DISPLAY_MAX_TRANSFER_LINES       (LOOKAI_DISPLAY_BUFFER_LINES + LOOKAI_DISPLAY_TRANSFER_GUARD_LINES)
#define LOOKAI_DISPLAY_USE_PSRAM_BUFFER         false
#define LOOKAI_DISPLAY_REQUIRE_DOUBLE_BUFFER    true

static const char *TAG = "lookai_display";

#ifndef CONFIG_LOOKAI_DISPLAY_DIAG
#define CONFIG_LOOKAI_DISPLAY_DIAG 0
#endif

#if LVGL_VERSION_MAJOR >= 9
#if CONFIG_LOOKAI_DISPLAY_DIAG
typedef struct {
    int64_t render_start_us;
    int64_t flush_start_us;
    int64_t last_sample_us;
    int64_t flush_total_us;
    int64_t render_total_us;
    uint32_t invalid_area_count;
    uint32_t flush_count;
    uint32_t render_count;
    uint32_t invalid_px_sum;
} lookai_display_diag_t;

static lookai_display_diag_t s_display_diag = {0};

static void lookai_display_diag_event_cb(lv_event_t *event)
{
    if (event == NULL) {
        return;
    }

    lv_event_code_t code = lv_event_get_code(event);
    int64_t now_us = runtime_diag_now_us();

    if (s_display_diag.last_sample_us == 0) {
        s_display_diag.last_sample_us = now_us;
    }

    if (code == LV_EVENT_RENDER_START) {
        s_display_diag.render_start_us = now_us;
        return;
    }

    if (code == LV_EVENT_RENDER_READY) {
        if (s_display_diag.render_start_us > 0) {
            int64_t duration_us = now_us - s_display_diag.render_start_us;
            if (duration_us > 0) {
                s_display_diag.render_total_us += duration_us;
                s_display_diag.render_count++;
            }
            s_display_diag.render_start_us = 0;
        }
    } else if (code == LV_EVENT_FLUSH_START) {
        s_display_diag.flush_start_us = now_us;
        return;
    } else if (code == LV_EVENT_FLUSH_FINISH) {
        if (s_display_diag.flush_start_us > 0) {
            int64_t duration_us = now_us - s_display_diag.flush_start_us;
            if (duration_us > 0) {
                s_display_diag.flush_total_us += duration_us;
                s_display_diag.flush_count++;
            }
            s_display_diag.flush_start_us = 0;
        }
    } else if (code == LV_EVENT_INVALIDATE_AREA) {
        lv_area_t *area = (lv_area_t *)lv_event_get_param(event);
        s_display_diag.invalid_area_count++;
        if (area != NULL) {
            int32_t width = area->x2 - area->x1 + 1;
            int32_t height = area->y2 - area->y1 + 1;
            if (width > 0 && height > 0) {
                uint32_t px = (uint32_t)width * (uint32_t)height;
                s_display_diag.invalid_px_sum += px;
            }
        }
    }

    if (now_us - s_display_diag.last_sample_us >= 1000000) {
        int64_t elapsed_us = now_us - s_display_diag.last_sample_us;
        ESP_LOGI(
            TAG,
            "lvgl_display_sample elapsed_us=%lld elapsed_ms=%lld render_count=%u render_total_us=%lld flush_count=%u flush_total_us=%lld invalid_areas=%u invalid_px=%u",
            (long long)elapsed_us,
            (long long)(elapsed_us / 1000),
            (unsigned int)s_display_diag.render_count,
            (long long)s_display_diag.render_total_us,
            (unsigned int)s_display_diag.flush_count,
            (long long)s_display_diag.flush_total_us,
            (unsigned int)s_display_diag.invalid_area_count,
            (unsigned int)s_display_diag.invalid_px_sum
        );

        s_display_diag.last_sample_us = now_us;
        s_display_diag.render_total_us = 0;
        s_display_diag.flush_total_us = 0;
        s_display_diag.render_count = 0;
        s_display_diag.flush_count = 0;
        s_display_diag.invalid_area_count = 0;
        s_display_diag.invalid_px_sum = 0;
    }
}

#endif /* CONFIG_LOOKAI_DISPLAY_DIAG */

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
    int64_t total_start_us = runtime_diag_now_us();

    ESP_LOGI(
        TAG,
        "Starting display with %d-line %s %s buffer, max_transfer_lines=%d",
        LOOKAI_DISPLAY_BUFFER_LINES,
        LOOKAI_DISPLAY_USE_PSRAM_BUFFER ? "PSRAM" : "internal",
        LOOKAI_DISPLAY_REQUIRE_DOUBLE_BUFFER ? "double" : "single",
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

    int64_t step_start_us = runtime_diag_now_us();
    esp_err_t err = esp_lv_adapter_init(&bsp_cfg.lv_adapter_cfg);
    runtime_diag_log_duration("display_esp_lv_adapter_init", step_start_us);
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

    step_start_us = runtime_diag_now_us();
    err = bsp_display_new(&display_config, &panel, &panel_io);
    runtime_diag_log_duration("display_bsp_display_new", step_start_us);
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
            .use_psram = LOOKAI_DISPLAY_USE_PSRAM_BUFFER,
            .enable_ppa_accel = false,
            .require_double_buffer = LOOKAI_DISPLAY_REQUIRE_DOUBLE_BUFFER,
        },
        .tear_avoid_mode = bsp_cfg.tear_avoid_mode,
        .te_sync = ESP_LV_ADAPTER_TE_SYNC_DISABLED(),
    };

    step_start_us = runtime_diag_now_us();
    lv_display_t *display = esp_lv_adapter_register_display(&display_adapter_config);
    runtime_diag_log_duration("display_register_display", step_start_us);
    if (display == NULL) {
        ESP_LOGE(TAG, "LVGL display registration failed");
        return NULL;
    }

#if LVGL_VERSION_MAJOR >= 9
    lv_display_add_event_cb(display, lookai_rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);
#if CONFIG_LOOKAI_DISPLAY_DIAG
    lv_display_add_event_cb(display, lookai_display_diag_event_cb, LV_EVENT_RENDER_START, NULL);
    lv_display_add_event_cb(display, lookai_display_diag_event_cb, LV_EVENT_RENDER_READY, NULL);
    lv_display_add_event_cb(display, lookai_display_diag_event_cb, LV_EVENT_FLUSH_START, NULL);
    lv_display_add_event_cb(display, lookai_display_diag_event_cb, LV_EVENT_FLUSH_FINISH, NULL);
    lv_display_add_event_cb(display, lookai_display_diag_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);
#endif
#else
    lv_disp_t *display_v8 = (lv_disp_t *)display;
    if (display_v8 != NULL && display_v8->driver != NULL) {
        display_v8->driver->rounder_cb = lookai_rounder_cb;
    }
#endif

    esp_lcd_touch_handle_t touch = NULL;
    step_start_us = runtime_diag_now_us();
    err = bsp_touch_new(&bsp_cfg, &touch);
    runtime_diag_log_duration("display_bsp_touch_new", step_start_us);
    if (err != ESP_OK || touch == NULL) {
        ESP_LOGE(TAG, "Touch init failed: %s", esp_err_to_name(err));
        return NULL;
    }

    const esp_lv_adapter_touch_config_t touch_config =
        ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(display, touch);

    step_start_us = runtime_diag_now_us();
    lv_indev_t *input = esp_lv_adapter_register_touch(&touch_config);
    runtime_diag_log_duration("display_register_touch", step_start_us);
    if (input == NULL) {
        ESP_LOGE(TAG, "LVGL touch registration failed");
        return NULL;
    }

    step_start_us = runtime_diag_now_us();
    err = bsp_display_brightness_init();
    runtime_diag_log_duration("display_brightness_init", step_start_us);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Display brightness init failed: %s", esp_err_to_name(err));
        return NULL;
    }

    step_start_us = runtime_diag_now_us();
    err = esp_lv_adapter_start();
    runtime_diag_log_duration("display_esp_lv_adapter_start", step_start_us);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LVGL adapter start failed: %s", esp_err_to_name(err));
        return NULL;
    }

    runtime_diag_log_duration("display_start_total", total_start_us);

    return display;
}
