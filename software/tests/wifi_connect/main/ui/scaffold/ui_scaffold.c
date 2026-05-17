/**
 * @file ui/scaffold/ui_scaffold.c
 * @brief Circular-screen scaffold layout implementation.
 */

#include "ui_scaffold.h"

#include <stdlib.h>

#include "ui_theme.h"

/*
 * LVGL fonts are compile-time font objects, not runtime-scaled sizes.
 * Enable this font in SDK Configuration Editor:
 * Component config -> LVGL -> Font usage -> Montserrat 18
 */
#ifndef UI_TITLE_FONT
#define UI_TITLE_FONT (&lv_font_montserrat_18)
#endif

#define UI_TITLE_SCROLL_EDGE_PADDING 20
#define UI_TITLE_SCROLL_START_WAIT_MS 900
#define UI_TITLE_SCROLL_END_WAIT_MS 1300
#define UI_TITLE_SCROLL_PX_PER_SECOND 26

typedef enum {
    TITLE_SCROLL_TO_END,
    TITLE_SCROLL_TO_START,
} title_scroll_direction_t;

typedef struct {
    lv_obj_t *label;
    lv_timer_t *timer;
    int32_t start_x;
    int32_t end_x;
    int32_t scroll_time_ms;
    title_scroll_direction_t direction;
} title_scroll_ctx_t;

static void title_scroll_anim_cb(void *object, int32_t value)
{
    lv_obj_set_x((lv_obj_t *)object, value);
}

static void prepare_screen(lv_obj_t *screen)
{
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
}

static void title_scroll_timer_cb(lv_timer_t *timer);
static void title_scroll_anim_ready_cb(lv_anim_t *anim);

static void title_scroll_start_timer(title_scroll_ctx_t *ctx, uint32_t delay_ms)
{
    if (ctx == NULL || ctx->label == NULL) {
        return;
    }

    if (ctx->timer != NULL) {
        lv_timer_del(ctx->timer);
        ctx->timer = NULL;
    }

    ctx->timer = lv_timer_create(title_scroll_timer_cb, delay_ms, ctx);
    lv_timer_set_repeat_count(ctx->timer, 1);
}

static void title_scroll_start_anim(title_scroll_ctx_t *ctx)
{
    if (ctx == NULL || ctx->label == NULL) {
        return;
    }

    int32_t from = ctx->direction == TITLE_SCROLL_TO_END ? ctx->start_x : ctx->end_x;
    int32_t to = ctx->direction == TITLE_SCROLL_TO_END ? ctx->end_x : ctx->start_x;

    lv_obj_set_x(ctx->label, from);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, ctx->label);
    lv_anim_set_exec_cb(&anim, title_scroll_anim_cb);
    lv_anim_set_values(&anim, from, to);
    lv_anim_set_time(&anim, ctx->scroll_time_ms);
    lv_anim_set_path_cb(&anim, lv_anim_path_linear);
    lv_anim_set_ready_cb(&anim, title_scroll_anim_ready_cb);
    lv_anim_set_user_data(&anim, ctx);
    lv_anim_set_early_apply(&anim, true);
    lv_anim_start(&anim);
}

static void title_scroll_timer_cb(lv_timer_t *timer)
{
    title_scroll_ctx_t *ctx = (title_scroll_ctx_t *)lv_timer_get_user_data(timer);
    if (ctx == NULL) {
        return;
    }

    ctx->timer = NULL;
    title_scroll_start_anim(ctx);
}

static void title_scroll_anim_ready_cb(lv_anim_t *anim)
{
    title_scroll_ctx_t *ctx = (title_scroll_ctx_t *)lv_anim_get_user_data(anim);
    if (ctx == NULL || ctx->label == NULL) {
        return;
    }

    if (ctx->direction == TITLE_SCROLL_TO_END) {
        ctx->direction = TITLE_SCROLL_TO_START;
        title_scroll_start_timer(ctx, UI_TITLE_SCROLL_END_WAIT_MS);
    } else {
        ctx->direction = TITLE_SCROLL_TO_END;
        title_scroll_start_timer(ctx, UI_TITLE_SCROLL_START_WAIT_MS);
    }
}

static void title_scroll_delete_cb(lv_event_t *event)
{
    title_scroll_ctx_t *ctx = (title_scroll_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL) {
        return;
    }

    if (ctx->timer != NULL) {
        lv_timer_del(ctx->timer);
        ctx->timer = NULL;
    }

    if (ctx->label != NULL) {
        lv_anim_del(ctx->label, title_scroll_anim_cb);
        ctx->label = NULL;
    }

    free(ctx);
}

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

static void create_scrolling_title(lv_obj_t *top, const char *title_text)
{
    lv_obj_t *viewport = lv_obj_create(top);

    lv_obj_set_size(viewport, UI_THEME_TITLE_WIDTH, UI_THEME_SLICE_SIZE);
    lv_obj_center(viewport);
    lv_obj_set_style_bg_opa(viewport, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(viewport, 0, 0);
    lv_obj_set_style_pad_all(viewport, 0, 0);
    lv_obj_clear_flag(viewport, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(viewport);
    lv_label_set_text(title, title_text != NULL ? title_text : "");
    lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(title, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(title, UI_TITLE_FONT, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, UI_TITLE_SCROLL_EDGE_PADDING, 0);

    lv_obj_update_layout(title);

    int32_t title_width = lv_obj_get_width(title);
    int32_t visible_width = UI_THEME_TITLE_WIDTH - (UI_TITLE_SCROLL_EDGE_PADDING * 2);

    if (title_width <= visible_width) {
        lv_obj_center(title);
        return;
    }

    title_scroll_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return;
    }

    ctx->label = title;
    ctx->start_x = UI_TITLE_SCROLL_EDGE_PADDING;
    ctx->end_x = UI_THEME_TITLE_WIDTH - title_width - UI_TITLE_SCROLL_EDGE_PADDING;
    ctx->direction = TITLE_SCROLL_TO_END;

    int32_t distance = ctx->start_x - ctx->end_x;
    if (distance < 0) {
        distance = -distance;
    }

    ctx->scroll_time_ms = (distance * 1000) / UI_TITLE_SCROLL_PX_PER_SECOND;
    if (ctx->scroll_time_ms < 1800) {
        ctx->scroll_time_ms = 1800;
    }

    lv_obj_add_event_cb(viewport, title_scroll_delete_cb, LV_EVENT_DELETE, ctx);

    /*
     * Delay first movement so immediate post-navigation state updates do not
     * make the animation appear to skip or restart.
     */
    title_scroll_start_timer(ctx, UI_TITLE_SCROLL_START_WAIT_MS);
}

static void create_top_slice(lv_obj_t *screen, const ui_scaffold_config_t *config)
{
    lv_obj_t *top = create_region(screen, UI_THEME_BODY_WIDTH, UI_THEME_SLICE_SIZE);

    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);

    create_scrolling_title(
        top,
        config != NULL && config->title != NULL ? config->title : ""
    );
}

static void create_left_slice(lv_obj_t *screen, const ui_scaffold_config_t *config)
{
    lv_obj_t *left = create_region(screen, UI_THEME_SLICE_SIZE, UI_THEME_BODY_HEIGHT);

    lv_obj_align(left, LV_ALIGN_LEFT_MID, 0, 0);

    if (config == NULL || !config->show_back) {
        return;
    }

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

static void create_empty_slices(lv_obj_t *screen)
{
    lv_obj_t *right = create_region(screen, UI_THEME_SLICE_SIZE, UI_THEME_BODY_HEIGHT);
    lv_obj_align(right, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *bottom = create_region(screen, UI_THEME_BODY_WIDTH, UI_THEME_SLICE_SIZE);
    lv_obj_align(bottom, LV_ALIGN_BOTTOM_MID, 0, 0);
}

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
