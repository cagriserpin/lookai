/**
 * @file ui/menus/settings/tts/tts_screen.c
 * @brief Text-to-speech UI body implementation.
 */

#include "tts_screen.h"

#include <stdio.h>
#include <string.h>

#include "ui_card.h"
#include "ui_loading_dots.h"
#include "ui_theme.h"
#include "runtime_diag.h"

#define TTS_COLOR_PURPLE UI_COLOR_TTS_PURPLE
#define TTS_COLOR_PURPLE_DARK 0x6D28D9
#define TTS_CONTAINER_WIDTH UI_THEME_CARD_WIDTH
#define TTS_CONTAINER_HEIGHT 330
#define TTS_STATUS_WIDTH (UI_THEME_CARD_WIDTH - 34)
#define TTS_CARD_HEIGHT 76

typedef struct {
    lv_obj_t *body;
    lv_obj_t *status_label;
    lv_obj_t *sample_1;
    lv_obj_t *sample_2;
    lv_obj_t *loading_dots;
} tts_screen_view_t;

static tts_screen_view_t s_view = {0};

static void style_plain_container(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void set_card_enabled(lv_obj_t *card, bool enabled)
{
    if (card == NULL) {
        return;
    }

    bool currently_enabled = lv_obj_has_flag(card, LV_OBJ_FLAG_CLICKABLE);
    if (currently_enabled == enabled) {
        return;
    }

    if (enabled) {
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_opa(card, LV_OPA_COVER, 0);
    } else {
        lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_opa(card, LV_OPA_40, 0);
    }
}

static void sample_1_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    runtime_diag_log("button_tts_sample_1_clicked");

    const ui_manager_callbacks_t *callbacks =
        (const ui_manager_callbacks_t *)lv_event_get_user_data(event);

    if (callbacks != NULL && callbacks->tts_sample_1 != NULL) {
        callbacks->tts_sample_1();
    }
}

static void sample_2_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    runtime_diag_log("button_tts_sample_2_clicked");

    const ui_manager_callbacks_t *callbacks =
        (const ui_manager_callbacks_t *)lv_event_get_user_data(event);

    if (callbacks != NULL && callbacks->tts_sample_2 != NULL) {
        callbacks->tts_sample_2();
    }
}

static lv_obj_t *create_status_label(lv_obj_t *parent, const char *status, const char *result)
{
    lv_obj_t *label = lv_label_create(parent);

    char text[320];
    snprintf(
        text,
        sizeof(text),
        "%s\n%s",
        status != NULL && status[0] != '\0' ? status : "Ready",
        result != NULL && result[0] != '\0' ? result : "Select a sample text."
    );

    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, TTS_STATUS_WIDTH);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(label, 4, 0);

    return label;
}

static void update_status_label(lv_obj_t *label, const char *status, const char *result)
{
    if (label == NULL) {
        return;
    }

    char text[320];
    snprintf(
        text,
        sizeof(text),
        "%s\n%s",
        status != NULL && status[0] != '\0' ? status : "Ready",
        result != NULL && result[0] != '\0' ? result : "Select a sample text."
    );

    const char *old_text = lv_label_get_text(label);
    if (old_text == NULL || strcmp(old_text, text) != 0) {
        lv_label_set_text(label, text);
    }
}

static lv_obj_t *create_sample_card(
    lv_obj_t *parent,
    const char *text,
    lv_event_cb_t cb,
    const ui_manager_callbacks_t *callbacks
)
{
    lv_obj_t *card = ui_card_create(parent);

    lv_obj_set_height(card, TTS_CARD_HEIGHT);
    lv_obj_set_style_bg_color(card, lv_color_hex(UI_COLOR_CARD), 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(UI_COLOR_CARD_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(card, lv_color_hex(UI_COLOR_BORDER_SOFT), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(TTS_COLOR_PURPLE), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_flex_align(
        card,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);

    if (cb != NULL) {
        lv_obj_add_event_cb(card, cb, LV_EVENT_CLICKED, (void *)callbacks);
    }

    lv_obj_t *label = lv_label_create(card);
    char label_text[160];
    snprintf(label_text, sizeof(label_text), "%s " LV_SYMBOL_PLAY, text != NULL ? text : "");
    lv_label_set_text(label, label_text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, UI_THEME_CARD_INNER_WIDTH);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

    return card;
}

bool tts_screen_update(
    lv_obj_t *body,
    const ui_manager_state_t *state,
    const ui_manager_callbacks_t *callbacks
)
{
    (void)callbacks;

    if (
        body == NULL ||
        s_view.body != body ||
        s_view.status_label == NULL ||
        s_view.sample_1 == NULL ||
        s_view.sample_2 == NULL
    ) {
        return false;
    }

    const char *status = "Ready";
    const char *result = "Select a sample text.";
    bool busy = false;
    bool wifi_connected = true;

    if (state != NULL) {
        status = state->tts_status[0] != '\0' ? state->tts_status : "Ready";
        result = state->tts_result;
        busy = state->tts_busy;
        wifi_connected = state->wifi_connected;
    }

    if (!wifi_connected) {
        result = "Connect Wi-Fi to generate speech.";
    }

    update_status_label(s_view.status_label, status, result);
    ui_loading_dots_set_active(s_view.loading_dots, wifi_connected && busy);
    set_card_enabled(s_view.sample_1, wifi_connected && !busy);
    set_card_enabled(s_view.sample_2, wifi_connected && !busy);

    return true;
}

void tts_screen_render(
    lv_obj_t *body,
    const ui_manager_state_t *state,
    const ui_manager_callbacks_t *callbacks
)
{
    const char *status = "Ready";
    const char *result = "Select a sample text.";
    bool busy = false;
    bool wifi_connected = true;

    if (state != NULL) {
        status = state->tts_status[0] != '\0' ? state->tts_status : "Ready";
        result = state->tts_result;
        busy = state->tts_busy;
        wifi_connected = state->wifi_connected;
    }

    if (!wifi_connected) {
        result = "Connect Wi-Fi to generate speech.";
    }

    /*
     * TTS page is fixed. It has status text and two sample cards.
     */
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(body, LV_DIR_NONE);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *container = lv_obj_create(body);

    lv_obj_set_size(container, TTS_CONTAINER_WIDTH, TTS_CONTAINER_HEIGHT);
    style_plain_container(container);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        container,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );
    lv_obj_set_style_pad_gap(container, 18, 0);

    lv_obj_t *status_label = create_status_label(container, status, result);
    lv_obj_t *loading_dots = ui_loading_dots_create(container, TTS_COLOR_PURPLE);
    ui_loading_dots_set_active(loading_dots, wifi_connected && busy);

    lv_obj_t *sample_1 = create_sample_card(
        container,
        "Merhaba! Ben LookAI",
        sample_1_event_cb,
        callbacks
    );

    lv_obj_t *sample_2 = create_sample_card(
        container,
        "Sana nasil yardimci olabilirim?",
        sample_2_event_cb,
        callbacks
    );

    set_card_enabled(sample_1, wifi_connected && !busy);
    set_card_enabled(sample_2, wifi_connected && !busy);

    s_view.body = body;
    s_view.status_label = status_label;
    s_view.sample_1 = sample_1;
    s_view.sample_2 = sample_2;
    s_view.loading_dots = loading_dots;
}
