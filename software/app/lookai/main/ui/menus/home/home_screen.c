/**
 * @file ui/menus/home/home_screen.c
 * @brief Home app picker UI implementation.
 */

#include "home_screen.h"

#include <stdlib.h>
#include <string.h>

#include "ui_icons.h"
#include "ui_theme.h"

#ifndef HOME_APP_ICON_FONT
#define HOME_APP_ICON_FONT (&lv_font_montserrat_28)
#endif

#ifndef HOME_APP_TEXT_ICON_FONT
#define HOME_APP_TEXT_ICON_FONT (&lv_font_montserrat_18)
#endif

#ifndef HOME_APP_TITLE_FONT
#define HOME_APP_TITLE_FONT (&lv_font_montserrat_28)
#endif

#define HOME_APP_COUNT 4
#define HOME_CARD_W 164
#define HOME_CARD_H 308
#define HOME_CARD_GAP 0
#define HOME_CAROUSEL_H 336
#define HOME_ICON_SLOT_SIZE 150
#define HOME_ICON_PILL_MIN 94
#define HOME_ICON_PILL_MAX 138
#define HOME_VIEWPORT_W UI_THEME_SCREEN_WIDTH
#define HOME_DISTANCE_MAX 165
#define HOME_OPA_MIN 51
#define HOME_OPA_MAX 255
#define HOME_DOT_SIZE 7
#define HOME_DOT_GAP 8
#define HOME_DOTS_MARGIN_TOP 10

static int32_t s_home_scroll_x = 0;
static uint32_t s_home_selected_index = 0;
static bool s_home_snapping = false;
static bool s_home_intro_played = false;
static lv_obj_t *s_intro_carousel = NULL;
static lv_obj_t *s_intro_dots = NULL;
static int32_t s_intro_card_from_x[HOME_APP_COUNT] = {0};

static void home_translate_x_anim_cb(void *object, int32_t value)
{
    lv_obj_set_style_translate_x((lv_obj_t *)object, value, 0);
}

static void home_translate_y_anim_cb(void *object, int32_t value)
{
    lv_obj_set_style_translate_y((lv_obj_t *)object, value, 0);
}

static void home_bg_opa_anim_cb(void *object, int32_t value)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)object, (lv_opa_t)value, 0);
}

static void home_text_opa_anim_cb(void *object, int32_t value)
{
    lv_obj_set_style_text_opa((lv_obj_t *)object, (lv_opa_t)value, 0);
}

static void start_home_items_intro(void);

static void hello_splash_delete_ready_cb(lv_anim_t *anim)
{
    lv_obj_t *hello = (lv_obj_t *)lv_anim_get_user_data(anim);
    if (hello == NULL) {
        return;
    }

    lv_obj_t *overlay = lv_obj_get_parent(hello);
    if (overlay != NULL) {
        lv_obj_delete(overlay);
    }

    start_home_items_intro();
}

static void hello_splash_fade_out(lv_obj_t *hello)
{
    if (hello == NULL) {
        return;
    }

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, hello);
    lv_anim_set_exec_cb(&anim, home_text_opa_anim_cb);
    lv_anim_set_values(&anim, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_delay(&anim, 650);
    lv_anim_set_time(&anim, 420);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in);
    lv_anim_set_user_data(&anim, hello);
    lv_anim_set_ready_cb(&anim, hello_splash_delete_ready_cb);
    lv_anim_start(&anim);
}

static void hello_splash_fade_in_ready_cb(lv_anim_t *anim)
{
    lv_obj_t *hello = (lv_obj_t *)lv_anim_get_user_data(anim);
    hello_splash_fade_out(hello);
}

static void start_home_intro(void)
{
    if (s_home_intro_played) {
        return;
    }

    s_home_intro_played = true;

    lv_obj_t *overlay = lv_obj_create(lv_layer_top());
    if (overlay == NULL) {
        start_home_items_intro();
        return;
    }

    lv_obj_set_size(overlay, UI_THEME_SCREEN_WIDTH, UI_THEME_SCREEN_HEIGHT);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_align(overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_move_foreground(overlay);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *hello = lv_label_create(overlay);
    lv_label_set_text_static(hello, "Hello");
    lv_obj_set_style_text_font(hello, HOME_APP_TITLE_FONT, 0);
    lv_obj_set_style_text_letter_space(hello, 3, 0);
    lv_obj_set_style_text_color(hello, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_opa(hello, LV_OPA_TRANSP, 0);
    lv_obj_align(hello, LV_ALIGN_CENTER, 0, 0);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, hello);
    lv_anim_set_exec_cb(&anim, home_text_opa_anim_cb);
    lv_anim_set_values(&anim, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_delay(&anim, 80);
    lv_anim_set_time(&anim, 620);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_user_data(&anim, hello);
    lv_anim_set_ready_cb(&anim, hello_splash_fade_in_ready_cb);
    lv_anim_start(&anim);
}

static bool wifi_is_connected(const ui_manager_state_t *state)
{
    return state != NULL && state->wifi_connected && state->wifi_enabled;
}

static void set_card_enabled(lv_obj_t *card, bool enabled)
{
    if (card == NULL) {
        return;
    }

    if (enabled) {
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_state(card, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_state(card, LV_STATE_DISABLED);
    }
}

static void add_card_click_target(lv_obj_t *obj, lv_event_cb_t cb)
{
    if (obj == NULL || cb == NULL) {
        return;
    }

    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, NULL);
}

static uint32_t clamp_index(uint32_t index, uint32_t count)
{
    if (count == 0) {
        return 0;
    }
    return index >= count ? count - 1 : index;
}

static int32_t card_distance_from_carousel_center(lv_obj_t *carousel, lv_obj_t *card)
{
    if (carousel == NULL || card == NULL) {
        return 0;
    }

    lv_area_t carousel_area;
    lv_area_t card_area;
    lv_obj_get_coords(carousel, &carousel_area);
    lv_obj_get_coords(card, &card_area);

    int32_t carousel_center = carousel_area.x1 + ((carousel_area.x2 - carousel_area.x1 + 1) / 2);
    int32_t card_center = card_area.x1 + ((card_area.x2 - card_area.x1 + 1) / 2);
    return card_center - carousel_center;
}

static uint32_t nearest_card_index(lv_obj_t *carousel)
{
    if (carousel == NULL) {
        return 0;
    }

    lv_obj_update_layout(carousel);
    uint32_t child_count = lv_obj_get_child_count(carousel);
    uint32_t best_index = 0;
    int32_t best_distance = 1000000;

    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t *card = lv_obj_get_child(carousel, i);
        if (card == NULL) {
            continue;
        }

        int32_t distance = card_distance_from_carousel_center(carousel, card);
        if (distance < 0) {
            distance = -distance;
        }

        if (distance < best_distance) {
            best_distance = distance;
            best_index = i;
        }
    }

    return best_index;
}

static lv_obj_t *card_icon_slot(lv_obj_t *card)
{
    if (card == NULL || lv_obj_get_child_count(card) < 1) {
        return NULL;
    }

    return lv_obj_get_child(card, 0);
}

static lv_obj_t *card_icon_pill(lv_obj_t *card)
{
    lv_obj_t *slot = card_icon_slot(card);
    if (slot == NULL || lv_obj_get_child_count(slot) < 1) {
        return NULL;
    }

    return lv_obj_get_child(slot, 0);
}

static lv_obj_t *card_icon_label(lv_obj_t *card)
{
    lv_obj_t *pill = card_icon_pill(card);
    if (pill == NULL || lv_obj_get_child_count(pill) < 1) {
        return NULL;
    }

    return lv_obj_get_child(pill, 0);
}

static lv_obj_t *card_title_label(lv_obj_t *card)
{
    return card != NULL && lv_obj_get_child_count(card) > 1 ? lv_obj_get_child(card, 1) : NULL;
}

static lv_obj_t *card_subtitle_label(lv_obj_t *card)
{
    return card != NULL && lv_obj_get_child_count(card) > 2 ? lv_obj_get_child(card, 2) : NULL;
}

static lv_opa_t card_opa_for_distance(lv_obj_t *card, int32_t distance)
{
    if (distance < 0) {
        distance = -distance;
    }
    if (distance > HOME_DISTANCE_MAX) {
        distance = HOME_DISTANCE_MAX;
    }

    int32_t focus = HOME_DISTANCE_MAX - distance;
    int32_t opa = HOME_OPA_MIN + (focus * (HOME_OPA_MAX - HOME_OPA_MIN) / HOME_DISTANCE_MAX);
    if (opa < HOME_OPA_MIN) {
        opa = HOME_OPA_MIN;
    }
    if (opa > HOME_OPA_MAX) {
        opa = HOME_OPA_MAX;
    }
    if (lv_obj_has_state(card, LV_STATE_DISABLED) && opa > LV_OPA_40) {
        opa = LV_OPA_40;
    }

    return (lv_opa_t)opa;
}

static lv_opa_t card_target_opa(lv_obj_t *carousel, lv_obj_t *card)
{
    if (carousel == NULL || card == NULL) {
        return LV_OPA_COVER;
    }

    return card_opa_for_distance(card, card_distance_from_carousel_center(carousel, card));
}

static void animate_obj_x(lv_obj_t *obj, int32_t from, int32_t to, uint32_t delay_ms, uint32_t time_ms)
{
    if (obj == NULL) {
        return;
    }

    lv_obj_set_style_translate_x(obj, from, 0);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, obj);
    lv_anim_set_exec_cb(&anim, home_translate_x_anim_cb);
    lv_anim_set_values(&anim, from, to);
    lv_anim_set_delay(&anim, delay_ms);
    lv_anim_set_time(&anim, time_ms);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);
}

static void animate_obj_y(lv_obj_t *obj, int32_t from, int32_t to, uint32_t delay_ms, uint32_t time_ms)
{
    if (obj == NULL) {
        return;
    }

    lv_obj_set_style_translate_y(obj, from, 0);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, obj);
    lv_anim_set_exec_cb(&anim, home_translate_y_anim_cb);
    lv_anim_set_values(&anim, from, to);
    lv_anim_set_delay(&anim, delay_ms);
    lv_anim_set_time(&anim, time_ms);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);
}

static void animate_text_opa(lv_obj_t *obj, lv_opa_t from, lv_opa_t to, uint32_t delay_ms, uint32_t time_ms)
{
    if (obj == NULL) {
        return;
    }

    lv_obj_set_style_text_opa(obj, from, 0);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, obj);
    lv_anim_set_exec_cb(&anim, home_text_opa_anim_cb);
    lv_anim_set_values(&anim, from, to);
    lv_anim_set_delay(&anim, delay_ms);
    lv_anim_set_time(&anim, time_ms);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);
}

static void animate_bg_opa(lv_obj_t *obj, lv_opa_t from, lv_opa_t to, uint32_t delay_ms, uint32_t time_ms)
{
    if (obj == NULL) {
        return;
    }

    lv_obj_set_style_bg_opa(obj, from, 0);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, obj);
    lv_anim_set_exec_cb(&anim, home_bg_opa_anim_cb);
    lv_anim_set_values(&anim, from, to);
    lv_anim_set_delay(&anim, delay_ms);
    lv_anim_set_time(&anim, time_ms);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);
}

static void prepare_home_items_intro(lv_obj_t *carousel, lv_obj_t *dots)
{
    if (s_home_intro_played || carousel == NULL) {
        return;
    }

    s_intro_carousel = carousel;
    s_intro_dots = dots;
    lv_obj_update_layout(carousel);

    uint32_t child_count = lv_obj_get_child_count(carousel);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t *card = lv_obj_get_child(carousel, i);
        if (card == NULL) {
            continue;
        }

        int32_t distance = card_distance_from_carousel_center(carousel, card);
        if (i < HOME_APP_COUNT) {
            s_intro_card_from_x[i] = -distance;
        }
        lv_obj_set_style_translate_x(card, -distance, 0);
        lv_obj_set_style_translate_y(card, 18, 0);

        lv_obj_t *pill = card_icon_pill(card);
        if (pill != NULL) {
            lv_obj_set_style_bg_opa(pill, LV_OPA_TRANSP, 0);
        }

        lv_obj_t *icon = card_icon_label(card);
        lv_obj_t *title = card_title_label(card);
        lv_obj_t *subtitle = card_subtitle_label(card);
        if (icon != NULL) {
            lv_obj_set_style_text_opa(icon, LV_OPA_TRANSP, 0);
        }
        if (title != NULL) {
            lv_obj_set_style_text_opa(title, LV_OPA_TRANSP, 0);
        }
        if (subtitle != NULL) {
            lv_obj_set_style_text_opa(subtitle, LV_OPA_TRANSP, 0);
        }
    }

    if (dots != NULL) {
        lv_obj_set_style_translate_y(dots, 12, 0);
    }
}

static void start_home_items_intro(void)
{
    if (s_intro_carousel == NULL) {
        return;
    }

    lv_obj_t *carousel = s_intro_carousel;
    lv_obj_update_layout(carousel);

    uint32_t child_count = lv_obj_get_child_count(carousel);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t *card = lv_obj_get_child(carousel, i);
        if (card == NULL) {
            continue;
        }

        int32_t from_x = i < HOME_APP_COUNT ? s_intro_card_from_x[i] : 0;
        uint32_t delay = 40 + (i * 55);
        uint32_t time = 430;
        lv_opa_t target_opa = card_opa_for_distance(card, -from_x);

        animate_obj_x(card, from_x, 0, delay, time);
        animate_obj_y(card, 18, 0, delay, time);
        animate_bg_opa(card_icon_pill(card), LV_OPA_TRANSP, target_opa, delay, time);
        animate_text_opa(card_icon_label(card), LV_OPA_TRANSP, target_opa, delay, time);
        animate_text_opa(card_title_label(card), LV_OPA_TRANSP, target_opa, delay + 40, time);
        animate_text_opa(card_subtitle_label(card), LV_OPA_TRANSP, target_opa, delay + 70, time);
    }

    if (s_intro_dots != NULL) {
        animate_obj_y(s_intro_dots, 12, 0, 140, 360);
    }
}

static void set_card_visual(lv_obj_t *card, int32_t distance)
{
    if (card == NULL) {
        return;
    }

    if (distance > HOME_DISTANCE_MAX) {
        distance = HOME_DISTANCE_MAX;
    }
    if (distance < 0) {
        distance = 0;
    }

    int32_t focus = HOME_DISTANCE_MAX - distance;
    int32_t opa = HOME_OPA_MIN + (focus * (HOME_OPA_MAX - HOME_OPA_MIN) / HOME_DISTANCE_MAX);
    int32_t pill_size = HOME_ICON_PILL_MIN + (focus * (HOME_ICON_PILL_MAX - HOME_ICON_PILL_MIN) / HOME_DISTANCE_MAX);

    if (opa < HOME_OPA_MIN) {
        opa = HOME_OPA_MIN;
    }
    if (opa > HOME_OPA_MAX) {
        opa = HOME_OPA_MAX;
    }
    if (pill_size < HOME_ICON_PILL_MIN) {
        pill_size = HOME_ICON_PILL_MIN;
    }
    if (pill_size > HOME_ICON_PILL_MAX) {
        pill_size = HOME_ICON_PILL_MAX;
    }

    if (lv_obj_has_state(card, LV_STATE_DISABLED)) {
        if (opa > LV_OPA_40) {
            opa = LV_OPA_40;
        }
    }

    lv_obj_t *slot = card_icon_slot(card);
    if (slot != NULL) {
        lv_obj_set_size(slot, HOME_ICON_SLOT_SIZE, HOME_ICON_SLOT_SIZE);
        lv_obj_set_style_radius(slot, HOME_ICON_SLOT_SIZE / 2, 0);
        lv_obj_set_style_bg_opa(slot, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(slot, LV_OPA_TRANSP, 0);
    }

    lv_obj_t *pill = card_icon_pill(card);
    if (pill != NULL) {
        lv_obj_set_size(pill, pill_size, pill_size);
        lv_obj_set_style_radius(pill, pill_size / 2, 0);
        lv_obj_set_style_bg_opa(pill, (lv_opa_t)opa, 0);
        lv_obj_center(pill);
    }

    lv_obj_t *icon = card_icon_label(card);
    if (icon != NULL) {
        lv_obj_set_style_text_opa(icon, (lv_opa_t)opa, 0);
    }

    lv_obj_t *title = card_title_label(card);
    if (title != NULL) {
        lv_obj_set_style_text_opa(title, (lv_opa_t)opa, 0);
    }

    lv_obj_t *subtitle = card_subtitle_label(card);
    if (subtitle != NULL) {
        lv_obj_set_style_text_opa(subtitle, (lv_opa_t)opa, 0);
    }
}

static void update_dots(lv_obj_t *dots, uint32_t selected_index)
{
    if (dots == NULL) {
        return;
    }

    uint32_t count = lv_obj_get_child_count(dots);
    selected_index = clamp_index(selected_index, count);

    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t *dot = lv_obj_get_child(dots, i);
        if (dot == NULL) {
            continue;
        }

        bool selected = i == selected_index;
        lv_obj_set_style_bg_color(
            dot,
            lv_color_hex(selected ? UI_COLOR_PRIMARY : UI_COLOR_BORDER_SOFT),
            0
        );
        lv_obj_set_style_bg_opa(dot, selected ? LV_OPA_COVER : LV_OPA_50, 0);
        lv_obj_set_size(dot, selected ? HOME_DOT_SIZE + 6 : HOME_DOT_SIZE, HOME_DOT_SIZE);
        lv_obj_set_style_radius(dot, HOME_DOT_SIZE / 2, 0);
    }
}

static lv_obj_t *find_dots_from_carousel(lv_obj_t *carousel)
{
    if (carousel == NULL) {
        return NULL;
    }

    lv_obj_t *body = lv_obj_get_parent(carousel);
    if (body == NULL || lv_obj_get_child_count(body) < 2) {
        return NULL;
    }

    return lv_obj_get_child(body, 1);
}

static void update_carousel_cards(lv_obj_t *carousel)
{
    if (carousel == NULL) {
        return;
    }

    lv_obj_update_layout(carousel);
    uint32_t child_count = lv_obj_get_child_count(carousel);

    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t *card = lv_obj_get_child(carousel, i);
        if (card == NULL) {
            continue;
        }

        int32_t distance = card_distance_from_carousel_center(carousel, card);
        if (distance < 0) {
            distance = -distance;
        }

        set_card_visual(card, distance);
    }

    s_home_selected_index = nearest_card_index(carousel);
    update_dots(find_dots_from_carousel(carousel), s_home_selected_index);
}

static void snap_carousel_to_nearest(lv_obj_t *carousel)
{
    if (carousel == NULL) {
        return;
    }

    uint32_t child_count = lv_obj_get_child_count(carousel);
    if (child_count == 0) {
        return;
    }

    uint32_t index = nearest_card_index(carousel);
    lv_obj_t *card = lv_obj_get_child(carousel, index);
    if (card == NULL) {
        return;
    }

    s_home_snapping = true;
    lv_obj_scroll_to_view(card, LV_ANIM_OFF);
    lv_obj_update_layout(carousel);
    s_home_snapping = false;
    s_home_scroll_x = lv_obj_get_scroll_x(carousel);
    s_home_selected_index = index;
    update_carousel_cards(carousel);
    update_dots(find_dots_from_carousel(carousel), s_home_selected_index);
}

static void carousel_event_cb(lv_event_t *event)
{
    lv_obj_t *carousel = (lv_obj_t *)lv_event_get_target(event);
    lv_event_code_t code = lv_event_get_code(event);

    if (carousel == NULL) {
        return;
    }

    if (code == LV_EVENT_SCROLL) {
        update_carousel_cards(carousel);
    }

    if (code == LV_EVENT_SCROLL_END && !s_home_snapping) {
        snap_carousel_to_nearest(carousel);
        update_carousel_cards(carousel);
    }
}

static lv_obj_t *create_app_card(
    lv_obj_t *parent,
    const char *icon,
    const char *title,
    const char *subtitle,
    uint32_t color,
    lv_event_cb_t cb
)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, HOME_CARD_W, HOME_CARD_H);
    lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_pad_gap(card, 12, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    add_card_click_target(card, cb);
    lv_obj_add_flag(card, LV_OBJ_FLAG_SNAPPABLE);

    lv_obj_t *icon_slot = lv_obj_create(card);
    lv_obj_set_size(icon_slot, HOME_ICON_SLOT_SIZE, HOME_ICON_SLOT_SIZE);
    lv_obj_set_style_bg_opa(icon_slot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(icon_slot, 0, 0);
    lv_obj_set_style_radius(icon_slot, HOME_ICON_SLOT_SIZE / 2, 0);
    lv_obj_set_style_pad_all(icon_slot, 0, 0);
    lv_obj_clear_flag(icon_slot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(icon_slot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(icon_slot, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);

    lv_obj_t *icon_pill = lv_obj_create(icon_slot);
    lv_obj_set_size(icon_pill, HOME_ICON_PILL_MAX, HOME_ICON_PILL_MAX);
    lv_obj_set_style_radius(icon_pill, HOME_ICON_PILL_MAX / 2, 0);
    lv_obj_set_style_bg_color(icon_pill, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(icon_pill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(icon_pill, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_border_opa(icon_pill, LV_OPA_20, 0);
    lv_obj_set_style_border_width(icon_pill, 1, 0);
    lv_obj_set_style_shadow_width(icon_pill, 0, 0);
    lv_obj_clear_flag(icon_pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(icon_pill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(icon_pill, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_center(icon_pill);

    lv_obj_t *icon_label = lv_label_create(icon_pill);
    lv_label_set_text_static(icon_label, icon != NULL ? icon : "");
    const bool text_icon =
        icon != NULL &&
        (strcmp(icon, "AI") == 0 || strcmp(icon, "STT") == 0 || strcmp(icon, "TTS") == 0);
    lv_obj_set_style_text_font(icon_label, text_icon ? HOME_APP_TEXT_ICON_FONT : HOME_APP_ICON_FONT, 0);
    lv_obj_set_style_text_color(icon_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_clear_flag(icon_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(icon_label);

    lv_obj_t *title_label = lv_label_create(card);
    lv_label_set_text_static(title_label, title != NULL ? title : "App");
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(title_label, HOME_CARD_W);
    lv_obj_set_style_text_letter_space(title_label, 1, 0);
    lv_obj_set_style_text_font(title_label, HOME_APP_TITLE_FONT, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_clear_flag(title_label, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *subtitle_label = lv_label_create(card);
    lv_label_set_text_static(subtitle_label, subtitle != NULL ? subtitle : "");
    lv_label_set_long_mode(subtitle_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(subtitle_label, HOME_CARD_W - 24);
    lv_obj_set_style_text_color(subtitle_label, lv_color_hex(UI_COLOR_MUTED), 0);
    lv_obj_set_style_text_align(subtitle_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_clear_flag(subtitle_label, LV_OBJ_FLAG_CLICKABLE);

    return card;
}

static lv_obj_t *create_dots(lv_obj_t *body)
{
    lv_obj_t *dots = lv_obj_create(body);
    lv_obj_set_size(dots, LV_SIZE_CONTENT, 22);
    lv_obj_set_style_bg_opa(dots, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dots, 0, 0);
    lv_obj_set_style_pad_all(dots, 0, 0);
    lv_obj_set_style_pad_gap(dots, HOME_DOT_GAP, 0);
    lv_obj_set_style_margin_top(dots, HOME_DOTS_MARGIN_TOP, 0);
    lv_obj_clear_flag(dots, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (uint32_t i = 0; i < HOME_APP_COUNT; i++) {
        lv_obj_t *dot = lv_obj_create(dots);
        lv_obj_set_size(dot, HOME_DOT_SIZE, HOME_DOT_SIZE);
        lv_obj_set_style_radius(dot, HOME_DOT_SIZE / 2, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    }

    return dots;
}

void home_screen_render(
    lv_obj_t *body,
    const ui_manager_state_t *state,
    lv_event_cb_t stt_app_cb,
    lv_event_cb_t ai_app_cb,
    lv_event_cb_t tts_app_cb,
    lv_event_cb_t settings_cb
)
{
    bool online = wifi_is_connected(state);

    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(body, LV_DIR_NONE);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_set_style_pad_gap(body, 0, 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *carousel = lv_obj_create(body);
    lv_obj_set_size(carousel, HOME_VIEWPORT_W, HOME_CAROUSEL_H);
    lv_obj_set_style_bg_opa(carousel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(carousel, 0, 0);
    lv_obj_set_style_radius(carousel, 0, 0);
    lv_obj_set_style_pad_left(carousel, (HOME_VIEWPORT_W - HOME_CARD_W) / 2, 0);
    lv_obj_set_style_pad_right(carousel, (HOME_VIEWPORT_W - HOME_CARD_W) / 2, 0);
    lv_obj_set_style_pad_top(carousel, 0, 0);
    lv_obj_set_style_pad_bottom(carousel, 0, 0);
    lv_obj_set_style_pad_column(carousel, HOME_CARD_GAP, 0);
    lv_obj_set_scroll_dir(carousel, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(carousel, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(carousel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(carousel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(carousel, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_flex_flow(carousel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(carousel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(carousel, carousel_event_cb, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(carousel, carousel_event_cb, LV_EVENT_SCROLL_END, NULL);

    lv_obj_t *ai = create_app_card(carousel, "AI", "AI", "Talk with AI", UI_COLOR_AI_CYAN, ai_app_cb);
    lv_obj_t *stt = create_app_card(carousel, "STT", "STT", "Speech to text", UI_COLOR_STT_GREEN, stt_app_cb);
    lv_obj_t *tts = create_app_card(carousel, "TTS", "TTS", "Text to speech", UI_COLOR_TTS_PURPLE, tts_app_cb);
    lv_obj_t *settings = create_app_card(carousel, LV_SYMBOL_SETTINGS, "Settings", "Device and API", UI_COLOR_PRIMARY, settings_cb);

    set_card_enabled(ai, online);
    set_card_enabled(stt, online);
    set_card_enabled(tts, online);
    set_card_enabled(settings, true);

    lv_obj_t *dots = create_dots(body);

    lv_obj_update_layout(carousel);
    if (s_home_scroll_x > 0) {
        lv_obj_scroll_to_x(carousel, s_home_scroll_x, LV_ANIM_OFF);
    } else {
        lv_obj_scroll_to_x(carousel, 0, LV_ANIM_OFF);
        s_home_selected_index = 0;
    }
    update_carousel_cards(carousel);
    update_dots(dots, s_home_selected_index);
    prepare_home_items_intro(carousel, dots);
    start_home_intro();
}

bool home_screen_update(lv_obj_t *body, const ui_manager_state_t *state)
{
    if (body == NULL || lv_obj_get_child_count(body) < 1) {
        return false;
    }

    lv_obj_t *carousel = lv_obj_get_child(body, 0);
    if (carousel == NULL || lv_obj_get_child_count(carousel) < HOME_APP_COUNT) {
        return false;
    }

    bool online = wifi_is_connected(state);
    set_card_enabled(lv_obj_get_child(carousel, 0), online);
    set_card_enabled(lv_obj_get_child(carousel, 1), online);
    set_card_enabled(lv_obj_get_child(carousel, 2), online);
    set_card_enabled(lv_obj_get_child(carousel, 3), true);
    update_carousel_cards(carousel);
    return true;
}

