/**
 * @file ui/scaffold/ui_scaffold.c
 * @brief Circular-screen scaffold layout implementation.
 */

#include "ui_scaffold.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "ui_icons.h"
#include "ui_theme.h"

/*
 * LVGL fonts are compile-time font objects, not runtime-scaled sizes.
 * Enable this font in SDK Configuration Editor:
 * Component config -> LVGL -> Font usage -> Montserrat 18
 */
#ifndef UI_TITLE_FONT
#define UI_TITLE_FONT (&lv_font_montserrat_18)
#endif

#ifndef UI_TIME_FONT
#define UI_TIME_FONT (&lv_font_montserrat_14)
#endif

#ifndef UI_BACK_ICON_FONT
#define UI_BACK_ICON_FONT (&lv_font_montserrat_18)
#endif

#ifndef UI_TITLE_ICON_FONT
#define UI_TITLE_ICON_FONT (&lv_font_montserrat_18)
#endif

#ifndef UI_TITLE_TEXT_ICON_FONT
#define UI_TITLE_TEXT_ICON_FONT (&lv_font_montserrat_14)
#endif

#ifndef UI_TITLE_CUSTOM_ICON_FONT
#define UI_TITLE_CUSTOM_ICON_FONT (&lookai_symbols)
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

typedef enum {
    WIFI_SCROLL_TO_END,
    WIFI_SCROLL_TO_START,
} wifi_scroll_direction_t;

typedef struct {
    lv_obj_t *label;
    lv_timer_t *timer;
    int32_t start_x;
    int32_t end_x;
    int32_t scroll_time_ms;
    wifi_scroll_direction_t direction;
} wifi_scroll_ctx_t;

typedef struct {
    lv_obj_t *container;
    lv_obj_t *dots[3];
    lv_timer_t *timer;
    uint8_t tick;
} loading_dots_ctx_t;

static lv_obj_t *s_title_label = NULL;
static bool s_title_is_time = false;
static lv_obj_t *s_time_label = NULL;
static lv_obj_t *s_time_loading_dots = NULL;
static lv_obj_t *s_wifi_label = NULL;
static lv_obj_t *s_wifi_icon_label = NULL;
static lv_obj_t *s_wifi_loading_dots = NULL;
static wifi_scroll_ctx_t *s_wifi_scroll_ctx = NULL;
static bool s_wifi_icon_loading = false;

#define UI_LOADING_DOT_SIZE 4
#define UI_LOADING_DOT_GAP 4
#define UI_LOADING_DOT_STEP_MS 120

static bool status_text_is_loading(const char *text)
{
    return text != NULL && (
        strcmp(text, "Starting") == 0 ||
        strcmp(text, "Scanning") == 0 ||
        strcmp(text, "Connecting") == 0 ||
        strcmp(text, "Syncing") == 0
    );
}

static bool time_text_is_loading(const char *text)
{
    return text == NULL || text[0] == '\0' || strcmp(text, "--:--") == 0;
}

static void loading_dots_timer_cb(lv_timer_t *timer)
{
    loading_dots_ctx_t *ctx = (loading_dots_ctx_t *)lv_timer_get_user_data(timer);
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
    loading_dots_ctx_t *ctx = (loading_dots_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL) {
        return;
    }

    if (ctx->timer != NULL) {
        lv_timer_del(ctx->timer);
        ctx->timer = NULL;
    }

    if (s_time_loading_dots == ctx->container) {
        s_time_loading_dots = NULL;
    }
    if (s_wifi_loading_dots == ctx->container) {
        s_wifi_loading_dots = NULL;
    }

    free(ctx);
}

static lv_obj_t *create_loading_dots(lv_obj_t *parent, uint32_t color)
{
    loading_dots_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return NULL;
    }

    lv_obj_t *container = lv_obj_create(parent);
    if (container == NULL) {
        free(ctx);
        return NULL;
    }

    ctx->container = container;

    lv_obj_set_size(container, (UI_LOADING_DOT_SIZE * 3) + (UI_LOADING_DOT_GAP * 2), 12);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_style_pad_gap(container, UI_LOADING_DOT_GAP, 0);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
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
    }

    ctx->timer = lv_timer_create(loading_dots_timer_cb, UI_LOADING_DOT_STEP_MS, ctx);
    lv_obj_add_event_cb(container, loading_dots_delete_cb, LV_EVENT_DELETE, ctx);

    return container;
}

#define UI_WIFI_BOX_W 158
#define UI_WIFI_ICON_W 22
#define UI_WIFI_TEXT_W 128
#define UI_WIFI_SCROLL_EDGE_PADDING 0
#define UI_WIFI_SCROLL_WAIT_MS 1500
#define UI_WIFI_SCROLL_PX_PER_SECOND 22

static void title_scroll_anim_cb(void *object, int32_t value)
{
    lv_obj_set_x((lv_obj_t *)object, value);
}

static void prepare_screen(lv_obj_t *screen)
{
    s_title_label = NULL;
    s_title_is_time = false;
    s_time_label = NULL;
    s_time_loading_dots = NULL;
    s_wifi_label = NULL;
    s_wifi_icon_label = NULL;
    s_wifi_loading_dots = NULL;
    s_wifi_scroll_ctx = NULL;
    s_wifi_icon_loading = false;
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

static void wifi_scroll_anim_cb(void *object, int32_t value)
{
    lv_obj_set_x((lv_obj_t *)object, value);
}

static void wifi_scroll_timer_cb(lv_timer_t *timer);
static void wifi_scroll_anim_ready_cb(lv_anim_t *anim);

static void wifi_scroll_stop_anim(wifi_scroll_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    if (ctx->timer != NULL) {
        lv_timer_del(ctx->timer);
        ctx->timer = NULL;
    }

    if (ctx->label != NULL) {
        lv_anim_del(ctx->label, wifi_scroll_anim_cb);
    }
}

static void wifi_scroll_start_timer(wifi_scroll_ctx_t *ctx, uint32_t delay_ms)
{
    if (ctx == NULL || ctx->label == NULL) {
        return;
    }

    if (ctx->timer != NULL) {
        lv_timer_del(ctx->timer);
        ctx->timer = NULL;
    }

    ctx->timer = lv_timer_create(wifi_scroll_timer_cb, delay_ms, ctx);
    lv_timer_set_repeat_count(ctx->timer, 1);
}

static void wifi_scroll_start_anim(wifi_scroll_ctx_t *ctx)
{
    if (ctx == NULL || ctx->label == NULL || ctx->start_x == ctx->end_x) {
        return;
    }

    int32_t from = ctx->direction == WIFI_SCROLL_TO_END ? ctx->start_x : ctx->end_x;
    int32_t to = ctx->direction == WIFI_SCROLL_TO_END ? ctx->end_x : ctx->start_x;

    lv_obj_set_x(ctx->label, from);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, ctx->label);
    lv_anim_set_exec_cb(&anim, wifi_scroll_anim_cb);
    lv_anim_set_values(&anim, from, to);
    lv_anim_set_time(&anim, ctx->scroll_time_ms);
    lv_anim_set_path_cb(&anim, lv_anim_path_linear);
    lv_anim_set_ready_cb(&anim, wifi_scroll_anim_ready_cb);
    lv_anim_set_user_data(&anim, ctx);
    lv_anim_set_early_apply(&anim, true);
    lv_anim_start(&anim);
}

static void wifi_scroll_timer_cb(lv_timer_t *timer)
{
    wifi_scroll_ctx_t *ctx = (wifi_scroll_ctx_t *)lv_timer_get_user_data(timer);
    if (ctx == NULL) {
        return;
    }

    ctx->timer = NULL;
    wifi_scroll_start_anim(ctx);
}

static void wifi_scroll_anim_ready_cb(lv_anim_t *anim)
{
    wifi_scroll_ctx_t *ctx = (wifi_scroll_ctx_t *)lv_anim_get_user_data(anim);
    if (ctx == NULL || ctx->label == NULL) {
        return;
    }

    if (ctx->direction == WIFI_SCROLL_TO_END) {
        ctx->direction = WIFI_SCROLL_TO_START;
    } else {
        ctx->direction = WIFI_SCROLL_TO_END;
    }

    wifi_scroll_start_timer(ctx, UI_WIFI_SCROLL_WAIT_MS);
}

static void wifi_scroll_delete_cb(lv_event_t *event)
{
    wifi_scroll_ctx_t *ctx = (wifi_scroll_ctx_t *)lv_event_get_user_data(event);
    if (ctx == NULL) {
        return;
    }

    wifi_scroll_stop_anim(ctx);
    if (s_wifi_scroll_ctx == ctx) {
        s_wifi_scroll_ctx = NULL;
    }
    free(ctx);
}

static void wifi_scroll_configure(lv_obj_t *viewport, lv_obj_t *label)
{
    if (viewport == NULL || label == NULL) {
        return;
    }

    lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_update_layout(label);
    int32_t label_w = lv_obj_get_width(label);
    int32_t viewport_w = lv_obj_get_width(viewport);

    if (s_wifi_scroll_ctx == NULL) {
        s_wifi_scroll_ctx = calloc(1, sizeof(*s_wifi_scroll_ctx));
        if (s_wifi_scroll_ctx == NULL) {
            lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
            return;
        }
        lv_obj_add_event_cb(viewport, wifi_scroll_delete_cb, LV_EVENT_DELETE, s_wifi_scroll_ctx);
    }

    wifi_scroll_ctx_t *ctx = s_wifi_scroll_ctx;
    wifi_scroll_stop_anim(ctx);
    ctx->label = label;

    if (label_w <= viewport_w) {
        ctx->start_x = 0;
        ctx->end_x = 0;
        ctx->direction = WIFI_SCROLL_TO_END;
        lv_obj_set_x(label, 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
        return;
    }

    /* If the SSID/status is wider than the viewport, start right-aligned next
     * to the Wi-Fi icon, wait, pan to the left edge, wait, then pan back. The
     * parent viewport clips the label so it never leaks into the icon area.
     */
    ctx->start_x = viewport_w - label_w - UI_WIFI_SCROLL_EDGE_PADDING;
    ctx->end_x = UI_WIFI_SCROLL_EDGE_PADDING;
    ctx->direction = WIFI_SCROLL_TO_END;

    int32_t distance = ctx->start_x - ctx->end_x;
    if (distance < 0) {
        distance = -distance;
    }

    ctx->scroll_time_ms = (distance * 1000) / UI_WIFI_SCROLL_PX_PER_SECOND;
    if (ctx->scroll_time_ms < 1800) {
        ctx->scroll_time_ms = 1800;
    }

    lv_obj_set_x(label, ctx->start_x);
    wifi_scroll_start_timer(ctx, UI_WIFI_SCROLL_WAIT_MS);
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

static void create_title_icon(lv_obj_t *top, ui_scaffold_title_icon_t title_icon)
{
    if (title_icon == UI_SCAFFOLD_TITLE_ICON_NONE) {
        return;
    }

    const char *icon_text = "";
    const lv_font_t *icon_font = UI_TITLE_ICON_FONT;

    switch (title_icon) {
        case UI_SCAFFOLD_TITLE_ICON_HOME:
            return;

        case UI_SCAFFOLD_TITLE_ICON_SETTINGS:
            icon_text = LV_SYMBOL_SETTINGS;
            break;

        case UI_SCAFFOLD_TITLE_ICON_WIFI:
            icon_text = LV_SYMBOL_WIFI;
            break;

        case UI_SCAFFOLD_TITLE_ICON_BRIGHTNESS:
            icon_text = UI_SYMBOL_SUN;
            icon_font = UI_TITLE_CUSTOM_ICON_FONT;
            break;

        case UI_SCAFFOLD_TITLE_ICON_VOLUME:
            icon_text = "VOL";
            icon_font = UI_TITLE_TEXT_ICON_FONT;
            break;

        case UI_SCAFFOLD_TITLE_ICON_STT:
            icon_text = "STT";
            icon_font = UI_TITLE_TEXT_ICON_FONT;
            break;

        case UI_SCAFFOLD_TITLE_ICON_AI:
            icon_text = "AI";
            icon_font = UI_TITLE_TEXT_ICON_FONT;
            break;

        case UI_SCAFFOLD_TITLE_ICON_TTS:
            icon_text = "TTS";
            icon_font = UI_TITLE_TEXT_ICON_FONT;
            break;

        case UI_SCAFFOLD_TITLE_ICON_NONE:
        default:
            return;
    }

    lv_obj_t *icon = lv_label_create(top);
    lv_label_set_text(icon, icon_text);
    lv_obj_set_style_text_font(icon, icon_font, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(UI_COLOR_PRIMARY_SOFT), 0);

    /*
     * Icon lives at the very top of the top slice. The title text keeps its
     * existing center position.
     */
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 2);
}

static void create_scrolling_title(lv_obj_t *top, const char *title_text)
{
    lv_obj_t *viewport = lv_obj_create(top);

    lv_obj_set_size(viewport, UI_THEME_TITLE_WIDTH, 30);
    lv_obj_align(viewport, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_style_bg_opa(viewport, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(viewport, 0, 0);
    lv_obj_set_style_pad_all(viewport, 0, 0);
    lv_obj_clear_flag(viewport, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(viewport, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    lv_obj_t *title = lv_label_create(viewport);
    s_title_label = title;
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

    title_scroll_start_timer(ctx, UI_TITLE_SCROLL_START_WAIT_MS);
}




static bool wifi_status_is_loading_text(const char *wifi_text);

static void wifi_icon_opa_anim_cb(void *object, int32_t value)
{
    lv_obj_set_style_text_opa((lv_obj_t *)object, (lv_opa_t)value, 0);
}

static void wifi_icon_y_anim_cb(void *object, int32_t value)
{
    lv_obj_set_style_translate_y((lv_obj_t *)object, value, 0);
}

static uint32_t wifi_icon_color(bool wifi_connected, const char *wifi_text)
{
    if (wifi_status_is_loading_text(wifi_text)) {
        return UI_COLOR_PRIMARY_SOFT;
    }

    if (wifi_connected) {
        return UI_COLOR_SUCCESS_TEXT;
    }

    if (wifi_text != NULL && strcmp(wifi_text, "Portal") == 0) {
        return UI_COLOR_WARNING;
    }

    return UI_COLOR_DIM;
}

static void wifi_icon_apply_state(bool wifi_connected, const char *wifi_text)
{
    if (s_wifi_icon_label == NULL) {
        return;
    }

    bool loading = wifi_status_is_loading_text(wifi_text);
    lv_label_set_text(s_wifi_icon_label, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(s_wifi_icon_label, lv_color_hex(wifi_icon_color(wifi_connected, wifi_text)), 0);

    if (!loading) {
        if (s_wifi_icon_loading) {
            lv_anim_del(s_wifi_icon_label, wifi_icon_opa_anim_cb);
            lv_anim_del(s_wifi_icon_label, wifi_icon_y_anim_cb);
            s_wifi_icon_loading = false;
        }
        lv_obj_set_style_text_opa(s_wifi_icon_label, LV_OPA_COVER, 0);
        lv_obj_set_style_translate_y(s_wifi_icon_label, 0, 0);
        return;
    }

    if (s_wifi_icon_loading) {
        return;
    }

    s_wifi_icon_loading = true;
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_wifi_icon_label);
    lv_anim_set_exec_cb(&anim, wifi_icon_opa_anim_cb);
    lv_anim_set_values(&anim, LV_OPA_50, LV_OPA_COVER);
    lv_anim_set_time(&anim, 520);
    lv_anim_set_playback_time(&anim, 520);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&anim, lv_anim_path_linear);
    lv_anim_start(&anim);

    lv_anim_t y_anim;
    lv_anim_init(&y_anim);
    lv_anim_set_var(&y_anim, s_wifi_icon_label);
    lv_anim_set_exec_cb(&y_anim, wifi_icon_y_anim_cb);
    lv_anim_set_values(&y_anim, 0, -4);
    lv_anim_set_time(&y_anim, 380);
    lv_anim_set_playback_time(&y_anim, 380);
    lv_anim_set_repeat_count(&y_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&y_anim, lv_anim_path_ease_in_out);
    lv_anim_start(&y_anim);
}

static bool wifi_status_is_loading_text(const char *wifi_text)
{
    return status_text_is_loading(wifi_text);
}

static void create_status_chips(lv_obj_t *top, const ui_scaffold_config_t *config)
{
    const char *time_text = config != NULL && config->time_text != NULL ? config->time_text : "--:--";
    const char *wifi_text = config != NULL && config->wifi_text != NULL ? config->wifi_text : "Offline";
    bool wifi_connected = config != NULL && config->wifi_connected;
    bool time_loading = time_text_is_loading(time_text);

    /* Clock is a small, muted label on the same baseline as the app name,
     * just to the left of the title. The upper row remains reserved for the
     * screen icon.
     */
    s_time_label = lv_label_create(top);
    lv_label_set_text(s_time_label, time_loading ? "" : time_text);
    lv_obj_set_width(s_time_label, 62);
    lv_obj_set_style_text_font(s_time_label, UI_TIME_FONT, 0);
    lv_obj_set_style_text_color(s_time_label, lv_color_hex(UI_COLOR_DIM), 0);
    lv_obj_set_style_text_align(s_time_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_time_label, LV_ALIGN_TOP_MID, -112, 34);

    s_time_loading_dots = create_loading_dots(top, UI_COLOR_DIM);
    if (s_time_loading_dots != NULL) {
        lv_obj_align(s_time_loading_dots, LV_ALIGN_TOP_MID, -100, 37);
        if (!time_loading) {
            lv_obj_add_flag(s_time_loading_dots, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (time_loading) {
        lv_obj_add_flag(s_time_label, LV_OBJ_FLAG_HIDDEN);
    }

    /* Single Wi-Fi icon, aligned with the title baseline. State is shown only
     * by color/pulsing; no SSID/state text or extra glyphs.
     */
    s_wifi_icon_label = lv_label_create(top);
    lv_obj_set_width(s_wifi_icon_label, 30);
    lv_obj_set_style_text_font(s_wifi_icon_label, UI_TITLE_ICON_FONT, 0);
    lv_obj_set_style_text_align(s_wifi_icon_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_wifi_icon_label, LV_ALIGN_TOP_RIGHT, -52, 31);
    wifi_icon_apply_state(wifi_connected, wifi_text);
}

void ui_scaffold_update_status(const char *time_text, bool wifi_connected, const char *wifi_text, int wifi_rssi)
{
    (void)wifi_rssi;

    bool time_loading = time_text_is_loading(time_text);

    if (s_time_label != NULL) {
        lv_label_set_text(s_time_label, time_loading ? "" : (time_text != NULL ? time_text : "--:--"));
        if (time_loading) {
            lv_obj_add_flag(s_time_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_time_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_time_loading_dots != NULL) {
        if (time_loading) {
            lv_obj_clear_flag(s_time_loading_dots, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_time_loading_dots, LV_OBJ_FLAG_HIDDEN);
        }
    }

    wifi_icon_apply_state(wifi_connected, wifi_text);
}

static void create_top_slice(lv_obj_t *screen, const ui_scaffold_config_t *config)
{
    lv_obj_t *top = create_region(screen, UI_THEME_BODY_WIDTH, UI_THEME_SLICE_SIZE);

    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);

    s_title_is_time = config != NULL && config->title_is_time;

    create_title_icon(
        top,
        config != NULL ? config->title_icon : UI_SCAFFOLD_TITLE_ICON_NONE
    );

    create_scrolling_title(
        top,
        config != NULL && config->title != NULL ? config->title : ""
    );

    create_status_chips(top, config);
}

static void create_left_slice(lv_obj_t *screen, const ui_scaffold_config_t *config)
{
    lv_obj_t *left = create_region(screen, UI_THEME_SLICE_SIZE, UI_THEME_BODY_HEIGHT);

    lv_obj_align(left, LV_ALIGN_LEFT_MID, 0, 0);

    if (config == NULL || !config->show_back) {
        return;
    }

    lv_obj_t *back_hitbox = lv_obj_create(left);
    lv_obj_set_size(back_hitbox, 54, 54);
    lv_obj_center(back_hitbox);
    lv_obj_set_style_radius(back_hitbox, UI_THEME_PILL_RADIUS, 0);
    lv_obj_set_style_bg_color(back_hitbox, lv_color_hex(UI_COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(back_hitbox, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(back_hitbox, lv_color_hex(UI_COLOR_CARD_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(back_hitbox, lv_color_hex(UI_COLOR_BORDER_SOFT), 0);
    lv_obj_set_style_border_width(back_hitbox, 1, 0);
    lv_obj_set_style_pad_all(back_hitbox, 0, 0);
    lv_obj_set_style_shadow_width(back_hitbox, 0, 0);
    lv_obj_clear_flag(back_hitbox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(back_hitbox, LV_OBJ_FLAG_CLICKABLE);

    if (config->back_cb != NULL) {
        lv_obj_add_event_cb(back_hitbox, config->back_cb, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t *icon = lv_label_create(back_hitbox);
    lv_label_set_text(icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(icon, UI_BACK_ICON_FONT, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(UI_COLOR_PRIMARY_SOFT), LV_STATE_PRESSED);
    lv_obj_center(icon);
}

static void create_empty_slices(lv_obj_t *screen, bool show_bottom_slice)
{
    lv_obj_t *right = create_region(screen, UI_THEME_SLICE_SIZE, UI_THEME_BODY_HEIGHT);
    lv_obj_align(right, LV_ALIGN_RIGHT_MID, 0, 0);

    if (show_bottom_slice) {
        lv_obj_t *bottom = create_region(screen, UI_THEME_BODY_WIDTH, UI_THEME_SLICE_SIZE);
        lv_obj_align(bottom, LV_ALIGN_BOTTOM_MID, 0, 0);
    }
}

static lv_obj_t *create_body(lv_obj_t *screen, bool show_bottom_slice, bool full_width_body)
{
    lv_obj_t *body = lv_obj_create(screen);

    int32_t body_height = show_bottom_slice ?
        UI_THEME_BODY_HEIGHT :
        UI_THEME_BODY_HEIGHT_NO_BOTTOM;
    int32_t body_width = full_width_body ? UI_THEME_SCREEN_WIDTH : UI_THEME_BODY_WIDTH;

    lv_obj_set_size(body, body_width, body_height);

    if (show_bottom_slice && !full_width_body) {
        lv_obj_center(body);
    } else {
        lv_obj_align(body, LV_ALIGN_TOP_MID, 0, UI_THEME_SLICE_SIZE);
    }

    /*
     * The body is visually the same color as the screen. Making it opaque
     * avoids transparent-background blending while scrolling dense text rows.
     */
    lv_obj_set_style_bg_color(body, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 8, 0);
    lv_obj_set_style_pad_right(body, UI_THEME_BODY_RIGHT_SCROLL_PADDING, 0);
    lv_obj_set_style_pad_gap(body, 12, 0);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(body, 4, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(body, lv_color_hex(UI_COLOR_PRIMARY), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(body, LV_OPA_50, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(body, UI_THEME_PILL_RADIUS, LV_PART_SCROLLBAR);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLL_MOMENTUM);
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
    bool show_bottom_slice = config != NULL && config->show_bottom_slice;

    prepare_screen(screen);

    create_top_slice(screen, config);
    create_left_slice(screen, config);
    create_empty_slices(screen, show_bottom_slice);

    return create_body(screen, show_bottom_slice, config != NULL && config->full_width_body);
}
