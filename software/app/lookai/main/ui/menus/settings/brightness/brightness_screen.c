/**
 * @file ui/menus/settings/brightness_screen.c
 * @brief Brightness settings body implementation.
 */

#include "brightness_screen.h"

#include <stdio.h>

#include "ui_card.h"
#include "ui_label.h"
#include "ui_theme.h"

#define BRIGHTNESS_SLIDER_KNOB_SIZE 38
#define BRIGHTNESS_SLIDER_EXT_CLICK_AREA 50

#define BRIGHTNESS_CARD_HORIZONTAL_PADDING 50
#define BRIGHTNESS_CARD_TOP_PADDING 18
#define BRIGHTNESS_CARD_BOTTOM_PADDING 34
#define BRIGHTNESS_CARD_VERTICAL_PADDING 18
#define BRIGHTNESS_CARD_INNER_WIDTH (UI_THEME_CARD_WIDTH - (BRIGHTNESS_CARD_HORIZONTAL_PADDING * 2))

void brightness_screen_render(
    lv_obj_t *body,
    const ui_manager_state_t *state,
    lv_event_cb_t slider_changed_cb
)
{
    lv_obj_t *card = ui_card_create(body);
    lv_obj_set_style_bg_color(card, lv_color_hex(UI_COLOR_CARD_ALT), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(UI_COLOR_BRIGHTNESS_ORANGE), 0);

    /*
     * Extra padding is applied horizontally only. The large slider knob needs
     * left/right breathing room, but top/bottom should stay compact.
     */
    lv_obj_set_style_pad_left(card, BRIGHTNESS_CARD_HORIZONTAL_PADDING, 0);
    lv_obj_set_style_pad_right(card, BRIGHTNESS_CARD_HORIZONTAL_PADDING, 0);
    lv_obj_set_style_pad_top(card, BRIGHTNESS_CARD_VERTICAL_PADDING, 0);
    lv_obj_set_style_pad_bottom(card, BRIGHTNESS_CARD_BOTTOM_PADDING, 0);
    lv_obj_set_style_pad_gap(card, 18, 0);

    ui_label_create(card, "Brightness", UI_COLOR_TEXT, BRIGHTNESS_CARD_INNER_WIDTH);

    lv_obj_t *value_label = lv_label_create(card);
    char value_text[16];
    snprintf(value_text, sizeof(value_text), "%d%%", state->brightness_percent);
    lv_label_set_text(value_label, value_text);
    lv_obj_set_width(value_label, BRIGHTNESS_CARD_INNER_WIDTH);
    lv_obj_set_style_text_color(value_label, lv_color_hex(UI_COLOR_BRIGHTNESS_ORANGE), 0);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *slider = lv_slider_create(card);
    lv_obj_set_width(slider, BRIGHTNESS_CARD_INNER_WIDTH);
    lv_obj_set_height(slider, 42);
    lv_slider_set_range(slider, 10, 100);
    lv_slider_set_value(slider, state->brightness_percent, LV_ANIM_OFF);

    lv_obj_set_style_radius(slider, 12, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 12, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_COLOR_SECONDARY), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_COLOR_BRIGHTNESS_ORANGE), LV_PART_INDICATOR);

    /*
     * Big white knob for better visibility and easier touch input.
     */
    lv_obj_set_style_width(slider, 38, LV_PART_KNOB);
    lv_obj_set_style_height(slider, 38, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, 19, LV_PART_KNOB);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_COLOR_TEXT), LV_PART_KNOB);
    lv_obj_set_style_border_color(slider, lv_color_hex(UI_COLOR_BRIGHTNESS_ORANGE), LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 2, LV_PART_KNOB);

    /*
     * The visible knob is larger than the slider track. At the min/max edges,
     * part of the knob is outside the slider object's normal hit box. Extending
     * the click area lets the whole visible knob start a drag, especially near
     * 95-100%.
     */
    lv_obj_set_ext_click_area(slider, BRIGHTNESS_SLIDER_EXT_CLICK_AREA);

    if (slider_changed_cb != NULL) {
        lv_obj_add_event_cb(slider, slider_changed_cb, LV_EVENT_VALUE_CHANGED, value_label);
    }
}
