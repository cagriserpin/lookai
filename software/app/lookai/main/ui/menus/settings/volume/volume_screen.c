/**
 * @file ui/menus/settings/volume_screen.c
 * @brief Volume settings body implementation.
 */

#include "volume_screen.h"

#include <stdio.h>

#include "ui_card.h"
#include "ui_label.h"
#include "ui_theme.h"

#define VOLUME_SLIDER_KNOB_SIZE 38
#define VOLUME_SLIDER_EXT_CLICK_AREA 50

#define VOLUME_CARD_HORIZONTAL_PADDING 50
#define VOLUME_CARD_BOTTOM_PADDING 34
#define VOLUME_CARD_VERTICAL_PADDING 18
#define VOLUME_CARD_INNER_WIDTH (UI_THEME_CARD_WIDTH - (VOLUME_CARD_HORIZONTAL_PADDING * 2))

static void create_volume_slider(
    lv_obj_t *card,
    int value,
    lv_event_cb_t slider_changed_cb
)
{
    ui_label_create(card, "Volume", UI_COLOR_TEXT, VOLUME_CARD_INNER_WIDTH);

    lv_obj_t *value_label = lv_label_create(card);
    char value_text[16];
    snprintf(value_text, sizeof(value_text), "%d%%", value);
    lv_label_set_text(value_label, value_text);
    lv_obj_set_width(value_label, VOLUME_CARD_INNER_WIDTH);
    lv_obj_set_style_text_color(value_label, lv_color_hex(UI_COLOR_AI_CYAN), 0);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *slider = lv_slider_create(card);
    lv_obj_set_width(slider, VOLUME_CARD_INNER_WIDTH);
    lv_obj_set_height(slider, 42);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);

    lv_obj_set_style_radius(slider, 12, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 12, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_COLOR_SECONDARY), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_COLOR_AI_CYAN), LV_PART_INDICATOR);

    lv_obj_set_style_width(slider, VOLUME_SLIDER_KNOB_SIZE, LV_PART_KNOB);
    lv_obj_set_style_height(slider, VOLUME_SLIDER_KNOB_SIZE, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, VOLUME_SLIDER_KNOB_SIZE / 2, LV_PART_KNOB);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_COLOR_TEXT), LV_PART_KNOB);
    lv_obj_set_style_border_color(slider, lv_color_hex(UI_COLOR_AI_CYAN), LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 2, LV_PART_KNOB);
    lv_obj_set_ext_click_area(slider, VOLUME_SLIDER_EXT_CLICK_AREA);

    if (slider_changed_cb != NULL) {
        lv_obj_add_event_cb(slider, slider_changed_cb, LV_EVENT_VALUE_CHANGED, value_label);
    }
}

void volume_screen_render(
    lv_obj_t *body,
    const ui_manager_state_t *state,
    lv_event_cb_t volume_slider_changed_cb
)
{
    if (body == NULL || state == NULL) {
        return;
    }

    lv_obj_t *card = ui_card_create(body);
    lv_obj_set_style_bg_color(card, lv_color_hex(UI_COLOR_CARD_ALT), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(UI_COLOR_AI_CYAN), 0);

    lv_obj_set_style_pad_left(card, VOLUME_CARD_HORIZONTAL_PADDING, 0);
    lv_obj_set_style_pad_right(card, VOLUME_CARD_HORIZONTAL_PADDING, 0);
    lv_obj_set_style_pad_top(card, VOLUME_CARD_VERTICAL_PADDING, 0);
    lv_obj_set_style_pad_bottom(card, VOLUME_CARD_BOTTOM_PADDING, 0);
    lv_obj_set_style_pad_gap(card, 16, 0);

    create_volume_slider(card, state->volume_percent, volume_slider_changed_cb);
}
