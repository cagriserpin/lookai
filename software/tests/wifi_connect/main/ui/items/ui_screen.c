/**
 * @file ui/items/ui_screen.c
 * @brief Reusable LVGL screen/header/content helper implementation.
 */

#include "ui_screen.h"

#include "ui_button.h"
#include "ui_theme.h"

void ui_screen_prepare(lv_obj_t *screen)
{
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
}

void ui_screen_create_header(
    lv_obj_t *screen,
    const char *title,
    bool can_go_back,
    lv_event_cb_t back_cb
)
{
    lv_obj_t *header = lv_obj_create(screen);

    lv_obj_set_size(header, 430, 72);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    if (can_go_back) {
        lv_obj_t *back = ui_button_create(
            header,
            "< Back",
            100,
            44,
            UI_COLOR_SECONDARY,
            back_cb,
            NULL
        );

        lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
    }

    lv_obj_t *label = lv_label_create(header);
    lv_label_set_text(label, title != NULL ? title : "");
    lv_obj_set_width(label, can_go_back ? 290 : 390);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_align(label, can_go_back ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_RIGHT_MID, 0, 0);
}

lv_obj_t *ui_screen_create_content(lv_obj_t *screen)
{
    lv_obj_t *content = lv_obj_create(screen);

    lv_obj_set_size(content, UI_THEME_CONTENT_WIDTH, 360);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 88);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 10, 0);
    lv_obj_set_style_pad_gap(content, 12, 0);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);

    return content;
}
