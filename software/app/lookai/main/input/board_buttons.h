/**
 * @file input/board_buttons.h
 * @brief Hardware side-button polling for BOOT and PWR.
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*board_button_callback_t)(void);

typedef struct {
    board_button_callback_t pwr_click;    /**< Called on debounced PWR/EXIO4 press. */
    board_button_callback_t boot_press;   /**< Called on debounced BOOT/GPIO0 press. */
    board_button_callback_t boot_release; /**< Called on debounced BOOT/GPIO0 release. */
} board_buttons_callbacks_t;

esp_err_t board_buttons_start(const board_buttons_callbacks_t *callbacks);

#ifdef __cplusplus
}
#endif
