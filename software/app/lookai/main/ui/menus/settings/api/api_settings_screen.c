/**
 * @file ui/menus/settings/api/api_settings_screen.c
 * @brief STT/TTS/AI runtime parameter settings screens implementation.
 */

#include "api_settings_screen.h"

#include <stdint.h>
#include <stdio.h>

#include "app_settings.h"
#include "ui_theme.h"

#define DROPDOWN_ROW_W UI_THEME_CARD_WIDTH
#define DROPDOWN_ROW_H 70
#define DROPDOWN_LABEL_W 104
#define DROPDOWN_W 168

typedef enum {
    API_SETTING_OPTION_STT_LANGUAGE = 1,
    API_SETTING_OPTION_STT_MODEL,
    API_SETTING_OPTION_AI_STYLE,
    API_SETTING_OPTION_AI_TEMPERATURE,
    API_SETTING_OPTION_TTS_VOICE,
    API_SETTING_OPTION_TTS_SPEED,
} api_setting_option_id_t;

static void create_section_label(lv_obj_t *body, const char *text)
{
    lv_obj_t *label = lv_label_create(body);
    lv_label_set_text_static(label, text != NULL ? text : "Parameters");
    lv_obj_set_width(label, UI_THEME_BUTTON_WIDTH - 6);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_DIM), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_pad_top(label, 2, 0);
    lv_obj_set_style_pad_left(label, 4, 0);
}

static lv_obj_t *create_dropdown_row(
    lv_obj_t *body,
    const char *label,
    const char *options,
    int selected_index,
    api_setting_option_id_t option_id,
    lv_event_cb_t option_cb
)
{
    lv_obj_t *row = lv_obj_create(body);
    lv_obj_set_size(row, DROPDOWN_ROW_W, DROPDOWN_ROW_H);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(UI_COLOR_BORDER_SOFT), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_radius(row, UI_THEME_CARD_RADIUS, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name = lv_label_create(row);
    lv_label_set_text(name, label != NULL ? label : "Option");
    lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(name, DROPDOWN_LABEL_W);
    lv_obj_set_style_text_color(name, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 14, 0);

    lv_obj_t *dropdown = lv_dropdown_create(row);
    lv_dropdown_set_options_static(dropdown, options != NULL ? options : "-");
    lv_dropdown_set_selected(dropdown, selected_index >= 0 ? (uint16_t)selected_index : 0);
    lv_obj_set_size(dropdown, DROPDOWN_W, 44);
    lv_obj_align(dropdown, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_radius(dropdown, 18, 0);
    lv_obj_set_style_bg_color(dropdown, lv_color_hex(UI_COLOR_CARD_ALT), 0);
    lv_obj_set_style_bg_color(dropdown, lv_color_hex(UI_COLOR_CARD_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(dropdown, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(dropdown, lv_color_hex(UI_COLOR_PRIMARY), 0);
    lv_obj_set_style_border_width(dropdown, 1, 0);
    lv_obj_set_style_text_color(dropdown, lv_color_hex(UI_COLOR_PRIMARY_SOFT), 0);
    lv_obj_set_style_pad_left(dropdown, 12, 0);
    lv_obj_set_style_pad_right(dropdown, 10, 0);
    lv_obj_set_style_shadow_width(dropdown, 0, 0);

    if (option_cb != NULL) {
        lv_obj_add_event_cb(dropdown, option_cb, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)option_id);
    }

    return dropdown;
}


static void speed_slider_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_VALUE_CHANGED && code != LV_EVENT_RELEASED) {
        return;
    }

    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(event);
    lv_obj_t *value_label = (lv_obj_t *)lv_event_get_user_data(event);
    if (slider == NULL) {
        return;
    }

    int32_t value = lv_slider_get_value(slider);
    int32_t snap_window = code == LV_EVENT_RELEASED ? 8 : 3;
    if (value >= 100 - snap_window && value <= 100 + snap_window) {
        value = 100;
        lv_slider_set_value(slider, value, LV_ANIM_OFF);
    }

    app_settings_set_tts_speed_percent((uint16_t)value);

    if (value_label != NULL) {
        char text[16];
        snprintf(text, sizeof(text), "%ld.%02ldx", (long)(value / 100), (long)(value % 100));
        lv_label_set_text(value_label, text);
    }
}

static lv_obj_t *create_speed_slider_row(lv_obj_t *body)
{
    lv_obj_t *row = lv_obj_create(body);
    lv_obj_set_size(row, DROPDOWN_ROW_W, 92);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(UI_COLOR_BORDER_SOFT), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_radius(row, UI_THEME_CARD_RADIUS, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name = lv_label_create(row);
    lv_label_set_text(name, "Speed");
    lv_obj_set_width(name, 116);
    lv_obj_set_style_text_color(name, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 14, 12);

    lv_obj_t *value_label = lv_label_create(row);
    lv_label_set_text(value_label, app_settings_get_tts_speed_label());
    lv_obj_set_width(value_label, 76);
    lv_obj_set_style_text_color(value_label, lv_color_hex(UI_COLOR_PRIMARY_SOFT), 0);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(value_label, LV_ALIGN_TOP_RIGHT, -14, 12);

    lv_obj_t *slider = lv_slider_create(row);
    lv_obj_set_size(slider, DROPDOWN_ROW_W - 36, 18);
    lv_slider_set_range(slider, 50, 200);
    lv_slider_set_value(slider, app_settings_get_tts_speed_percent(), LV_ANIM_OFF);
    lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_COLOR_CARD_PRESSED), 0);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_COLOR_PRIMARY), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_COLOR_TEXT), LV_PART_KNOB);
    lv_obj_set_style_radius(slider, UI_THEME_PILL_RADIUS, 0);
    lv_obj_set_style_radius(slider, UI_THEME_PILL_RADIUS, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_width(slider, 0, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, speed_slider_event_cb, LV_EVENT_VALUE_CHANGED, value_label);
    lv_obj_add_event_cb(slider, speed_slider_event_cb, LV_EVENT_RELEASED, value_label);

    return slider;
}

void api_settings_screen_render(
    lv_obj_t *body,
    api_settings_kind_t kind,
    const ui_manager_state_t *state,
    lv_event_cb_t option_cb
)
{
    (void)state;

    switch (kind) {
        case API_SETTINGS_KIND_STT:
            create_section_label(body, "Speech to text");
            create_dropdown_row(
                body,
                "Language",
                app_settings_get_stt_language_dropdown_options(),
                app_settings_get_stt_language_index(),
                API_SETTING_OPTION_STT_LANGUAGE,
                option_cb
            );
            create_dropdown_row(
                body,
                "Model",
                app_settings_get_stt_model_dropdown_options(),
                app_settings_get_stt_model_index(),
                API_SETTING_OPTION_STT_MODEL,
                option_cb
            );
            break;

        case API_SETTINGS_KIND_AI:
            create_section_label(body, "Assistant");
            create_dropdown_row(
                body,
                "Style",
                app_settings_get_ai_style_dropdown_options(),
                app_settings_get_ai_style_index(),
                API_SETTING_OPTION_AI_STYLE,
                option_cb
            );
            create_dropdown_row(
                body,
                "Tone",
                app_settings_get_ai_temperature_dropdown_options(),
                app_settings_get_ai_temperature_index(),
                API_SETTING_OPTION_AI_TEMPERATURE,
                option_cb
            );
            break;

        case API_SETTINGS_KIND_TTS:
        default:
            create_section_label(body, "Text to speech");
            create_dropdown_row(
                body,
                "Voice",
                app_settings_get_tts_voice_dropdown_options(),
                app_settings_get_tts_voice_index(),
                API_SETTING_OPTION_TTS_VOICE,
                option_cb
            );
            (void)option_cb;
            create_speed_slider_row(body);
            break;
    }
}
