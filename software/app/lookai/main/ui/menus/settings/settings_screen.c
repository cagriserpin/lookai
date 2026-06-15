/**
 * @file ui/menus/settings/settings_screen.c
 * @brief Root settings menu body implementation.
 */

#include "settings_screen.h"

#include "ui_card.h"
#include "ui_settings_item.h"
#include "ui_theme.h"

#ifndef SETTINGS_HERO_FONT
#define SETTINGS_HERO_FONT (&lv_font_montserrat_18)
#endif

static void create_section_label(lv_obj_t *body, const char *text)
{
    lv_obj_t *label = lv_label_create(body);

    lv_label_set_text_static(label, text != NULL ? text : "");
    lv_obj_set_width(label, UI_THEME_BUTTON_WIDTH - 6);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_DIM), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_pad_top(label, 2, 0);
    lv_obj_set_style_pad_left(label, 4, 0);
}

static void create_hero_card(lv_obj_t *body)
{
    lv_obj_t *hero = ui_card_create(body);

    lv_obj_set_style_bg_color(hero, lv_color_hex(UI_COLOR_CARD_ALT), 0);
    lv_obj_set_style_border_color(hero, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_pad_all(hero, 14, 0);
    lv_obj_set_style_pad_gap(hero, 6, 0);

    lv_obj_t *eyebrow = lv_label_create(hero);
    lv_label_set_text_static(eyebrow, "LOOKAI");
    lv_obj_set_width(eyebrow, UI_THEME_CARD_INNER_WIDTH);
    lv_obj_set_style_text_color(eyebrow, lv_color_hex(UI_COLOR_PRIMARY_SOFT), 0);
    lv_obj_set_style_text_align(eyebrow, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *title = lv_label_create(hero);
    lv_label_set_text_static(title, "Control Center");
    lv_obj_set_width(title, UI_THEME_CARD_INNER_WIDTH);
    lv_obj_set_style_text_font(title, SETTINGS_HERO_FONT, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *subtitle = lv_label_create(hero);
    lv_label_set_text_static(subtitle, "Voice, network and display settings");
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(subtitle, UI_THEME_CARD_INNER_WIDTH);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(UI_COLOR_MUTED), 0);
    lv_obj_set_style_text_align(subtitle, LV_TEXT_ALIGN_CENTER, 0);
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
        .color = UI_COLOR_STT_GREEN,
    };

    const ui_settings_item_icon_t ai_icon = {
        .type = UI_SETTINGS_ITEM_ICON_TEXT,
        .text = "AI",
        .color = UI_COLOR_AI_CYAN,
    };

    const ui_settings_item_icon_t tts_icon = {
        .type = UI_SETTINGS_ITEM_ICON_TEXT,
        .text = "TTS",
        .color = UI_COLOR_TTS_PURPLE,
    };

    create_hero_card(body);
    create_section_label(body, "Device");

    ui_settings_item_create(
        body,
        &wifi_icon,
        "Wi-Fi",
        wifi_button_cb,
        NULL
    );

    ui_settings_item_create(
        body,
        &brightness_icon,
        "Brightness",
        brightness_button_cb,
        NULL
    );

    create_section_label(body, "Voice assistant");

    ui_settings_item_create(
        body,
        &stt_icon,
        "Speech to Text",
        stt_button_cb,
        NULL
    );

    ui_settings_item_create(
        body,
        &ai_icon,
        "AI Assistant",
        ai_button_cb,
        NULL
    );

    ui_settings_item_create(
        body,
        &tts_icon,
        "Text to Speech",
        tts_button_cb,
        NULL
    );
}
