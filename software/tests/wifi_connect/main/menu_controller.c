#include "menu_controller.h"

#include <string.h>

static void notify_changed(menu_controller_t *controller)
{
    if (controller != NULL && controller->changed_cb != NULL) {
        controller->changed_cb(menu_controller_current(controller), controller->user_ctx);
    }
}

void menu_controller_init(
    menu_controller_t *controller,
    menu_screen_t root,
    menu_controller_changed_cb_t changed_cb,
    void *user_ctx
)
{
    if (controller == NULL) {
        return;
    }

    memset(controller, 0, sizeof(*controller));
    controller->stack[0] = root;
    controller->top = 0;
    controller->changed_cb = changed_cb;
    controller->user_ctx = user_ctx;

    notify_changed(controller);
}

menu_screen_t menu_controller_current(const menu_controller_t *controller)
{
    if (controller == NULL || controller->top < 0 || controller->top >= MENU_CONTROLLER_MAX_DEPTH) {
        return MENU_SCREEN_SETTINGS;
    }

    return controller->stack[controller->top];
}

esp_err_t menu_controller_push(menu_controller_t *controller, menu_screen_t screen)
{
    if (controller == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (controller->top + 1 >= MENU_CONTROLLER_MAX_DEPTH) {
        controller->stack[controller->top] = screen;
        notify_changed(controller);
        return ESP_OK;
    }

    controller->top++;
    controller->stack[controller->top] = screen;

    notify_changed(controller);

    return ESP_OK;
}

esp_err_t menu_controller_pop(menu_controller_t *controller)
{
    if (controller == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (controller->top > 0) {
        controller->top--;
    }

    notify_changed(controller);

    return ESP_OK;
}

esp_err_t menu_controller_reset(menu_controller_t *controller, menu_screen_t root)
{
    if (controller == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    controller->top = 0;
    controller->stack[0] = root;

    notify_changed(controller);

    return ESP_OK;
}

esp_err_t menu_controller_show(menu_controller_t *controller, menu_screen_t screen)
{
    if (controller == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    controller->stack[controller->top] = screen;

    notify_changed(controller);

    return ESP_OK;
}

bool menu_controller_can_go_back(const menu_controller_t *controller)
{
    return controller != NULL && controller->top > 0;
}
