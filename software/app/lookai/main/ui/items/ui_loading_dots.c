/**
 * @file ui/items/ui_loading_dots.c
 * @brief Small reusable three-dot loading indicator for lightweight UI states.
 */

#include "ui_loading_dots.h"

#include <stdlib.h>

#define UI_LOADING_DOT_SIZE 5
#define UI_LOADING_DOT_GAP 5
#define UI_LOADING_DOT_STEP_MS 120

typedef struct {
    lv_obj_t *container;
    lv_obj_t *dots[3];
    lv_timer_t *timer;
    uint8_t tick;
} ui_loading_dots_ctx_t;

static void loading_dots_timer_cb(lv_timer_t *timer)
{
    ui_loading_dots_ctx_t *ctx = (ui_loading_dots_ctx_t *)lv_timer_get_user_data(timer);
    if (ctx == NULL || ctx->container == NULL || lv_obj_has_flag(ctx->container, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    static const int8_t y_offsets[] = {0, -2, -5, -2, 0, 0};
    static const lv_opa_t opas[] = {LV_OPA_50, LV_OPA_70, LV_OPA_COVER, LV_OPA_70, LV_OPA_50, LV_OPA_50};

    for (uint8_t i = 0; i < 3; i++) {
        if (ctx->dots[i] == NULL) {
            continue;
        }

        uint8_t phase = (uint8_t)((ctx->tick + (i * 2)) % 6);
        lv_obj_set_style_translate_y(ctx->dots[i], y_offsets[phase], 0);
        lv_obj_set_style_bg_opa(ctx->dots[i], opas[phase], 0);
    }

    ctx->tick = (uint8_t)((ctx->tick + 1) % 6);
}

static void loading_dots_delete_cb(lv_event_t *event)
{
    ui_loading_dots_ctx_t *ctx = (ui_loading_dots_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL) {
        return;
    }

    if (ctx->timer != NULL) {
        lv_timer_del(ctx->timer);
        ctx->timer = NULL;
    }

    free(ctx);
}

lv_obj_t *ui_loading_dots_create(lv_obj_t *parent, uint32_t color)
{
    ui_loading_dots_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return NULL;
    }

    lv_obj_t *container = lv_obj_create(parent);
    if (container == NULL) {
        free(ctx);
        return NULL;
    }

    ctx->container = container;

    lv_obj_set_size(container, (UI_LOADING_DOT_SIZE * 3) + (UI_LOADING_DOT_GAP * 2), 14);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_style_pad_gap(container, UI_LOADING_DOT_GAP, 0);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (uint8_t i = 0; i < 3; i++) {
        lv_obj_t *dot = lv_obj_create(container);
        ctx->dots[i] = dot;
        lv_obj_set_size(dot, UI_LOADING_DOT_SIZE, UI_LOADING_DOT_SIZE);
        lv_obj_set_style_radius(dot, UI_LOADING_DOT_SIZE / 2, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_50, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    }

    ctx->timer = lv_timer_create(loading_dots_timer_cb, UI_LOADING_DOT_STEP_MS, ctx);
    lv_obj_add_event_cb(container, loading_dots_delete_cb, LV_EVENT_DELETE, ctx);

    return container;
}

void ui_loading_dots_set_active(lv_obj_t *dots, bool active)
{
    if (dots == NULL) {
        return;
    }

    if (active) {
        lv_obj_clear_flag(dots, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(dots, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_loading_dots_set_color(lv_obj_t *dots, uint32_t color)
{
    if (dots == NULL) {
        return;
    }

    uint32_t count = lv_obj_get_child_count(dots);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t *dot = lv_obj_get_child(dots, i);
        if (dot != NULL) {
            lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
        }
    }
}
