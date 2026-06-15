/**
 * @file input/board_buttons.h
 * @brief Hardware BOOT/PWR button polling for LookAI.
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*board_button_cb_t)(void);

typedef struct {
    board_button_cb_t boot_press;    /**< GPIO0 BOOT press callback. */
    board_button_cb_t boot_release;  /**< GPIO0 BOOT release callback. */
    board_button_cb_t pwr_press;     /**< EXIO4 PWR press callback. */
} board_buttons_callbacks_t;

/**
 * @brief Start the hardware button polling task.
 */
esp_err_t board_buttons_start(const board_buttons_callbacks_t *callbacks);

#ifdef __cplusplus
}
#endif
