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
    lv_obj_set_style_radius(card, UI_THEME_CARD_RADIUS, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(UI_COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(UI_COLOR_BORDER_SOFT), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_style_pad_gap(card, 10, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    /*
     * Cards should never show their own scrollbars. Text should wrap or scroll
     * inside individual child labels instead.
     */
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);

    return card;
}
