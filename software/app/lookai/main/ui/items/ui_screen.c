/**
 * @file ui/items/ui_screen.c
 * @brief Reusable LVGL screen/header/content helper implementation.
 */

#include "ui_screen.h"

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
    /*
     * Scaffold-style transparent AppBar.
     *
     * On a circular screen, the top safe area is much narrower than the middle
     * of the display. Keep the AppBar narrow and centered so the back icon does
     * not disappear into the clipped circular edge.
     */
    lv_obj_t *header = lv_obj_create(screen);

    lv_obj_set_size(header, UI_THEME_APPBAR_WIDTH, UI_THEME_APPBAR_HEIGHT);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, UI_THEME_APPBAR_Y);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    if (can_go_back) {
        /*
         * Invisible touch target with only the icon visible.
         *
         * The hitbox is inside the circular safe area, but the title is not
         * shifted by this object. This keeps the AppBar title visually centered
         * like a Flutter AppBar title.
         */
        lv_obj_t *back_hitbox = lv_obj_create(header);
        lv_obj_set_size(back_hitbox, 44, 44);
        lv_obj_align(back_hitbox, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_opa(back_hitbox, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(back_hitbox, 0, 0);
        lv_obj_set_style_pad_all(back_hitbox, 0, 0);
        lv_obj_clear_flag(back_hitbox, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(back_hitbox, LV_OBJ_FLAG_CLICKABLE);

        if (back_cb != NULL) {
            lv_obj_add_event_cb(back_hitbox, back_cb, LV_EVENT_CLICKED, NULL);
        }

        lv_obj_t *back_icon = lv_label_create(back_hitbox);
        lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_color(back_icon, lv_color_hex(UI_COLOR_TEXT), 0);
        lv_obj_center(back_icon);
    }

    lv_obj_t *label = lv_label_create(header);
    lv_label_set_text(label, title != NULL ? title : "");

    /*
     * Important:
     * The title is always centered in the AppBar, independent from the back
     * icon. When the back icon exists, the title area is narrower so it does
     * not visually collide with the icon, but it is still aligned to the exact
     * center of the header/display.
     */
    lv_obj_set_width(label, can_go_back ? 190 : 260);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
}

lv_obj_t *ui_screen_create_content(lv_obj_t *screen)
{
    /*
     * Circular-safe body area. Children are horizontally centered so cards and
     * full-width buttons look balanced on the round panel.
     */
    lv_obj_t *content = lv_obj_create(screen);

    lv_obj_set_size(content, UI_THEME_CONTENT_WIDTH, UI_THEME_CONTENT_HEIGHT);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, UI_THEME_CONTENT_Y);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 8, 0);
    lv_obj_set_style_pad_gap(content, 12, 0);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        content,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );

    return content;
}
