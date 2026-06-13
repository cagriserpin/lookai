/**
 * @file ui/ui_manager.c
 * @brief LVGL UI coordinator and scaffold-based menu routing implementation.
 */

#include "ui_manager.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "lvgl.h"

#include "brightness/brightness_screen.h"
#include "wifi/manage_networks_screen.h"
#include "menu_controller.h"
#include "settings_screen.h"
#include "stt/stt_screen.h"
#include "tts/tts_screen.h"
#include "ui_scaffold.h"
#include "ui_theme.h"
#include "wifi/wifi_settings_screen.h"

static const char *TAG = "ui_manager";

static menu_controller_t s_menu;
static ui_manager_callbacks_t s_callbacks = {0};

/**
 * @brief Current scaffold body object.
 *
 * The scaffold/title/back slices are recreated only on navigation. Normal UI
 * data updates repaint only this body object, which keeps the title animation
 * alive.
 */
static lv_obj_t *s_body = NULL;

static ui_manager_state_t s_state = {
    .wifi_status = "Starting",
    .wifi_ssid = "",
    .wifi_ip = "",
    .saved_count = 0,
    .portal_active = false,
    .brightness_percent = 100,
    .stt_status = "Ready",
    .stt_result = "",
    .stt_recording = false,
    .stt_processing = false,
    .stt_speaker_test_active = false,
    .stt_recording_playback_active = false,
    .tts_status = "Ready",
    .tts_result = "Select a sample text.",
    .tts_busy = false,
    .saved_items_count = 0,
};

static lv_obj_t *get_active_screen(void)
{
#if LVGL_VERSION_MAJOR >= 9
    return lv_screen_active();
#else
    return lv_scr_act();
#endif
}

static const char *get_current_title(void)
{
    switch (menu_controller_current(&s_menu)) {
        case MENU_SCREEN_WIFI:
            return "Wi-Fi";

        case MENU_SCREEN_SAVED_NETWORKS:
            return "Saved Wi-Fi Networks Scroll Animation Demo";

        case MENU_SCREEN_BRIGHTNESS:
            return "Brightness";

        case MENU_SCREEN_STT:
            return "Speech to Text";

        case MENU_SCREEN_TTS:
            return "Text to Speech";

        case MENU_SCREEN_SETTINGS:
        default:
            return "Settings";
    }
}

static ui_scaffold_title_icon_t get_current_title_icon(void)
{
    switch (menu_controller_current(&s_menu)) {
        case MENU_SCREEN_WIFI:
        case MENU_SCREEN_SAVED_NETWORKS:
            return UI_SCAFFOLD_TITLE_ICON_WIFI;

        case MENU_SCREEN_BRIGHTNESS:
            return UI_SCAFFOLD_TITLE_ICON_BRIGHTNESS;

        case MENU_SCREEN_STT:
        case MENU_SCREEN_TTS:
            return UI_SCAFFOLD_TITLE_ICON_NONE;

        case MENU_SCREEN_SETTINGS:
        default:
            return UI_SCAFFOLD_TITLE_ICON_SETTINGS;
    }
}

static void render_current_unlocked(void);

static void menu_changed_cb(menu_screen_t screen, void *user_ctx)
{
    (void)screen;
    (void)user_ctx;

    render_current_unlocked();
}

static void apply_display_brightness(int brightness_percent)
{
    if (brightness_percent < 10) {
        brightness_percent = 10;
    }

    if (brightness_percent > 100) {
        brightness_percent = 100;
    }

    esp_err_t err = bsp_display_brightness_set(brightness_percent);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set display brightness to %d%%: %s",
                 brightness_percent, esp_err_to_name(err));
    }
}

static void wifi_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        menu_controller_push(&s_menu, MENU_SCREEN_WIFI);
    }
}

static void brightness_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        menu_controller_push(&s_menu, MENU_SCREEN_BRIGHTNESS);
    }
}

static void stt_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        menu_controller_push(&s_menu, MENU_SCREEN_STT);
    }
}

static void tts_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        menu_controller_push(&s_menu, MENU_SCREEN_TTS);
    }
}

static void back_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        menu_controller_pop(&s_menu);
    }
}

static void manage_saved_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        menu_controller_push(&s_menu, MENU_SCREEN_SAVED_NETWORKS);
    }
}

static void brightness_slider_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(event);
    lv_obj_t *value_label = (lv_obj_t *)lv_event_get_user_data(event);

    int32_t value = lv_slider_get_value(slider);

    s_state.brightness_percent = (int)value;
    apply_display_brightness(s_state.brightness_percent);

    if (value_label != NULL) {
        char value_text[16];
        snprintf(value_text, sizeof(value_text), "%ld%%", (long)value);
        lv_label_set_text(value_label, value_text);
    }
}

static void portal_toggle_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

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

static void append_body_scroll_spacer(lv_obj_t *body)
{
    /*
     * LVGL bottom padding on a scrollable flex container is not always enough
     * to create a comfortable scroll tail. A real invisible child guarantees
     * that the last UI element can scroll above the round screen's bottom edge.
     */
    lv_obj_t *spacer = lv_obj_create(body);

    lv_obj_set_size(spacer, 1, UI_THEME_BODY_BOTTOM_SCROLL_PADDING);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_style_pad_all(spacer, 0, 0);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_CLICKABLE);
}

static void render_body_unlocked(lv_obj_t *body)
{
    if (body == NULL) {
        return;
    }

    menu_screen_t screen_id = menu_controller_current(&s_menu);

    if (screen_id == MENU_SCREEN_SETTINGS) {
        settings_screen_render(
            body,
            &s_state,
            &s_callbacks,
            wifi_button_event_cb,
            brightness_button_event_cb,
            stt_button_event_cb,
            tts_button_event_cb
        );
    } else if (screen_id == MENU_SCREEN_WIFI) {
        wifi_settings_screen_render(
            body,
            &s_state,
            &s_callbacks,
            manage_saved_event_cb,
            portal_toggle_event_cb
        );
    } else if (screen_id == MENU_SCREEN_BRIGHTNESS) {
        brightness_screen_render(
            body,
            &s_state,
            brightness_slider_event_cb
        );
    } else if (screen_id == MENU_SCREEN_STT) {
        stt_screen_render(
            body,
            &s_state,
            &s_callbacks
        );
    } else if (screen_id == MENU_SCREEN_TTS) {
        tts_screen_render(
            body,
            &s_state,
            &s_callbacks
        );
    } else {
        manage_networks_screen_render(
            body,
            &s_state,
            &s_callbacks,
            connect_saved_event_cb,
            forget_network_event_cb,
            portal_toggle_event_cb
        );
    }

    if (screen_id != MENU_SCREEN_STT && screen_id != MENU_SCREEN_TTS) {
        append_body_scroll_spacer(body);
    }
}

static void render_current_unlocked(void)
{
    lv_obj_t *screen = get_active_screen();

    ui_scaffold_config_t scaffold_config = {
        .title = get_current_title(),
        .title_icon = get_current_title_icon(),
        .show_back = menu_controller_can_go_back(&s_menu),
        .show_bottom_slice = false,
        .back_cb = back_event_cb,
    };

    s_body = NULL;
    s_body = ui_scaffold_create(screen, &scaffold_config);

    render_body_unlocked(s_body);
}

static void render_current_locked(void)
{
    if (bsp_display_lock(1000) != ESP_OK) {
        return;
    }

    render_current_unlocked();

    bsp_display_unlock();
}

static void render_body_only_locked(void)
{
    if (bsp_display_lock(1000) != ESP_OK) {
        return;
    }

    if (s_body == NULL) {
        render_current_unlocked();
        bsp_display_unlock();
        return;
    }

    lv_obj_clean(s_body);
    render_body_unlocked(s_body);

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
    apply_display_brightness(s_state.brightness_percent);

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
    bool status_changed = false;
    bool ssid_changed = false;
    bool ip_changed = false;
    bool saved_count_changed = false;
    bool portal_changed = false;

    if (status != NULL && strcmp(s_state.wifi_status, status) != 0) {
        strncpy(s_state.wifi_status, status, sizeof(s_state.wifi_status) - 1);
        s_state.wifi_status[sizeof(s_state.wifi_status) - 1] = '\0';
        status_changed = true;
    }

    if (ssid != NULL && strcmp(s_state.wifi_ssid, ssid) != 0) {
        strncpy(s_state.wifi_ssid, ssid, sizeof(s_state.wifi_ssid) - 1);
        s_state.wifi_ssid[sizeof(s_state.wifi_ssid) - 1] = '\0';
        ssid_changed = true;
    }

    if (ip != NULL && strcmp(s_state.wifi_ip, ip) != 0) {
        strncpy(s_state.wifi_ip, ip, sizeof(s_state.wifi_ip) - 1);
        s_state.wifi_ip[sizeof(s_state.wifi_ip) - 1] = '\0';
        ip_changed = true;
    }

    if (s_state.saved_count != saved_count) {
        s_state.saved_count = saved_count;
        saved_count_changed = true;
    }

    if (s_state.portal_active != portal_active) {
        s_state.portal_active = portal_active;
        portal_changed = true;
    }

    bool changed =
        status_changed ||
        ssid_changed ||
        ip_changed ||
        saved_count_changed ||
        portal_changed;

    if (!changed) {
        return;
    }

    menu_screen_t current = menu_controller_current(&s_menu);
    bool should_render = true;

    if (current == MENU_SCREEN_SAVED_NETWORKS) {
        should_render = portal_changed;
    } else if (
        current == MENU_SCREEN_BRIGHTNESS ||
        current == MENU_SCREEN_STT ||
        current == MENU_SCREEN_TTS
    ) {
        should_render = false;
    }

    if (should_render) {
        render_body_only_locked();
    }
}

void ui_manager_update_stt_status(
    const char *status,
    const char *result,
    bool recording,
    bool processing,
    bool speaker_test_active,
    bool recording_playback_active
)
{
    bool changed = false;

    if (status != NULL && strcmp(s_state.stt_status, status) != 0) {
        strncpy(s_state.stt_status, status, sizeof(s_state.stt_status) - 1);
        s_state.stt_status[sizeof(s_state.stt_status) - 1] = '\0';
        changed = true;
    }

    if (result != NULL && strcmp(s_state.stt_result, result) != 0) {
        strncpy(s_state.stt_result, result, sizeof(s_state.stt_result) - 1);
        s_state.stt_result[sizeof(s_state.stt_result) - 1] = '\0';
        changed = true;
    }

    if (s_state.stt_recording != recording) {
        s_state.stt_recording = recording;
        changed = true;
    }

    if (s_state.stt_processing != processing) {
        s_state.stt_processing = processing;
        changed = true;
    }

    if (s_state.stt_speaker_test_active != speaker_test_active) {
        s_state.stt_speaker_test_active = speaker_test_active;
        changed = true;
    }

    if (s_state.stt_recording_playback_active != recording_playback_active) {
        s_state.stt_recording_playback_active = recording_playback_active;
        changed = true;
    }

    if (!changed) {
        return;
    }

    if (menu_controller_current(&s_menu) == MENU_SCREEN_STT) {
        if (
            recording &&
            !processing &&
            !speaker_test_active &&
            !recording_playback_active
        ) {
            return;
        }

        render_body_only_locked();
    }
}


void ui_manager_update_tts_status(
    const char *status,
    const char *result,
    bool busy
)
{
    bool changed = false;

    if (status != NULL && strcmp(s_state.tts_status, status) != 0) {
        strncpy(s_state.tts_status, status, sizeof(s_state.tts_status) - 1);
        s_state.tts_status[sizeof(s_state.tts_status) - 1] = '\0';
        changed = true;
    }

    if (result != NULL && strcmp(s_state.tts_result, result) != 0) {
        strncpy(s_state.tts_result, result, sizeof(s_state.tts_result) - 1);
        s_state.tts_result[sizeof(s_state.tts_result) - 1] = '\0';
        changed = true;
    }

    if (s_state.tts_busy != busy) {
        s_state.tts_busy = busy;
        changed = true;
    }

    if (!changed) {
        return;
    }

    if (menu_controller_current(&s_menu) == MENU_SCREEN_TTS) {
        render_body_only_locked();
    }
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

    bool changed = s_state.saved_items_count != count;

    if (!changed) {
        for (int i = 0; i < count; i++) {
            if (
                strcmp(s_state.saved_items[i].ssid, items[i].ssid) != 0 ||
                s_state.saved_items[i].connected != items[i].connected
            ) {
                changed = true;
                break;
            }
        }
    }

    if (!changed) {
        return;
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

    /*
     * Saved item contents only affect the saved-networks screen. Rendering the
     * current body on every cache update caused extra display flushes during
     * Wi-Fi connect events, exactly when internal DMA-capable memory is tight.
     */
    if (menu_controller_current(&s_menu) == MENU_SCREEN_SAVED_NETWORKS) {
        render_body_only_locked();
    }
}
