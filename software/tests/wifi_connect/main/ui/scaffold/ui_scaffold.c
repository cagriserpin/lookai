/**
 * @file ui/scaffold/ui_scaffold.c
 * @brief Circular-screen scaffold layout implementation.
 */

#include "ui_scaffold.h"

#include "ui_theme.h"

static void prepare_screen(lv_obj_t *screen)
{
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
}

/**
 * @brief Create a transparent absolute-positioned region.
 */
static lv_obj_t *create_region(lv_obj_t *screen, int width, int height)
{
    lv_obj_t *region = lv_obj_create(screen);

    lv_obj_set_size(region, width, height);
    lv_obj_set_style_bg_opa(region, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(region, 0, 0);
    lv_obj_set_style_pad_all(region, 0, 0);
    lv_obj_clear_flag(region, LV_OBJ_FLAG_SCROLLABLE);

    return region;
}

/**
 * @brief Create the title area in the top slice.
 */
static void create_top_slice(lv_obj_t *screen, const ui_scaffold_config_t *config)
{
    lv_obj_t *top = create_region(
        screen,
        UI_THEME_BODY_WIDTH,
        UI_THEME_SLICE_SIZE
    );

    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *title = lv_label_create(top);
    lv_label_set_text(title, config != NULL && config->title != NULL ? config->title : "");
    lv_obj_set_width(title, UI_THEME_TITLE_WIDTH);
    lv_label_set_long_mode(title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(title);
}

/**
 * @brief Create the optional back icon in the left slice.
 */
static void create_left_slice(lv_obj_t *screen, const ui_scaffold_config_t *config)
{
    lv_obj_t *left = create_region(
        screen,
        UI_THEME_SLICE_SIZE,
        UI_THEME_BODY_HEIGHT
    );

    lv_obj_align(left, LV_ALIGN_LEFT_MID, 0, 0);

    if (config == NULL || !config->show_back) {
        return;
    }

    /*
     * The left slice sits in the vertical middle of the circle, where the full
     * display width is available. This keeps the icon visible and avoids the
     * top-left clipped circular edge.
     */
    lv_obj_t *back_hitbox = lv_obj_create(left);
    lv_obj_set_size(back_hitbox, 56, 56);
    lv_obj_center(back_hitbox);
    lv_obj_set_style_bg_opa(back_hitbox, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(back_hitbox, 0, 0);
    lv_obj_set_style_pad_all(back_hitbox, 0, 0);
    lv_obj_clear_flag(back_hitbox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(back_hitbox, LV_OBJ_FLAG_CLICKABLE);

    if (config->back_cb != NULL) {
        lv_obj_add_event_cb(back_hitbox, config->back_cb, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t *icon = lv_label_create(back_hitbox);
    lv_label_set_text(icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(icon, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_center(icon);
}

/**
 * @brief Create currently-empty right and bottom slices.
 */
static void create_empty_slices(lv_obj_t *screen)
{
    lv_obj_t *right = create_region(
        screen,
        UI_THEME_SLICE_SIZE,
        UI_THEME_BODY_HEIGHT
    );
    lv_obj_align(right, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *bottom = create_region(
        screen,
        UI_THEME_BODY_WIDTH,
        UI_THEME_SLICE_SIZE
    );
    lv_obj_align(bottom, LV_ALIGN_BOTTOM_MID, 0, 0);
}

/**
 * @brief Create the central scrollable body.
 */
static lv_obj_t *create_body(lv_obj_t *screen)
{
    lv_obj_t *body = lv_obj_create(screen);

    lv_obj_set_size(body, UI_THEME_BODY_WIDTH, UI_THEME_BODY_HEIGHT);
    lv_obj_center(body);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 8, 0);
    lv_obj_set_style_pad_gap(body, 12, 0);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        body,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );

    return body;
}

lv_obj_t *ui_scaffold_create(lv_obj_t *screen, const ui_scaffold_config_t *config)
{
    prepare_screen(screen);

    create_top_slice(screen, config);
    create_left_slice(screen, config);
    create_empty_slices(screen);

    return create_body(screen);
}
