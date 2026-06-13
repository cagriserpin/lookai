/**
 * @file ui/menus/settings/stt/stt_screen.c
 * @brief Speech-to-text UI body implementation.
 */

#include "stt_screen.h"

#include "ui_theme.h"

#ifndef STT_TALK_FONT
#define STT_TALK_FONT (&lv_font_montserrat_28)
#endif

#define STT_COLOR_GREEN 0x22C55E
#define STT_COLOR_GREEN_DARK 0x14532D
#define STT_COLOR_GREEN_SOFT 0x86EFAC
#define STT_COLOR_BLUE 0x2D7EE8
#define STT_COLOR_BLUE_DARK 0x1D4ED8
#define STT_COLOR_GRAY 0x334155
#define STT_COLOR_GRAY_DARK 0x1E293B

#define STT_CONTAINER_WIDTH UI_THEME_CARD_WIDTH
#define STT_CONTAINER_HEIGHT 330
#define STT_TALK_BUTTON_SIZE 124
#define STT_SMALL_BUTTON_WIDTH 196
#define STT_SMALL_BUTTON_HEIGHT 38
#define STT_TEXT_WIDTH (UI_THEME_CARD_WIDTH - 44)

typedef struct {
    const ui_manager_callbacks_t *callbacks;
    lv_obj_t *status_label;
    lv_obj_t *message_label;
} stt_talk_context_t;

static stt_talk_context_t s_talk_context = {0};

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
    stt_talk_context_t *context =
        (stt_talk_context_t *)lv_event_get_user_data(event);

    if (button == NULL || context == NULL) {
        return;
    }

    if (code == LV_EVENT_PRESSED) {
        lv_obj_set_style_bg_color(button, lv_color_hex(STT_COLOR_GREEN_DARK), 0);

        /*
         * Update visible recording state locally while the user is holding the
         * same LVGL button. The backend must not re-render the STT screen on
         * press, otherwise LVGL can lose the matching RELEASED event.
         */
        if (context->status_label != NULL) {
            lv_label_set_text(context->status_label, "Recording...");
        }

        if (context->message_label != NULL) {
            lv_label_set_text(context->message_label, "Release when you are done speaking.");
        }

        if (context->callbacks != NULL && context->callbacks->stt_press != NULL) {
            context->callbacks->stt_press();
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        lv_obj_set_style_bg_color(button, lv_color_hex(STT_COLOR_GREEN), 0);

        if (context->status_label != NULL) {
            lv_label_set_text(context->status_label, "Saving...");
        }

        if (context->message_label != NULL) {
            lv_label_set_text(context->message_label, "");
        }

        if (context->callbacks != NULL && context->callbacks->stt_release != NULL) {
            context->callbacks->stt_release();
        }
    }
}

static void test_speaker_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    const ui_manager_callbacks_t *callbacks =
        (const ui_manager_callbacks_t *)lv_event_get_user_data(event);

    if (callbacks != NULL && callbacks->stt_test_speaker != NULL) {
        callbacks->stt_test_speaker();
    }
}

static void play_recording_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    const ui_manager_callbacks_t *callbacks =
        (const ui_manager_callbacks_t *)lv_event_get_user_data(event);

    if (callbacks != NULL && callbacks->stt_play_recording != NULL) {
        callbacks->stt_play_recording();
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

static lv_obj_t *create_small_button(
    lv_obj_t *parent,
    const char *text,
    uint32_t color,
    uint32_t border_color,
    lv_event_cb_t cb,
    const ui_manager_callbacks_t *callbacks
)
{
    lv_obj_t *button = lv_button_create(parent);

    lv_obj_set_size(button, STT_SMALL_BUTTON_WIDTH, STT_SMALL_BUTTON_HEIGHT);
    lv_obj_set_style_radius(button, STT_SMALL_BUTTON_HEIGHT / 2, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(border_color), 0);
    lv_obj_set_style_border_width(button, 2, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_scrollbar_mode(button, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);

    if (cb != NULL) {
        lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, (void *)callbacks);
    }

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text != NULL ? text : "");
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_center(label);

    return button;
}

void stt_screen_render(
    lv_obj_t *body,
    const ui_manager_state_t *state,
    const ui_manager_callbacks_t *callbacks
)
{
    const char *status = "Ready";
    const char *result = "";

    bool recording = false;
    bool processing = false;
    bool speaker_test_active = false;
    bool recording_playback_active = false;

    if (state != NULL) {
        status = state->stt_status[0] != '\0' ? state->stt_status : "Ready";
        result = state->stt_result;
        recording = state->stt_recording;
        processing = state->stt_processing;
        speaker_test_active = state->stt_speaker_test_active;
        recording_playback_active = state->stt_recording_playback_active;
    }

    bool talk_enabled =
        !processing &&
        !speaker_test_active &&
        !recording_playback_active;

    bool test_speaker_enabled =
        !recording &&
        !processing &&
        !recording_playback_active;

    bool play_recording_enabled =
        !recording &&
        !processing &&
        !speaker_test_active &&
        !recording_playback_active;

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
    lv_obj_set_style_pad_gap(container, 7, 0);
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
        message_label = create_centered_label(container, "Press TALK while speaking.", UI_COLOR_MUTED);
    }

    lv_obj_t *talk_holder = lv_obj_create(container);
    lv_obj_set_size(talk_holder, STT_CONTAINER_WIDTH, 130);
    lv_obj_set_style_bg_opa(talk_holder, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(talk_holder, 0, 0);
    lv_obj_set_style_pad_all(talk_holder, 0, 0);
    lv_obj_set_scrollbar_mode(talk_holder, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(talk_holder, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *talk_button = lv_button_create(talk_holder);

    lv_obj_set_size(talk_button, STT_TALK_BUTTON_SIZE, STT_TALK_BUTTON_SIZE);
    lv_obj_set_style_radius(talk_button, STT_TALK_BUTTON_SIZE / 2, 0);
    lv_obj_set_style_bg_color(talk_button, lv_color_hex(STT_COLOR_GREEN), 0);
    lv_obj_set_style_bg_opa(talk_button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(talk_button, lv_color_hex(STT_COLOR_GREEN_SOFT), 0);
    lv_obj_set_style_border_width(talk_button, 3, 0);
    lv_obj_set_style_shadow_width(talk_button, 12, 0);
    lv_obj_set_style_shadow_spread(talk_button, 1, 0);
    lv_obj_set_style_shadow_color(talk_button, lv_color_hex(STT_COLOR_GREEN_DARK), 0);
    lv_obj_set_style_shadow_opa(talk_button, LV_OPA_40, 0);
    lv_obj_set_style_pad_all(talk_button, 10, 0);
    lv_obj_set_scrollbar_mode(talk_button, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(talk_button, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_align(talk_button, LV_ALIGN_BOTTOM_MID, 0, -3);

    lv_obj_t *talk_label = lv_label_create(talk_button);
    lv_label_set_text(talk_label, "TALK");
    lv_obj_set_style_text_font(talk_label, STT_TALK_FONT, 0);
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

    lv_obj_t *test_button = create_small_button(
        container,
        speaker_test_active ? "Stop Speaker" : "Test Speaker",
        speaker_test_active ? STT_COLOR_GRAY : STT_COLOR_BLUE,
        speaker_test_active ? STT_COLOR_GRAY_DARK : STT_COLOR_BLUE_DARK,
        test_speaker_button_event_cb,
        callbacks
    );
    set_button_enabled(test_button, test_speaker_enabled);

    lv_obj_t *play_recording_button = create_small_button(
        container,
        recording_playback_active ? "Playing..." : "Play Recording",
        STT_COLOR_BLUE,
        STT_COLOR_BLUE_DARK,
        play_recording_button_event_cb,
        callbacks
    );
    set_button_enabled(play_recording_button, play_recording_enabled);
}
