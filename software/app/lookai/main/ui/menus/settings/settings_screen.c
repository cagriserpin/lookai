/**
 * @file ui/menus/settings/settings_screen.c
 * @brief Root settings menu body implementation.
 */

#include "settings_screen.h"

#include "ui_settings_item.h"
#include "ui_theme.h"

static void create_settings_separator(lv_obj_t *body)
{
    lv_obj_t *separator = lv_obj_create(body);

    lv_obj_set_size(separator, UI_THEME_BUTTON_WIDTH - 16, 1);
    lv_obj_set_style_bg_color(separator, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_bg_opa(separator, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(separator, 0, 0);
    lv_obj_set_style_pad_all(separator, 0, 0);
    lv_obj_clear_flag(separator, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(separator, LV_OBJ_FLAG_CLICKABLE);
}

void settings_screen_render(
    lv_obj_t *body,
    const ui_manager_state_t *state,
    const ui_manager_callbacks_t *callbacks,
    lv_event_cb_t wifi_button_cb,
    lv_event_cb_t brightness_button_cb,
    lv_event_cb_t stt_button_cb,
    lv_event_cb_t ai_button_cb,
    lv_event_cb_t tts_button_cb
)
{
    (void)state;
    (void)callbacks;

    const ui_settings_item_icon_t wifi_icon = {
        .type = UI_SETTINGS_ITEM_ICON_WIFI,
        .text = NULL,
        .color = UI_COLOR_WIFI_BLUE,
    };

    const ui_settings_item_icon_t brightness_icon = {
        .type = UI_SETTINGS_ITEM_ICON_SUN,
        .text = NULL,
        .color = UI_COLOR_BRIGHTNESS_ORANGE,
    };

    const ui_settings_item_icon_t stt_icon = {
        .type = UI_SETTINGS_ITEM_ICON_TEXT,
        .text = "STT",
        .color = 0x22C55E,
    };

    const ui_settings_item_icon_t ai_icon = {
        .type = UI_SETTINGS_ITEM_ICON_TEXT,
        .text = "AI",
        .color = 0x38BDF8,
    };

    const ui_settings_item_icon_t tts_icon = {
        .type = UI_SETTINGS_ITEM_ICON_TEXT,
        .text = "TTS",
        .color = 0xA855F7,
    };

    ui_settings_item_create(
        body,
        &wifi_icon,
        "Wi-Fi",
        wifi_button_cb,
        NULL
    );

    create_settings_separator(body);

    ui_settings_item_create(
        body,
        &brightness_icon,
        "Brightness",
        brightness_button_cb,
        NULL
    );

    create_settings_separator(body);

    ui_settings_item_create(
        body,
        &stt_icon,
        "Speech to Text",
        stt_button_cb,
        NULL
    );

    create_settings_separator(body);

    ui_settings_item_create(
        body,
        &ai_icon,
        "AI Assistant",
        ai_button_cb,
        NULL
    );

    create_settings_separator(body);

    ui_settings_item_create(
        body,
        &tts_icon,
        "Text to Speech",
        tts_button_cb,
        NULL
    );
}
