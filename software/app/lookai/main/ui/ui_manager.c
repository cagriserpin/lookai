/**
 * @file ui/ui_manager.c
 * @brief LVGL UI coordinator and scaffold-based menu routing implementation.
 */

#include "ui_manager.h"

#include "runtime_diag.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "lvgl.h"

#include "lookai_display.h"

#include "ai_screen.h"
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

#ifndef CONFIG_LOOKAI_RUNTIME_DIAG_ENABLE
#define CONFIG_LOOKAI_RUNTIME_DIAG_ENABLE 0
#endif

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

typedef struct {
    bool active;
    int64_t start_us;
    int64_t last_sample_us;
    uint32_t event_count;
    uint32_t last_sample_count;
    int32_t start_y;
    int32_t last_y;
} ui_scroll_diag_t;

static ui_scroll_diag_t s_scroll_diag = {0};

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
    .ai_status = "Ready",
    .ai_result = "Hold TALK to ask AI.",
    .ai_recording = false,
    .ai_busy = false,
    .ai_speaking = false,
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

static const char *screen_to_name(menu_screen_t screen)
{
    switch (screen) {
        case MENU_SCREEN_SETTINGS:
            return "settings";

        case MENU_SCREEN_WIFI:
            return "wifi";

        case MENU_SCREEN_SAVED_NETWORKS:
            return "saved_networks";

        case MENU_SCREEN_BRIGHTNESS:
            return "brightness";

        case MENU_SCREEN_STT:
            return "stt";

        case MENU_SCREEN_AI:
            return "ai";

        case MENU_SCREEN_TTS:
            return "tts";

        default:
            return "unknown";
    }
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

        case MENU_SCREEN_AI:
            return "AI Assistant";

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
        case MENU_SCREEN_AI:
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
        runtime_diag_log("button_settings_wifi_clicked");
        runtime_diag_log("ui_wifi_click_before_push");
        menu_controller_push(&s_menu, MENU_SCREEN_WIFI);
        runtime_diag_log("ui_wifi_click_after_push");
    }
}

static void brightness_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        runtime_diag_log("button_settings_brightness_clicked");
        menu_controller_push(&s_menu, MENU_SCREEN_BRIGHTNESS);
    }
}

static void stt_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        runtime_diag_log("button_settings_stt_clicked");
        runtime_diag_log("ui_stt_click_before_push");
        menu_controller_push(&s_menu, MENU_SCREEN_STT);
        runtime_diag_log("ui_stt_click_after_push");
    }
}

static void ai_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        runtime_diag_log("button_settings_ai_clicked");
        menu_controller_push(&s_menu, MENU_SCREEN_AI);
    }
}

static void tts_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        runtime_diag_log("button_settings_tts_clicked");
        menu_controller_push(&s_menu, MENU_SCREEN_TTS);
    }
}

static void back_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        runtime_diag_log("button_back_clicked");
        runtime_diag_log("ui_back_before_pop");
        menu_controller_pop(&s_menu);
        runtime_diag_log("ui_back_after_pop");
    }
}

static void manage_saved_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        runtime_diag_log("button_wifi_manage_saved_clicked");
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

    runtime_diag_log(
        s_state.portal_active ?
            "button_wifi_close_portal_clicked" :
            "button_wifi_connect_another_clicked"
    );

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

    runtime_diag_log("button_saved_network_connect_clicked");

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

    runtime_diag_log("button_saved_network_forget_clicked");

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

static void body_scroll_diag_event_cb(lv_event_t *event)
{
    if (event == NULL) {
        return;
    }

    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *body = (lv_obj_t *)lv_event_get_target(event);
    if (body == NULL) {
        return;
    }

    int32_t scroll_y = lv_obj_get_scroll_y(body);
    menu_screen_t screen = menu_controller_current(&s_menu);

    if (code == LV_EVENT_SCROLL_BEGIN) {
        memset(&s_scroll_diag, 0, sizeof(s_scroll_diag));
        s_scroll_diag.active = true;
        s_scroll_diag.start_us = runtime_diag_now_us();
        s_scroll_diag.last_sample_us = s_scroll_diag.start_us;
        s_scroll_diag.start_y = scroll_y;
        s_scroll_diag.last_y = scroll_y;

        ESP_LOGI(
            TAG,
            "scroll_begin screen=%s y=%ld",
            screen_to_name(screen),
            (long)scroll_y
        );
        return;
    }

    if (code == LV_EVENT_SCROLL) {
        if (!s_scroll_diag.active) {
            s_scroll_diag.active = true;
            s_scroll_diag.start_us = runtime_diag_now_us();
            s_scroll_diag.last_sample_us = s_scroll_diag.start_us;
            s_scroll_diag.start_y = scroll_y;
            s_scroll_diag.last_y = scroll_y;
        }

        s_scroll_diag.event_count++;
        s_scroll_diag.last_y = scroll_y;

        int64_t now_us = runtime_diag_now_us();
        if (now_us - s_scroll_diag.last_sample_us >= 500000) {
            uint32_t sample_events = s_scroll_diag.event_count - s_scroll_diag.last_sample_count;
            runtime_diag_log_sample(
                "ui_scroll_sample",
                now_us - s_scroll_diag.last_sample_us,
                (int32_t)screen,
                scroll_y,
                sample_events
            );
            s_scroll_diag.last_sample_us = now_us;
            s_scroll_diag.last_sample_count = s_scroll_diag.event_count;
        }
        return;
    }

    if (code == LV_EVENT_SCROLL_END) {
        int64_t now_us = runtime_diag_now_us();
        int64_t elapsed_us = s_scroll_diag.active ? now_us - s_scroll_diag.start_us : 0;
        ESP_LOGI(
            TAG,
            "scroll_end screen=%s elapsed_us=%lld elapsed_ms=%lld events=%u start_y=%ld end_y=%ld delta_y=%ld",
            screen_to_name(screen),
            (long long)elapsed_us,
            (long long)(elapsed_us / 1000),
            (unsigned int)s_scroll_diag.event_count,
            (long)s_scroll_diag.start_y,
            (long)scroll_y,
            (long)(scroll_y - s_scroll_diag.start_y)
        );
        memset(&s_scroll_diag, 0, sizeof(s_scroll_diag));
    }
}

static void attach_body_diag(lv_obj_t *body)
{
#if CONFIG_LOOKAI_RUNTIME_DIAG_ENABLE
    if (body == NULL) {
        return;
    }

    lv_obj_add_event_cb(body, body_scroll_diag_event_cb, LV_EVENT_SCROLL_BEGIN, NULL);
    lv_obj_add_event_cb(body, body_scroll_diag_event_cb, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(body, body_scroll_diag_event_cb, LV_EVENT_SCROLL_END, NULL);
#else
    (void)body;
#endif
}

static bool update_current_body_in_place(lv_obj_t *body)
{
    if (body == NULL) {
        return false;
    }

    switch (menu_controller_current(&s_menu)) {
        case MENU_SCREEN_STT:
            return stt_screen_update(body, &s_state, &s_callbacks);

        case MENU_SCREEN_AI:
            return ai_screen_update(body, &s_state, &s_callbacks);

        case MENU_SCREEN_TTS:
            return tts_screen_update(body, &s_state, &s_callbacks);

        default:
            return false;
    }
}

static void render_body_unlocked(lv_obj_t *body)
{
    if (body == NULL) {
        return;
    }

    int64_t total_start_us = runtime_diag_now_us();
    menu_screen_t screen_id = menu_controller_current(&s_menu);

    ESP_LOGI(TAG, "render_body_begin screen=%s", screen_to_name(screen_id));

    if (screen_id == MENU_SCREEN_SETTINGS) {
        int64_t screen_start_us = runtime_diag_now_us();
        settings_screen_render(
            body,
            &s_state,
            &s_callbacks,
            wifi_button_event_cb,
            brightness_button_event_cb,
            stt_button_event_cb,
            ai_button_event_cb,
            tts_button_event_cb
        );
        runtime_diag_log_duration("ui_render_settings_screen", screen_start_us);
    } else if (screen_id == MENU_SCREEN_WIFI) {
        int64_t screen_start_us = runtime_diag_now_us();
        wifi_settings_screen_render(
            body,
            &s_state,
            &s_callbacks,
            manage_saved_event_cb,
            portal_toggle_event_cb
        );
        runtime_diag_log_duration("ui_render_wifi_screen", screen_start_us);
    } else if (screen_id == MENU_SCREEN_BRIGHTNESS) {
        int64_t screen_start_us = runtime_diag_now_us();
        brightness_screen_render(
            body,
            &s_state,
            brightness_slider_event_cb
        );
        runtime_diag_log_duration("ui_render_brightness_screen", screen_start_us);
    } else if (screen_id == MENU_SCREEN_STT) {
        int64_t screen_start_us = runtime_diag_now_us();
        stt_screen_render(
            body,
            &s_state,
            &s_callbacks
        );
        runtime_diag_log_duration("ui_render_stt_screen", screen_start_us);
    } else if (screen_id == MENU_SCREEN_AI) {
        int64_t screen_start_us = runtime_diag_now_us();
        ai_screen_render(
            body,
            &s_state,
            &s_callbacks
        );
        runtime_diag_log_duration("ui_render_ai_screen", screen_start_us);
    } else if (screen_id == MENU_SCREEN_TTS) {
        int64_t screen_start_us = runtime_diag_now_us();
        tts_screen_render(
            body,
            &s_state,
            &s_callbacks
        );
        runtime_diag_log_duration("ui_render_tts_screen", screen_start_us);
    } else {
        int64_t screen_start_us = runtime_diag_now_us();
        manage_networks_screen_render(
            body,
            &s_state,
            &s_callbacks,
            connect_saved_event_cb,
            forget_network_event_cb,
            portal_toggle_event_cb
        );
        runtime_diag_log_duration("ui_render_saved_networks_screen", screen_start_us);
    }

    if (screen_id != MENU_SCREEN_STT && screen_id != MENU_SCREEN_AI && screen_id != MENU_SCREEN_TTS) {
        int64_t spacer_start_us = runtime_diag_now_us();
        append_body_scroll_spacer(body);
        runtime_diag_log_duration("ui_append_scroll_spacer", spacer_start_us);
    }

    runtime_diag_log_duration("ui_render_body_total", total_start_us);
}

static void render_current_unlocked(void)
{
    int64_t total_start_us = runtime_diag_now_us();
    menu_screen_t screen_id = menu_controller_current(&s_menu);

    ESP_LOGI(TAG, "render_current_begin screen=%s", screen_to_name(screen_id));
    runtime_diag_log("ui_render_current_begin");

    lv_obj_t *screen = get_active_screen();

    ui_scaffold_config_t scaffold_config = {
        .title = get_current_title(),
        .title_icon = get_current_title_icon(),
        .show_back = menu_controller_can_go_back(&s_menu),
        .show_bottom_slice = false,
        .back_cb = back_event_cb,
    };

    s_body = NULL;

    int64_t scaffold_start_us = runtime_diag_now_us();
    s_body = ui_scaffold_create(screen, &scaffold_config);
    runtime_diag_log_duration("ui_scaffold_create", scaffold_start_us);

    attach_body_diag(s_body);

    runtime_diag_log("ui_before_render_body");
    int64_t body_start_us = runtime_diag_now_us();
    render_body_unlocked(s_body);
    runtime_diag_log_duration("ui_render_body_call", body_start_us);
    runtime_diag_log("ui_after_render_body");

    runtime_diag_log_duration("ui_render_current_total", total_start_us);
    runtime_diag_log("ui_render_current_end");
}

static void render_current_locked(void)
{
    int64_t total_start_us = runtime_diag_now_us();
    int64_t lock_start_us = runtime_diag_now_us();

    if (bsp_display_lock(1000) != ESP_OK) {
        runtime_diag_log_duration("ui_render_current_lock_timeout", lock_start_us);
        return;
    }

    runtime_diag_log_duration("ui_render_current_lock_wait", lock_start_us);

    render_current_unlocked();

    int64_t unlock_start_us = runtime_diag_now_us();
    bsp_display_unlock();
    runtime_diag_log_duration("ui_render_current_unlock", unlock_start_us);
    runtime_diag_log_duration("ui_render_current_locked_total", total_start_us);
}

static void render_body_only_locked(void)
{
    int64_t total_start_us = runtime_diag_now_us();
    menu_screen_t screen_id = menu_controller_current(&s_menu);

    ESP_LOGI(TAG, "render_body_only_begin screen=%s", screen_to_name(screen_id));
    runtime_diag_log("ui_render_body_only_begin");

    int64_t lock_start_us = runtime_diag_now_us();
    if (bsp_display_lock(1000) != ESP_OK) {
        runtime_diag_log_duration("ui_render_body_only_lock_timeout", lock_start_us);
        return;
    }
    runtime_diag_log_duration("ui_render_body_only_lock_wait", lock_start_us);

    if (s_body == NULL) {
        render_current_unlocked();
        bsp_display_unlock();
        runtime_diag_log_duration("ui_render_body_only_total_missing_body", total_start_us);
        return;
    }

    if (update_current_body_in_place(s_body)) {
        int64_t unlock_start_us = runtime_diag_now_us();
        bsp_display_unlock();
        runtime_diag_log_duration("ui_render_body_only_in_place_unlock", unlock_start_us);
        runtime_diag_log_duration("ui_render_body_only_in_place_total", total_start_us);
        return;
    }

    int32_t old_scroll_y = lv_obj_get_scroll_y(s_body);

    int64_t clean_start_us = runtime_diag_now_us();
    lv_obj_clean(s_body);
    runtime_diag_log_duration("ui_body_clean", clean_start_us);

    runtime_diag_log("ui_before_body_rerender");
    int64_t render_start_us = runtime_diag_now_us();
    render_body_unlocked(s_body);
    runtime_diag_log_duration("ui_body_rerender", render_start_us);
    runtime_diag_log("ui_after_body_rerender");

    if (old_scroll_y != 0) {
        int64_t restore_start_us = runtime_diag_now_us();
        lv_obj_update_layout(s_body);
        lv_obj_scroll_to_y(s_body, old_scroll_y, LV_ANIM_OFF);
        runtime_diag_log_duration("ui_body_restore_scroll", restore_start_us);
        ESP_LOGI(
            TAG,
            "body_scroll_restore screen=%s old_y=%ld new_y=%ld",
            screen_to_name(screen_id),
            (long)old_scroll_y,
            (long)lv_obj_get_scroll_y(s_body)
        );
    }

    int64_t unlock_start_us = runtime_diag_now_us();
    bsp_display_unlock();
    runtime_diag_log_duration("ui_render_body_only_unlock", unlock_start_us);

    runtime_diag_log_duration("ui_render_body_only_total", total_start_us);
    runtime_diag_log("ui_render_body_only_end");
}

esp_err_t ui_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing display UI");
    int64_t init_start_us = runtime_diag_now_us();

    int64_t display_start_us = runtime_diag_now_us();
    lv_display_t *display = lookai_display_start();
    runtime_diag_log_duration("ui_display_start", display_start_us);
    if (display == NULL) {
        ESP_LOGE(TAG, "Display init failed");
        return ESP_FAIL;
    }

    int64_t brightness_start_us = runtime_diag_now_us();
    apply_display_brightness(s_state.brightness_percent);
    runtime_diag_log_duration("ui_apply_initial_brightness", brightness_start_us);

    int64_t lock_start_us = runtime_diag_now_us();
    if (bsp_display_lock(1000) != ESP_OK) {
        runtime_diag_log_duration("ui_initial_lock_timeout", lock_start_us);
        ESP_LOGE(TAG, "Failed to lock display for initial render");
        return ESP_FAIL;
    }
    runtime_diag_log_duration("ui_initial_lock_wait", lock_start_us);

    menu_controller_init(&s_menu, MENU_SCREEN_SETTINGS, menu_changed_cb, NULL);

    int64_t unlock_start_us = runtime_diag_now_us();
    bsp_display_unlock();
    runtime_diag_log_duration("ui_initial_unlock", unlock_start_us);

    /* Keep the panel dark until the first real screen is rendered. With LVGL
     * perf monitor enabled, this avoids exposing a blank/white active screen
     * while the sysmon label is already running.
     */
    int64_t backlight_start_us = runtime_diag_now_us();
    ESP_ERROR_CHECK(bsp_display_backlight_on());
    runtime_diag_log_duration("ui_backlight_on_after_initial_render", backlight_start_us);

    runtime_diag_log_duration("ui_manager_init_total", init_start_us);

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
    bool should_render = false;

    if (current == MENU_SCREEN_WIFI) {
        should_render = true;
    } else if (current == MENU_SCREEN_SAVED_NETWORKS) {
        should_render = portal_changed;
    }

    ESP_LOGI(
        TAG,
        "wifi_status_update current=%s changed(status=%d ssid=%d ip=%d saved=%d portal=%d) render=%d",
        screen_to_name(current),
        status_changed,
        ssid_changed,
        ip_changed,
        saved_count_changed,
        portal_changed,
        should_render
    );

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


void ui_manager_update_ai_status(
    const char *status,
    const char *result,
    bool recording,
    bool busy,
    bool speaking
)
{
    bool changed = false;

    if (status != NULL && strcmp(s_state.ai_status, status) != 0) {
        strncpy(s_state.ai_status, status, sizeof(s_state.ai_status) - 1);
        s_state.ai_status[sizeof(s_state.ai_status) - 1] = '\0';
        changed = true;
    }

    if (result != NULL && strcmp(s_state.ai_result, result) != 0) {
        strncpy(s_state.ai_result, result, sizeof(s_state.ai_result) - 1);
        s_state.ai_result[sizeof(s_state.ai_result) - 1] = '\0';
        changed = true;
    }

    if (s_state.ai_recording != recording) {
        s_state.ai_recording = recording;
        changed = true;
    }

    if (s_state.ai_busy != busy) {
        s_state.ai_busy = busy;
        changed = true;
    }

    if (s_state.ai_speaking != speaking) {
        s_state.ai_speaking = speaking;
        changed = true;
    }

    if (!changed) {
        return;
    }

    if (menu_controller_current(&s_menu) == MENU_SCREEN_AI) {
        if (recording && !busy && !speaking) {
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
    menu_screen_t current = menu_controller_current(&s_menu);
    bool should_render = current == MENU_SCREEN_SAVED_NETWORKS;

    ESP_LOGI(
        TAG,
        "saved_networks_update current=%s count=%d render=%d",
        screen_to_name(current),
        count,
        should_render
    );

    if (should_render) {
        render_body_only_locked();
    }
}
