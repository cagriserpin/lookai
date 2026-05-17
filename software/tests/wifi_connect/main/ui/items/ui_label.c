/**
 * @file ui/items/ui_label.c
 * @brief Reusable LVGL label helper implementation.
 */

#include "ui_label.h"

lv_obj_t *ui_label_create(lv_obj_t *parent, const char *text, uint32_t color, int width)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, text != NULL ? text : "");
    lv_obj_set_width(label, width);
    lv_obj_set_height(label, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);

    /*
     * Card labels should never cause horizontal scrolling. Long text wraps to
     * multiple lines so all content remains inside the card.
     */
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_SCROLLABLE);

    return label;
}
