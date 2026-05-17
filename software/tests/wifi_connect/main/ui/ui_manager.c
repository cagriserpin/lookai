/**
 * @file ui/ui_manager.c
 * @brief LVGL UI coordinator and menu routing implementation.
 */

#include "ui_manager.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "lvgl.h"

#include "manage_networks_screen.h"
#include "menu_controller.h"
#include "settings_screen.h"
#include "ui_screen.h"
#include "wifi_settings_screen.h"

static const char *TAG = "ui_manager";

static menu_controller_t s_menu;
static ui_manager_callbacks_t s_callbacks = {0};

static ui_manager_state_t s_state = {
    .wifi_status = "Starting",
    .wifi_ssid = "",
    .wifi_ip = "",
    .saved_count = 0,
    .portal_active = false,
    .saved_items_count = 0,
};

/**
 * @brief Return the active LVGL screen across LVGL major versions.
 */
static lv_obj_t *get_active_screen(void)
{
#if LVGL_VERSION_MAJOR >= 9
    return lv_screen_active();
#else
    return lv_scr_act();
#endif
}

/**
 * @brief Render the current screen without taking the display lock.
 */
static void render_current_unlocked(void);

/**
 * @brief Menu-controller change callback.
 */
static void menu_changed_cb(menu_screen_t screen, void *user_ctx)
{
    (void)screen;
    (void)user_ctx;

    render_current_unlocked();
}

/**
 * @brief Open Wi-Fi settings screen.
 */
static void wifi_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        menu_controller_push(&s_menu, MENU_SCREEN_WIFI);
    }
}

/**
 * @brief Navigate one screen back.
 */
static void back_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        menu_controller_pop(&s_menu);
    }
}

/**
 * @brief Open saved networks management screen.
 */
static void manage_saved_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        menu_controller_push(&s_menu, MENU_SCREEN_SAVED_NETWORKS);
    }
}

/**
 * @brief Toggle setup portal state from the UI.
 */
static void portal_toggle_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    /*
     * UX requirement:
     * If Add New Network is pressed from a nested screen, return to Wi-Fi status
     * before enabling setup portal.
     */
    menu_controller_show(&s_menu, MENU_SCREEN_WIFI);

    if (s_state.portal_active) {
        if (s_callbacks.close_portal != NULL) {
            s_callbacks.close_portal();
        }
    } else {
        if (s_callbacks.connect_another != NULL) {
            s_callbacks.connect_another();
        }
    }
}

/**
 * @brief Connect to a saved network selected by the user.
 */
static void connect_saved_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    const char *ssid = (const char *)lv_event_get_user_data(event);
    if (ssid == NULL || ssid[0] == '\0') {
        return;
    }

    menu_controller_show(&s_menu, MENU_SCREEN_WIFI);

    if (s_callbacks.connect_saved != NULL) {
        s_callbacks.connect_saved(ssid);
    }
}

/**
 * @brief Forget a saved network selected by the user.
 */
static void forget_network_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    const char *ssid = (const char *)lv_event_get_user_data(event);
    if (ssid == NULL || ssid[0] == '\0') {
        return;
    }

    if (s_callbacks.forget_saved != NULL) {
        s_callbacks.forget_saved(ssid);
    }
}

/**
 * @brief Render current screen under an already-held display lock.
 */
static void render_current_unlocked(void)
{
    lv_obj_t *screen = get_active_screen();

    ui_screen_prepare(screen);

    menu_screen_t screen_id = menu_controller_current(&s_menu);

    if (screen_id == MENU_SCREEN_SETTINGS) {
        settings_screen_render(
            screen,
            &s_state,
            &s_callbacks,
            wifi_button_event_cb
        );
    } else if (screen_id == MENU_SCREEN_WIFI) {
        wifi_settings_screen_render(
            screen,
            &s_state,
            &s_callbacks,
            menu_controller_can_go_back(&s_menu),
            back_event_cb,
            manage_saved_event_cb,
            portal_toggle_event_cb
        );
    } else {
        manage_networks_screen_render(
            screen,
            &s_state,
            &s_callbacks,
            menu_controller_can_go_back(&s_menu),
            back_event_cb,
            connect_saved_event_cb,
            forget_network_event_cb,
            portal_toggle_event_cb
        );
    }
}

/**
 * @brief Render current screen while taking the display lock.
 */
static void render_current_locked(void)
{
    if (bsp_display_lock(1000) != ESP_OK) {
        return;
    }

    render_current_unlocked();

    bsp_display_unlock();
}

esp_err_t ui_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing display UI");

    lv_display_t *display = bsp_display_start();
    if (display == NULL) {
        ESP_LOGE(TAG, "Display init failed");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(bsp_display_backlight_on());

    /*
     * menu_controller_init() triggers the initial render through its changed
     * callback, so it must run while the LVGL/display lock is held.
     */
    if (bsp_display_lock(1000) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to lock display for initial render");
        return ESP_FAIL;
    }

    menu_controller_init(&s_menu, MENU_SCREEN_SETTINGS, menu_changed_cb, NULL);

    bsp_display_unlock();

    return ESP_OK;
}

void ui_manager_set_callbacks(const ui_manager_callbacks_t *callbacks)
{
    if (callbacks == NULL) {
        memset(&s_callbacks, 0, sizeof(s_callbacks));
        return;
    }

    s_callbacks = *callbacks;
}

void ui_manager_show_settings(void)
{
    if (bsp_display_lock(1000) != ESP_OK) {
        return;
    }

    menu_controller_reset(&s_menu, MENU_SCREEN_SETTINGS);

    bsp_display_unlock();
}

void ui_manager_show_wifi(void)
{
    if (bsp_display_lock(1000) != ESP_OK) {
        return;
    }

    menu_controller_reset(&s_menu, MENU_SCREEN_SETTINGS);
    menu_controller_push(&s_menu, MENU_SCREEN_WIFI);

    bsp_display_unlock();
}

void ui_manager_update_wifi_status(
    const char *status,
    const char *ssid,
    const char *ip,
    int saved_count,
    bool portal_active
)
{
    if (status != NULL) {
        strncpy(s_state.wifi_status, status, sizeof(s_state.wifi_status) - 1);
        s_state.wifi_status[sizeof(s_state.wifi_status) - 1] = '\0';
    }

    if (ssid != NULL) {
        strncpy(s_state.wifi_ssid, ssid, sizeof(s_state.wifi_ssid) - 1);
        s_state.wifi_ssid[sizeof(s_state.wifi_ssid) - 1] = '\0';
    }

    if (ip != NULL) {
        strncpy(s_state.wifi_ip, ip, sizeof(s_state.wifi_ip) - 1);
        s_state.wifi_ip[sizeof(s_state.wifi_ip) - 1] = '\0';
    }

    s_state.saved_count = saved_count;
    s_state.portal_active = portal_active;

    render_current_locked();
}

void ui_manager_set_saved_networks(
    const ui_manager_saved_network_t *items,
    int count
)
{
    if (count < 0) {
        count = 0;
    }

    if (count > UI_MANAGER_MAX_SAVED_NETWORKS) {
        count = UI_MANAGER_MAX_SAVED_NETWORKS;
    }

    memset(s_state.saved_items, 0, sizeof(s_state.saved_items));
    s_state.saved_items_count = count;
    s_state.saved_count = count;

    for (int i = 0; i < count; i++) {
        strncpy(
            s_state.saved_items[i].ssid,
            items[i].ssid,
            sizeof(s_state.saved_items[i].ssid) - 1
        );
        s_state.saved_items[i].ssid[sizeof(s_state.saved_items[i].ssid) - 1] = '\0';
        s_state.saved_items[i].connected = items[i].connected;
    }

    render_current_locked();
}
