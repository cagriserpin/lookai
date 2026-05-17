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
    lv_obj_set_scrollbar_mode(btn, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    if (cb != NULL) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text != NULL ? text : "");

    /*
     * Button labels are constrained to the button width. If text is longer
     * than the available area, LVGL scrolls to the end, waits, then scrolls
     * back to the beginning.
     */
    lv_obj_set_width(label, width > 24 ? width - 24 : width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);

    return btn;
}
