/**
 * @file ui/items/ui_card.c
 * @brief Reusable LVGL card container implementation.
 */

#include "ui_card.h"

#include "ui_theme.h"

lv_obj_t *ui_card_create(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);

    lv_obj_set_width(card, UI_THEME_CARD_WIDTH);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(card, 20, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(UI_COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_style_pad_gap(card, 10, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    return card;
}
