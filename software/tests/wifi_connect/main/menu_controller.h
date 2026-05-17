#pragma once

#include <stdbool.h>

#include "esp_err.h"

#define MENU_CONTROLLER_MAX_DEPTH 8

typedef enum {
    MENU_SCREEN_SETTINGS = 0,
    MENU_SCREEN_WIFI,
    MENU_SCREEN_SAVED_NETWORKS,
} menu_screen_t;

typedef void (*menu_controller_changed_cb_t)(menu_screen_t screen, void *user_ctx);

typedef struct {
    menu_screen_t stack[MENU_CONTROLLER_MAX_DEPTH];
    int top;
    menu_controller_changed_cb_t changed_cb;
    void *user_ctx;
} menu_controller_t;

void menu_controller_init(
    menu_controller_t *controller,
    menu_screen_t root,
    menu_controller_changed_cb_t changed_cb,
    void *user_ctx
);

menu_screen_t menu_controller_current(const menu_controller_t *controller);

esp_err_t menu_controller_push(menu_controller_t *controller, menu_screen_t screen);
esp_err_t menu_controller_pop(menu_controller_t *controller);
esp_err_t menu_controller_reset(menu_controller_t *controller, menu_screen_t root);
esp_err_t menu_controller_show(menu_controller_t *controller, menu_screen_t screen);

bool menu_controller_can_go_back(const menu_controller_t *controller);
