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

#define STT_TALK_AREA_HEIGHT 110
#define STT_TALK_BUTTON_SIZE 102

#define STT_TEXT_PANEL_WIDTH UI_THEME_CARD_WIDTH
#define STT_TEXT_PANEL_HEIGHT 150
#define STT_TEXT_WIDTH (UI_THEME_CARD_WIDTH - 34)

#define STT_ACTION_ROW_HEIGHT 40
#define STT_ACTION_BUTTON_WIDTH 86
#define STT_ACTION_BUTTON_HEIGHT 36
#define STT_ACTION_BUTTON_GAP 14

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
            lv_label_set_text(context->status_label, "Recording");
        }

        if (context->message_label != NULL) {
            lv_label_set_text(context->message_label, "Release TALK to transcribe.");
        }

        if (context->callbacks != NULL && context->callbacks->stt_press != NULL) {
            context->callbacks->stt_press();
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        lv_obj_set_style_bg_color(button, lv_color_hex(STT_COLOR_GREEN), 0);

        if (context->status_label != NULL) {
            lv_label_set_text(context->status_label, "Saving");
        }

        if (context->message_label != NULL) {
            lv_label_set_text(context->message_label, "Preparing recording.");
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
    lv_obj_set_width(label, STT_TEXT_WIDTH);
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

    lv_obj_set_size(panel, STT_TEXT_PANEL_WIDTH, STT_TEXT_PANEL_HEIGHT);
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
        STT_COLOR_GREEN_SOFT,
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

    lv_obj_set_size(button, STT_ACTION_BUTTON_WIDTH, STT_ACTION_BUTTON_HEIGHT);
    lv_obj_set_style_radius(button, STT_ACTION_BUTTON_HEIGHT / 2, 0);
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
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(label, STT_ACTION_BUTTON_WIDTH - 10);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
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
    const char *message = "Hold TALK to record.";
    uint32_t message_color = UI_COLOR_MUTED;

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

        if (result != NULL && result[0] != '\0') {
            message = result;
            message_color = UI_COLOR_TEXT;
        }
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
     * The STT page itself is fixed. Only the top text panel scrolls.
     */
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(body, LV_DIR_NONE);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *container = lv_obj_create(body);

    lv_obj_set_size(container, STT_CONTAINER_WIDTH, STT_CONTAINER_HEIGHT);
    style_plain_container(container);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        container,
        LV_FLEX_ALIGN_SPACE_BETWEEN,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );

    /*
     * Top: scrollable status/transcript panel.
     */
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

    /*
     * Middle: TALK button.
     */
    lv_obj_t *talk_holder = lv_obj_create(container);
    lv_obj_set_size(talk_holder, STT_CONTAINER_WIDTH, STT_TALK_AREA_HEIGHT);
    style_plain_container(talk_holder);

    lv_obj_t *talk_button = lv_button_create(talk_holder);

    lv_obj_set_size(talk_button, STT_TALK_BUTTON_SIZE, STT_TALK_BUTTON_SIZE);
    lv_obj_set_style_radius(talk_button, STT_TALK_BUTTON_SIZE / 2, 0);
    lv_obj_set_style_bg_color(talk_button, lv_color_hex(STT_COLOR_GREEN), 0);
    lv_obj_set_style_bg_opa(talk_button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(talk_button, lv_color_hex(STT_COLOR_GREEN_SOFT), 0);
    lv_obj_set_style_border_width(talk_button, 3, 0);
    lv_obj_set_style_shadow_width(talk_button, 9, 0);
    lv_obj_set_style_shadow_spread(talk_button, 1, 0);
    lv_obj_set_style_shadow_color(talk_button, lv_color_hex(STT_COLOR_GREEN_DARK), 0);
    lv_obj_set_style_shadow_opa(talk_button, LV_OPA_40, 0);
    lv_obj_set_style_pad_all(talk_button, 8, 0);
    lv_obj_set_scrollbar_mode(talk_button, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(talk_button, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_center(talk_button);

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

    /*
     * Bottom: compact action buttons.
     */
    lv_obj_t *action_row = lv_obj_create(container);
    lv_obj_set_size(action_row, STT_CONTAINER_WIDTH, STT_ACTION_ROW_HEIGHT);
    style_plain_container(action_row);
    lv_obj_set_flex_flow(action_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        action_row,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );
    lv_obj_set_style_pad_column(action_row, STT_ACTION_BUTTON_GAP, 0);

    lv_obj_t *test_button = create_small_button(
        action_row,
        speaker_test_active ? "Stop" : "Test",
        speaker_test_active ? STT_COLOR_GRAY : STT_COLOR_BLUE,
        speaker_test_active ? STT_COLOR_GRAY_DARK : STT_COLOR_BLUE_DARK,
        test_speaker_button_event_cb,
        callbacks
    );
    set_button_enabled(test_button, test_speaker_enabled);

    lv_obj_t *play_recording_button = create_small_button(
        action_row,
        recording_playback_active ? "Playing" : "Play",
        STT_COLOR_BLUE,
        STT_COLOR_BLUE_DARK,
        play_recording_button_event_cb,
        callbacks
    );
    set_button_enabled(play_recording_button, play_recording_enabled);
}
