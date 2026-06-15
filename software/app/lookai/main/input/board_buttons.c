/**
 * @file input/board_buttons.c
 * @brief Hardware side-button polling for BOOT/GPIO0 and PWR/EXIO4.
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
#define BOARD_BUTTON_PWR_EXIO_MASK IO_EXPANDER_PIN_NUM_4
#define BOARD_BUTTON_POLL_MS 20
#define BOARD_BUTTON_DEBOUNCE_SAMPLES 3
#define BOARD_BUTTON_TASK_STACK_SIZE 3072
#define BOARD_BUTTON_TASK_PRIORITY 4

typedef struct {
    bool initialized;
    bool stable_pressed;
    bool candidate_pressed;
    uint8_t candidate_count;
} debounced_button_t;

static board_buttons_callbacks_t s_callbacks = {0};
static TaskHandle_t s_task_handle = NULL;
static esp_io_expander_handle_t s_io_expander = NULL;
static bool s_pwr_available = false;

static bool debounce_update(debounced_button_t *button, bool raw_pressed, bool *changed)
{
    if (button == NULL || changed == NULL) {
        return false;
    }

    *changed = false;

    if (!button->initialized) {
        button->initialized = true;
        button->stable_pressed = raw_pressed;
        button->candidate_pressed = raw_pressed;
        button->candidate_count = 0;
        return button->stable_pressed;
    }

    if (raw_pressed == button->stable_pressed) {
        button->candidate_pressed = raw_pressed;
        button->candidate_count = 0;
        return button->stable_pressed;
    }

    if (raw_pressed != button->candidate_pressed) {
        button->candidate_pressed = raw_pressed;
        button->candidate_count = 1;
        return button->stable_pressed;
    }

    if (button->candidate_count < BOARD_BUTTON_DEBOUNCE_SAMPLES) {
        button->candidate_count++;
    }

    if (button->candidate_count >= BOARD_BUTTON_DEBOUNCE_SAMPLES) {
        button->stable_pressed = button->candidate_pressed;
        button->candidate_count = 0;
        *changed = true;
    }

    return button->stable_pressed;
}

static esp_err_t configure_boot_button(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << BOARD_BUTTON_BOOT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    return gpio_config(&io_conf);
}

static esp_err_t configure_pwr_button(void)
{
    s_io_expander = bsp_io_expander_init();
    if (s_io_expander == NULL) {
        ESP_LOGW(TAG, "IO expander unavailable; PWR button disabled");
        return ESP_FAIL;
    }

    esp_err_t err = esp_io_expander_set_dir(
        s_io_expander,
        BOARD_BUTTON_PWR_EXIO_MASK,
        IO_EXPANDER_INPUT
    );
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set PWR/EXIO4 as input: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_io_expander_set_pullupdown(
        s_io_expander,
        BOARD_BUTTON_PWR_EXIO_MASK,
        IO_EXPANDER_PULL_UP
    );
    if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "Failed to enable PWR/EXIO4 pull-up: %s", esp_err_to_name(err));
    }

    return ESP_OK;
}

static bool read_boot_pressed(void)
{
    return gpio_get_level(BOARD_BUTTON_BOOT_GPIO) == 0;
}

static bool read_pwr_pressed(void)
{
    if (!s_pwr_available || s_io_expander == NULL) {
        return false;
    }

    uint32_t level_mask = 0;
    esp_err_t err = esp_io_expander_get_level(
        s_io_expander,
        BOARD_BUTTON_PWR_EXIO_MASK,
        &level_mask
    );
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read PWR/EXIO4: %s", esp_err_to_name(err));
        return false;
    }

    return (level_mask & BOARD_BUTTON_PWR_EXIO_MASK) == 0;
}

static void board_buttons_task(void *arg)
{
    (void)arg;

    debounced_button_t boot = {0};
    debounced_button_t pwr = {0};

    (void)debounce_update(&boot, read_boot_pressed(), &(bool){0});
    (void)debounce_update(&pwr, read_pwr_pressed(), &(bool){0});

    while (true) {
        bool changed = false;
        bool boot_pressed = debounce_update(&boot, read_boot_pressed(), &changed);
        if (changed) {
            if (boot_pressed) {
                ESP_LOGI(TAG, "BOOT pressed");
                if (s_callbacks.boot_press != NULL) {
                    s_callbacks.boot_press();
                }
            } else {
                ESP_LOGI(TAG, "BOOT released");
                if (s_callbacks.boot_release != NULL) {
                    s_callbacks.boot_release();
                }
            }
        }

        bool pwr_changed = false;
        bool pwr_pressed = debounce_update(&pwr, read_pwr_pressed(), &pwr_changed);
        if (pwr_changed && pwr_pressed) {
            ESP_LOGI(TAG, "PWR pressed");
            if (s_callbacks.pwr_click != NULL) {
                s_callbacks.pwr_click();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(BOARD_BUTTON_POLL_MS));
    }
}

esp_err_t board_buttons_start(const board_buttons_callbacks_t *callbacks)
{
    if (s_task_handle != NULL) {
        if (callbacks != NULL) {
            s_callbacks = *callbacks;
        }
        return ESP_OK;
    }

    if (callbacks != NULL) {
        s_callbacks = *callbacks;
    } else {
        memset(&s_callbacks, 0, sizeof(s_callbacks));
    }

    esp_err_t err = configure_boot_button();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure BOOT/GPIO0: %s", esp_err_to_name(err));
        return err;
    }

    s_pwr_available = configure_pwr_button() == ESP_OK;

    BaseType_t ok = xTaskCreate(
        board_buttons_task,
        "board_buttons",
        BOARD_BUTTON_TASK_STACK_SIZE,
        NULL,
        BOARD_BUTTON_TASK_PRIORITY,
        &s_task_handle
    );
    if (ok != pdPASS) {
        s_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Hardware buttons ready: BOOT=GPIO0, PWR=EXIO4%s", s_pwr_available ? "" : " (PWR disabled)");
    return ESP_OK;
}
