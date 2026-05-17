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
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);

    return label;
}
