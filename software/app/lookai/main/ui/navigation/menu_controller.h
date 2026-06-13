/**
 * @file ui/navigation/menu_controller.h
 * @brief Small stack-based menu navigation controller.
 */

#pragma once

#include <stdbool.h>

#include "esp_err.h"

#define MENU_CONTROLLER_MAX_DEPTH 8

/**
 * @brief Identifiers for UI screens managed by the menu controller.
 */
typedef enum {
    MENU_SCREEN_SETTINGS = 0,       /**< Root settings screen. */
    MENU_SCREEN_WIFI,               /**< Wi-Fi status/settings screen. */
    MENU_SCREEN_SAVED_NETWORKS,     /**< Saved Wi-Fi networks screen. */
    MENU_SCREEN_BRIGHTNESS,         /**< Brightness settings screen. */
    MENU_SCREEN_STT,                /**< Speech-to-text screen. */
} menu_screen_t;

/**
 * @brief Callback fired when current screen changes.
 */
typedef void (*menu_controller_changed_cb_t)(menu_screen_t screen, void *user_ctx);

/**
 * @brief Stack-based menu navigation state.
 */
typedef struct {
    menu_screen_t stack[MENU_CONTROLLER_MAX_DEPTH]; /**< Screen stack. */
    int top;                                        /**< Current stack top. */
    menu_controller_changed_cb_t changed_cb;        /**< Optional change callback. */
    void *user_ctx;                                 /**< User context for callback. */
} menu_controller_t;

/**
 * @brief Initialize a menu controller.
 */
void menu_controller_init(
    menu_controller_t *controller,
    menu_screen_t root,
    menu_controller_changed_cb_t changed_cb,
    void *user_ctx
);

/**
 * @brief Get the current screen.
 */
menu_screen_t menu_controller_current(const menu_controller_t *controller);

/**
 * @brief Push a new screen onto the navigation stack.
 */
esp_err_t menu_controller_push(menu_controller_t *controller, menu_screen_t screen);

/**
 * @brief Pop one screen from the navigation stack.
 */
esp_err_t menu_controller_pop(menu_controller_t *controller);

/**
 * @brief Reset the navigation stack to a root screen.
 */
esp_err_t menu_controller_reset(menu_controller_t *controller, menu_screen_t root);

/**
 * @brief Replace the current screen without changing stack depth.
 */
esp_err_t menu_controller_show(menu_controller_t *controller, menu_screen_t screen);

/**
 * @brief Return whether back navigation is available.
 */
bool menu_controller_can_go_back(const menu_controller_t *controller);
