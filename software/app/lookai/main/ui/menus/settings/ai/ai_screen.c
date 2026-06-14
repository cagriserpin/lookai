/**
 * @file ui/menus/settings/ai/ai_screen.c
 * @brief Voice assistant UI body implementation.
 */

#include "ai_screen.h"

#include "runtime_diag.h"
#include "ui_theme.h"

#ifndef AI_TALK_FONT
#define AI_TALK_FONT (&lv_font_montserrat_28)
#endif

#define AI_COLOR_CYAN 0x38BDF8
#define AI_COLOR_CYAN_DARK 0x0369A1
#define AI_COLOR_CYAN_SOFT 0x7DD3FC

#define AI_CONTAINER_WIDTH UI_THEME_CARD_WIDTH
#define AI_CONTAINER_HEIGHT 330

#define AI_TALK_AREA_HEIGHT 130
#define AI_TALK_BUTTON_SIZE 112

#define AI_TEXT_PANEL_WIDTH UI_THEME_CARD_WIDTH
#define AI_TEXT_PANEL_HEIGHT 170
#define AI_TEXT_WIDTH (UI_THEME_CARD_WIDTH - 34)

typedef struct {
    const ui_manager_callbacks_t *callbacks;
    lv_obj_t *status_label;
    lv_obj_t *message_label;
} ai_talk_context_t;

static ai_talk_context_t s_talk_context = {0};

static void set_button_enabled(lv_obj_t *button, bool enabled)
{
    if (button == NULL) {
        return;
    }

    if (enabled) {
        lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_opa(button, LV_OPA_COVER, 0);
    } else {
        lv_obj_clear_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_opa(button, LV_OPA_40, 0);
    }
}

static void talk_button_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *button = (lv_obj_t *)lv_event_get_target(event);
    ai_talk_context_t *context =
        (ai_talk_context_t *)lv_event_get_user_data(event);

    if (button == NULL || context == NULL) {
        return;
    }

    if (code == LV_EVENT_PRESSED) {
        runtime_diag_log("button_ai_talk_pressed");
        lv_obj_set_style_bg_color(button, lv_color_hex(AI_COLOR_CYAN_DARK), 0);

        /*
         * Update visible recording state locally while the user is holding the
         * same LVGL button. The backend must not re-render the AI screen on
         * press, otherwise LVGL can lose the matching RELEASED event.
         */
        if (context->status_label != NULL) {
            lv_label_set_text(context->status_label, "Recording");
        }

        if (context->message_label != NULL) {
            lv_label_set_text(context->message_label, "Release TALK to ask AI.");
        }

        if (context->callbacks != NULL && context->callbacks->ai_press != NULL) {
            context->callbacks->ai_press();
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        runtime_diag_log(
            code == LV_EVENT_RELEASED ?
                "button_ai_talk_released" :
                "button_ai_talk_press_lost"
        );
        lv_obj_set_style_bg_color(button, lv_color_hex(AI_COLOR_CYAN), 0);

        if (context->status_label != NULL) {
            lv_label_set_text(context->status_label, "Saving");
        }

        if (context->message_label != NULL) {
            lv_label_set_text(context->message_label, "Preparing recording.");
        }

        if (context->callbacks != NULL && context->callbacks->ai_release != NULL) {
            context->callbacks->ai_release();
        }
    }
}

static void style_plain_container(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *create_text_label(
    lv_obj_t *parent,
    const char *text,
    uint32_t color,
    lv_text_align_t align
)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, text != NULL ? text : "");
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, AI_TEXT_WIDTH);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_obj_set_style_text_line_space(label, 2, 0);

    return label;
}

static lv_obj_t *create_text_panel(
    lv_obj_t *parent,
    const char *status,
    const char *message,
    uint32_t message_color,
    lv_obj_t **out_status_label,
    lv_obj_t **out_message_label
)
{
    lv_obj_t *panel = lv_obj_create(parent);

    lv_obj_set_size(panel, AI_TEXT_PANEL_WIDTH, AI_TEXT_PANEL_HEIGHT);
    lv_obj_set_style_bg_color(panel, lv_color_hex(UI_COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 18, 0);
    lv_obj_set_style_pad_left(panel, 14, 0);
    lv_obj_set_style_pad_right(panel, 14, 0);
    lv_obj_set_style_pad_top(panel, 12, 0);
    lv_obj_set_style_pad_bottom(panel, 12, 0);
    lv_obj_set_style_pad_gap(panel, 7, 0);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        panel,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START
    );

    lv_obj_t *status_label = create_text_label(
        panel,
        status,
        AI_COLOR_CYAN_SOFT,
        LV_TEXT_ALIGN_LEFT
    );

    lv_obj_t *message_label = create_text_label(
        panel,
        message,
        message_color,
        LV_TEXT_ALIGN_LEFT
    );

    if (out_status_label != NULL) {
        *out_status_label = status_label;
    }

    if (out_message_label != NULL) {
        *out_message_label = message_label;
    }

    return panel;
}

void ai_screen_render(
    lv_obj_t *body,
    const ui_manager_state_t *state,
    const ui_manager_callbacks_t *callbacks
)
{
    const char *status = "Ready";
    const char *result = "";
    const char *message = "Hold TALK to ask AI.";
    uint32_t message_color = UI_COLOR_MUTED;

    bool recording = false;
    bool busy = false;
    bool speaking = false;

    if (state != NULL) {
        status = state->ai_status[0] != '\0' ? state->ai_status : "Ready";
        result = state->ai_result;
        recording = state->ai_recording;
        busy = state->ai_busy;
        speaking = state->ai_speaking;

        if (result != NULL && result[0] != '\0') {
            message = result;
            message_color = UI_COLOR_TEXT;
        }
    }

    bool talk_enabled = !busy || recording;
    (void)speaking;

    /*
     * The AI page itself is fixed. Only the top text panel scrolls.
     */
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(body, LV_DIR_NONE);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *container = lv_obj_create(body);

    lv_obj_set_size(container, AI_CONTAINER_WIDTH, AI_CONTAINER_HEIGHT);
    style_plain_container(container);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        container,
        LV_FLEX_ALIGN_SPACE_BETWEEN,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );

    lv_obj_t *status_label = NULL;
    lv_obj_t *message_label = NULL;
    create_text_panel(
        container,
        status,
        message,
        message_color,
        &status_label,
        &message_label
    );

    lv_obj_t *talk_holder = lv_obj_create(container);
    lv_obj_set_size(talk_holder, AI_CONTAINER_WIDTH, AI_TALK_AREA_HEIGHT);
    style_plain_container(talk_holder);

    lv_obj_t *talk_button = lv_button_create(talk_holder);

    lv_obj_set_size(talk_button, AI_TALK_BUTTON_SIZE, AI_TALK_BUTTON_SIZE);
    lv_obj_set_style_radius(talk_button, AI_TALK_BUTTON_SIZE / 2, 0);
    lv_obj_set_style_bg_color(talk_button, lv_color_hex(AI_COLOR_CYAN), 0);
    lv_obj_set_style_bg_opa(talk_button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(talk_button, lv_color_hex(AI_COLOR_CYAN_SOFT), 0);
    lv_obj_set_style_border_width(talk_button, 3, 0);
    lv_obj_set_style_shadow_width(talk_button, 9, 0);
    lv_obj_set_style_shadow_spread(talk_button, 1, 0);
    lv_obj_set_style_shadow_color(talk_button, lv_color_hex(AI_COLOR_CYAN_DARK), 0);
    lv_obj_set_style_shadow_opa(talk_button, LV_OPA_40, 0);
    lv_obj_set_style_pad_all(talk_button, 8, 0);
    lv_obj_set_scrollbar_mode(talk_button, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(talk_button, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_center(talk_button);

    lv_obj_t *talk_label = lv_label_create(talk_button);
    lv_label_set_text(talk_label, "TALK");
    lv_obj_set_style_text_font(talk_label, AI_TALK_FONT, 0);
    lv_obj_set_style_text_color(talk_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_align(talk_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(talk_label);

    s_talk_context.callbacks = callbacks;
    s_talk_context.status_label = status_label;
    s_talk_context.message_label = message_label;

    lv_obj_add_event_cb(talk_button, talk_button_event_cb, LV_EVENT_PRESSED, &s_talk_context);
    lv_obj_add_event_cb(talk_button, talk_button_event_cb, LV_EVENT_RELEASED, &s_talk_context);
    lv_obj_add_event_cb(talk_button, talk_button_event_cb, LV_EVENT_PRESS_LOST, &s_talk_context);

    set_button_enabled(talk_button, talk_enabled);
}
