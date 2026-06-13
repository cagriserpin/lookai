/**
 * @file ui/menus/settings/stt/stt_screen.c
 * @brief Speech-to-text UI body implementation.
 */

#include "stt_screen.h"

#include <string.h>

#include "ui_theme.h"

#define STT_COLOR_GREEN 0x22C55E
#define STT_COLOR_GREEN_DARK 0x14532D
#define STT_COLOR_GREEN_SOFT 0x86EFAC
#define STT_COLOR_PLAY_BLUE 0x2D7EE8
#define STT_COLOR_PLAY_BLUE_DARK 0x1D4ED8

#define STT_CONTAINER_WIDTH UI_THEME_CARD_WIDTH
#define STT_CONTAINER_HEIGHT 315
#define STT_BUTTON_SIZE 132
#define STT_PLAY_BUTTON_WIDTH 196
#define STT_PLAY_BUTTON_HEIGHT 42
#define STT_TEXT_WIDTH (UI_THEME_CARD_WIDTH - 44)

typedef struct {
    const ui_manager_callbacks_t *callbacks;
    lv_obj_t *status_label;
    lv_obj_t *message_label;
    lv_obj_t *button_label;
} stt_button_context_t;

static stt_button_context_t s_button_context = {0};

static void record_button_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *button = (lv_obj_t *)lv_event_get_target(event);
    stt_button_context_t *context =
        (stt_button_context_t *)lv_event_get_user_data(event);

    if (button == NULL || context == NULL) {
        return;
    }

    if (code == LV_EVENT_PRESSED) {
        lv_obj_set_style_bg_color(button, lv_color_hex(STT_COLOR_GREEN_DARK), 0);

        /*
         * Update the visible STT state locally while the user is still holding
         * the button. Do not ask ui_manager to re-render here, otherwise the
         * pressed button object can be deleted before LVGL sends RELEASED.
         */
        if (context->status_label != NULL) {
            lv_label_set_text(context->status_label, "Recording...");
        }

        if (context->message_label != NULL) {
            lv_label_set_text(context->message_label, "Release when you are done speaking.");
        }

        if (context->button_label != NULL) {
            lv_label_set_text(context->button_label, "Release\nto Send");
        }

        if (context->callbacks != NULL && context->callbacks->stt_press != NULL) {
            context->callbacks->stt_press();
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        lv_obj_set_style_bg_color(button, lv_color_hex(STT_COLOR_GREEN), 0);

        if (context->status_label != NULL) {
            lv_label_set_text(context->status_label, "Processing...");
        }

        if (context->message_label != NULL) {
            lv_label_set_text(context->message_label, "");
        }

        if (context->button_label != NULL) {
            lv_label_set_text(context->button_label, "Push\nto Talk");
        }

        if (context->callbacks != NULL && context->callbacks->stt_release != NULL) {
            context->callbacks->stt_release();
        }
    }
}

static void play_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    const ui_manager_callbacks_t *callbacks =
        (const ui_manager_callbacks_t *)lv_event_get_user_data(event);

    if (callbacks != NULL && callbacks->stt_play != NULL) {
        callbacks->stt_play();
    }
}

static lv_obj_t *create_centered_label(
    lv_obj_t *parent,
    const char *text,
    uint32_t color
)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, text != NULL ? text : "");
    lv_obj_set_width(label, STT_TEXT_WIDTH);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

    return label;
}

static bool is_audio_playing(const ui_manager_state_t *state)
{
    return state != NULL &&
        state->stt_processing &&
        strcmp(state->stt_status, "Playing audio...") == 0;
}

void stt_screen_render(
    lv_obj_t *body,
    const ui_manager_state_t *state,
    const ui_manager_callbacks_t *callbacks
)
{
    const char *status = "Ready";
    const char *result = "";

    if (state != NULL) {
        status = state->stt_status[0] != '\0' ? state->stt_status : "Ready";
        result = state->stt_result;
    }

    bool audio_playing = is_audio_playing(state);

    /*
     * STT intentionally fits inside the visible body and does not use the
     * common bottom spacer. The scaffold title already shows "Speech to Text".
     */
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *container = lv_obj_create(body);

    lv_obj_set_size(container, STT_CONTAINER_WIDTH, STT_CONTAINER_HEIGHT);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_left(container, 0, 0);
    lv_obj_set_style_pad_right(container, 0, 0);
    lv_obj_set_style_pad_top(container, 0, 0);
    lv_obj_set_style_pad_bottom(container, 0, 0);
    lv_obj_set_style_pad_gap(container, 8, 0);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        container,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );

    lv_obj_t *status_label = create_centered_label(container, status, STT_COLOR_GREEN_SOFT);
    lv_obj_t *message_label = NULL;

    if (result != NULL && result[0] != '\0') {
        message_label = create_centered_label(container, result, UI_COLOR_TEXT);
    } else {
        message_label = create_centered_label(container, "Speak while pressing the button.", UI_COLOR_MUTED);
    }

    lv_obj_t *button_holder = lv_obj_create(container);
    lv_obj_set_size(button_holder, STT_CONTAINER_WIDTH, 142);
    lv_obj_set_style_bg_opa(button_holder, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(button_holder, 0, 0);
    lv_obj_set_style_pad_all(button_holder, 0, 0);
    lv_obj_set_scrollbar_mode(button_holder, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(button_holder, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *button = lv_button_create(button_holder);

    lv_obj_set_size(button, STT_BUTTON_SIZE, STT_BUTTON_SIZE);
    lv_obj_set_style_radius(button, STT_BUTTON_SIZE / 2, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(STT_COLOR_GREEN), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(STT_COLOR_GREEN_SOFT), 0);
    lv_obj_set_style_border_width(button, 3, 0);
    lv_obj_set_style_shadow_width(button, 14, 0);
    lv_obj_set_style_shadow_spread(button, 1, 0);
    lv_obj_set_style_shadow_color(button, lv_color_hex(STT_COLOR_GREEN_DARK), 0);
    lv_obj_set_style_shadow_opa(button, LV_OPA_40, 0);
    lv_obj_set_style_pad_all(button, 14, 0);
    lv_obj_set_scrollbar_mode(button, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_align(button, LV_ALIGN_BOTTOM_MID, 0, -4);

    lv_obj_t *button_label = lv_label_create(button);
    lv_label_set_text(button_label, "Push\nto Talk");
    lv_obj_set_width(button_label, STT_BUTTON_SIZE - 28);
    lv_obj_set_style_text_color(button_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_align(button_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(button_label);

    s_button_context.callbacks = callbacks;
    s_button_context.status_label = status_label;
    s_button_context.message_label = message_label;
    s_button_context.button_label = button_label;

    lv_obj_add_event_cb(button, record_button_event_cb, LV_EVENT_PRESSED, &s_button_context);
    lv_obj_add_event_cb(button, record_button_event_cb, LV_EVENT_RELEASED, &s_button_context);
    lv_obj_add_event_cb(button, record_button_event_cb, LV_EVENT_PRESS_LOST, &s_button_context);

    lv_obj_t *play_button = lv_button_create(container);
    lv_obj_set_size(play_button, STT_PLAY_BUTTON_WIDTH, STT_PLAY_BUTTON_HEIGHT);
    lv_obj_set_style_radius(play_button, STT_PLAY_BUTTON_HEIGHT / 2, 0);
    lv_obj_set_style_bg_color(play_button, lv_color_hex(STT_COLOR_PLAY_BLUE), 0);
    lv_obj_set_style_bg_opa(play_button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(play_button, lv_color_hex(STT_COLOR_PLAY_BLUE_DARK), 0);
    lv_obj_set_style_border_width(play_button, 2, 0);
    lv_obj_set_style_pad_all(play_button, 0, 0);
    lv_obj_set_scrollbar_mode(play_button, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(play_button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(play_button, play_button_event_cb, LV_EVENT_CLICKED, (void *)callbacks);

    lv_obj_t *play_label = lv_label_create(play_button);
    lv_label_set_text(play_label, audio_playing ? "Stop Audio" : "Play Audio");
    lv_obj_set_style_text_color(play_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_center(play_label);
}
