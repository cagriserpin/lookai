/**
 * @file ui/items/ui_button.c
 * @brief Reusable LVGL button helper implementation.
 */

#include "ui_button.h"

lv_obj_t *ui_button_create(
    lv_obj_t *parent,
    const char *text,
    int width,
    int height,
    uint32_t color,
    lv_event_cb_t cb,
    void *user_data
)
{
    lv_obj_t *btn = lv_button_create(parent);

    lv_obj_set_size(btn, width, height);
    lv_obj_set_style_radius(btn, 18, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);

    if (cb != NULL) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text != NULL ? text : "");
    lv_obj_center(label);

    return btn;
}
