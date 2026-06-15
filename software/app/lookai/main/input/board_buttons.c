/**
 * @file input/board_buttons.c
 * @brief Hardware BOOT/PWR button polling for LookAI.
 */

#include "board_buttons.h"

#include <stdbool.h>
#include <string.h>

#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_io_expander.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board_buttons";

#define BOARD_BUTTON_BOOT_GPIO GPIO_NUM_0
#define BOARD_BUTTON_PWR_EXIO IO_EXPANDER_PIN_NUM_4
#define BOARD_BUTTON_POLL_MS 20
#define BOARD_BUTTON_DEBOUNCE_SAMPLES 2
#define BOARD_BUTTON_TASK_STACK_SIZE 3072
#define BOARD_BUTTON_TASK_PRIORITY 4

typedef struct {
    bool stable_pressed;
    bool last_raw_pressed;
    uint8_t same_count;
} button_debounce_t;

static board_buttons_callbacks_t s_callbacks = {0};
static TaskHandle_t s_button_task_handle = NULL;
static esp_io_expander_handle_t s_io_expander = NULL;

static esp_err_t configure_boot_gpio(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BOARD_BUTTON_BOOT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    return gpio_config(&cfg);
}

static esp_err_t configure_pwr_exio(void)
{
    s_io_expander = bsp_io_expander_init();
    if (s_io_expander == NULL) {
        ESP_LOGW(TAG, "PWR EXIO expander is not available");
        return ESP_OK;
    }

    esp_err_t err = esp_io_expander_set_dir(s_io_expander, BOARD_BUTTON_PWR_EXIO, IO_EXPANDER_INPUT);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to configure PWR EXIO4 as input: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_io_expander_set_pullupdown(s_io_expander, BOARD_BUTTON_PWR_EXIO, IO_EXPANDER_PULL_UP);
    if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "Failed to enable PWR EXIO4 pull-up: %s", esp_err_to_name(err));
    }

    return ESP_OK;
}

static bool read_boot_pressed(void)
{
    return gpio_get_level(BOARD_BUTTON_BOOT_GPIO) == 0;
}

static bool read_pwr_pressed(void)
{
    if (s_io_expander == NULL) {
        return false;
    }

    uint32_t level_mask = 0;
    esp_err_t err = esp_io_expander_get_level(s_io_expander, BOARD_BUTTON_PWR_EXIO, &level_mask);
    if (err != ESP_OK) {
        return false;
    }

    return (level_mask & BOARD_BUTTON_PWR_EXIO) == 0;
}

static bool debounce_update(button_debounce_t *button, bool raw_pressed, bool *pressed)
{
    if (button == NULL || pressed == NULL) {
        return false;
    }

    if (raw_pressed == button->last_raw_pressed) {
        if (button->same_count < BOARD_BUTTON_DEBOUNCE_SAMPLES) {
            button->same_count++;
        }
    } else {
        button->last_raw_pressed = raw_pressed;
        button->same_count = 1;
    }

    if (button->same_count < BOARD_BUTTON_DEBOUNCE_SAMPLES || button->stable_pressed == raw_pressed) {
        return false;
    }

    button->stable_pressed = raw_pressed;
    *pressed = raw_pressed;
    return true;
}

static void button_task(void *arg)
{
    (void)arg;

    button_debounce_t boot = {
        .stable_pressed = read_boot_pressed(),
        .last_raw_pressed = read_boot_pressed(),
        .same_count = BOARD_BUTTON_DEBOUNCE_SAMPLES,
    };
    button_debounce_t pwr = {
        .stable_pressed = read_pwr_pressed(),
        .last_raw_pressed = read_pwr_pressed(),
        .same_count = BOARD_BUTTON_DEBOUNCE_SAMPLES,
    };

    while (true) {
        bool pressed = false;

        if (debounce_update(&boot, read_boot_pressed(), &pressed)) {
            if (pressed) {
                if (s_callbacks.boot_press != NULL) {
                    s_callbacks.boot_press();
                }
            } else if (s_callbacks.boot_release != NULL) {
                s_callbacks.boot_release();
            }
        }

        if (debounce_update(&pwr, read_pwr_pressed(), &pressed) && pressed) {
            if (s_callbacks.pwr_press != NULL) {
                s_callbacks.pwr_press();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(BOARD_BUTTON_POLL_MS));
    }
}

esp_err_t board_buttons_start(const board_buttons_callbacks_t *callbacks)
{
    if (s_button_task_handle != NULL) {
        return ESP_OK;
    }

    if (callbacks != NULL) {
        s_callbacks = *callbacks;
    } else {
        memset(&s_callbacks, 0, sizeof(s_callbacks));
    }

    esp_err_t err = configure_boot_gpio();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to configure BOOT GPIO0: %s", esp_err_to_name(err));
        return err;
    }

    err = configure_pwr_exio();
    if (err != ESP_OK) {
        return err;
    }

    BaseType_t task_ok = xTaskCreate(
        button_task,
        "board_buttons",
        BOARD_BUTTON_TASK_STACK_SIZE,
        NULL,
        BOARD_BUTTON_TASK_PRIORITY,
        &s_button_task_handle
    );

    if (task_ok != pdPASS) {
        s_button_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
