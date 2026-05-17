#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"

#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "lvgl.h"

static const char *TAG = "ws175_lvgl";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting display");

    lv_display_t *display = bsp_display_start();
    if (display == NULL) {
        ESP_LOGE(TAG, "Display init failed");
        return;
    }

    ESP_ERROR_CHECK(bsp_display_backlight_on());

    ESP_ERROR_CHECK(bsp_display_lock(1000));

#if LVGL_VERSION_MAJOR >= 9
    lv_obj_t *screen = lv_screen_active();
#else
    lv_obj_t *screen = lv_scr_act();
#endif

    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, "Hello LVGL\nESP32-S3 AMOLED");
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);

    bsp_display_unlock();

    ESP_LOGI(TAG, "UI ready");
}