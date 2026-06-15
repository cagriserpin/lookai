/**
 * @file ui/menus/settings/wifi_settings_screen.c
 * @brief Wi-Fi settings menu body implementation.
 */

#include "wifi_settings_screen.h"

#include <stdio.h>
#include <string.h>

#include "ui_button.h"
#include "ui_card.h"
#include "ui_label.h"
#include "ui_theme.h"

typedef struct {
    lv_obj_t *body;
    lv_obj_t *status_card;
    lv_obj_t *switch_obj;
    lv_obj_t *status_label;
    lv_obj_t *ssid_label;
    lv_obj_t *ip_label;
    lv_obj_t *saved_label;
    lv_obj_t *manage_button;
    lv_obj_t *portal_button;
    lv_obj_t *portal_label;
    bool portal_visible;
} wifi_settings_view_t;

static wifi_settings_view_t s_view = {0};

static void set_label_text_if_changed(lv_obj_t *label, const char *text)
{
    if (label == NULL) {
        return;
    }

    const char *safe_text = text != NULL ? text : "";
    const char *old_text = lv_label_get_text(label);
    if (old_text == NULL || strcmp(old_text, safe_text) != 0) {
        lv_label_set_text(label, safe_text);
    }
}

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

static void build_status_line(char *out, size_t out_size, const ui_manager_state_t *state)
{
    snprintf(
        out,
        out_size,
        "Status: %s",
        state != NULL && state->wifi_enabled ? state->wifi_status : "Disabled"
    );
}

static void build_ssid_line(char *out, size_t out_size, const ui_manager_state_t *state)
{
    snprintf(
        out,
        out_size,
        "SSID: %s",
        (state != NULL && state->wifi_enabled && state->wifi_ssid[0] != '\0') ? state->wifi_ssid : "-"
    );
}

static void build_ip_line(char *out, size_t out_size, const ui_manager_state_t *state)
{
    snprintf(
        out,
        out_size,
        "IP: %s",
        (state != NULL && state->wifi_enabled && state->wifi_ip[0] != '\0') ? state->wifi_ip : "-"
    );
}

static void build_saved_line(char *out, size_t out_size, const ui_manager_state_t *state)
{
    snprintf(out, out_size, "Saved networks: %d", state != NULL ? state->saved_count : 0);
}

static void style_switch(lv_obj_t *sw)
{
    lv_obj_set_size(sw, 64, 34);
    lv_obj_set_style_radius(sw, UI_THEME_PILL_RADIUS, 0);
    lv_obj_set_style_bg_color(sw, lv_color_hex(UI_COLOR_CARD_PRESSED), 0);
    lv_obj_set_style_bg_color(sw, lv_color_hex(UI_COLOR_SUCCESS), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, lv_color_hex(UI_COLOR_DIM), LV_PART_KNOB);
    lv_obj_set_style_bg_color(sw, lv_color_hex(UI_COLOR_TEXT), LV_PART_KNOB | LV_STATE_CHECKED);
    lv_obj_set_style_shadow_width(sw, 0, 0);
}

void wifi_settings_screen_render(
    lv_obj_t *body,
    const ui_manager_state_t *state,
    const ui_manager_callbacks_t *callbacks,
    lv_event_cb_t manage_saved_cb,
    lv_event_cb_t portal_toggle_cb,
    lv_event_cb_t wifi_enable_cb
)
{
    (void)callbacks;

    memset(&s_view, 0, sizeof(s_view));
    s_view.body = body;
    s_view.portal_visible = state != NULL && state->portal_active && state->wifi_enabled;

    lv_obj_t *status_card = ui_card_create(body);
    s_view.status_card = status_card;
    lv_obj_set_style_bg_color(status_card, lv_color_hex(UI_COLOR_CARD_ALT), 0);
    lv_obj_set_style_border_color(status_card, lv_color_hex(state->wifi_enabled ? UI_COLOR_WIFI_BLUE : UI_COLOR_BORDER_SOFT), 0);

    lv_obj_t *top = lv_obj_create(status_card);
    lv_obj_set_size(top, UI_THEME_CARD_INNER_WIDTH, 42);
    lv_obj_set_style_bg_opa(top, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_pad_all(top, 0, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(top);
    lv_label_set_text(title, "Wi-Fi");
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *sw = lv_switch_create(top);
    s_view.switch_obj = sw;
    style_switch(sw);
    if (state->wifi_enabled) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(sw, LV_STATE_CHECKED);
    }
    if (wifi_enable_cb != NULL) {
        lv_obj_add_event_cb(sw, wifi_enable_cb, LV_EVENT_VALUE_CHANGED, NULL);
    }
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);

    char status_line[96];
    snprintf(status_line, sizeof(status_line), "Status: %s", state->wifi_enabled ? state->wifi_status : "Disabled");
    s_view.status_label = ui_label_create(status_card, status_line, UI_COLOR_MUTED, UI_THEME_CARD_INNER_WIDTH);

    char ssid_line[96];
    snprintf(
        ssid_line,
        sizeof(ssid_line),
        "SSID: %s",
        (state->wifi_enabled && state->wifi_ssid[0] != '\0') ? state->wifi_ssid : "-"
    );
    s_view.ssid_label = ui_label_create(status_card, ssid_line, UI_COLOR_MUTED, UI_THEME_CARD_INNER_WIDTH);

    char ip_line[64];
    snprintf(
        ip_line,
        sizeof(ip_line),
        "IP: %s",
        (state->wifi_enabled && state->wifi_ip[0] != '\0') ? state->wifi_ip : "-"
    );
    s_view.ip_label = ui_label_create(status_card, ip_line, UI_COLOR_MUTED, UI_THEME_CARD_INNER_WIDTH);

    char saved_line[64];
    snprintf(saved_line, sizeof(saved_line), "Saved networks: %d", state->saved_count);
    s_view.saved_label = ui_label_create(status_card, saved_line, UI_COLOR_DIM, UI_THEME_CARD_INNER_WIDTH);

    if (state->portal_active && state->wifi_enabled) {
        lv_obj_t *portal_card = ui_card_create(body);
        lv_obj_set_style_bg_color(portal_card, lv_color_hex(UI_COLOR_CARD_ALT), 0);
        lv_obj_set_style_border_color(portal_card, lv_color_hex(UI_COLOR_SUCCESS), 0);
        ui_label_create(portal_card, "Setup portal active", UI_COLOR_SUCCESS_TEXT, UI_THEME_CARD_INNER_WIDTH);
        ui_label_create(portal_card, "Wi-Fi: LookAI-Setup", UI_COLOR_MUTED, UI_THEME_CARD_INNER_WIDTH);
        ui_label_create(portal_card, "Password: 12345678", UI_COLOR_MUTED, UI_THEME_CARD_INNER_WIDTH);
        ui_label_create(portal_card, "IP: 192.168.4.1", UI_COLOR_MUTED, UI_THEME_CARD_INNER_WIDTH);
    }

    lv_obj_t *manage = ui_button_create(
        body,
        "Manage saved networks",
        UI_THEME_BUTTON_WIDTH,
        UI_THEME_BUTTON_HEIGHT,
        UI_COLOR_SECONDARY,
        manage_saved_cb,
        NULL
    );

    s_view.manage_button = manage;

    lv_obj_t *portal = ui_button_create(
        body,
        state->portal_active ? "Close captive portal" : "Connect another network",
        UI_THEME_BUTTON_WIDTH,
        UI_THEME_BUTTON_HEIGHT,
        state->portal_active ? UI_COLOR_DANGER : UI_COLOR_PRIMARY,
        portal_toggle_cb,
        NULL
    );

    s_view.portal_button = portal;

    if (!state->wifi_enabled) {
        lv_obj_clear_flag(manage, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_opa(manage, LV_OPA_40, 0);
        lv_obj_clear_flag(portal, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_opa(portal, LV_OPA_40, 0);
    }
}


bool wifi_settings_screen_update(
    lv_obj_t *body,
    const ui_manager_state_t *state,
    const ui_manager_callbacks_t *callbacks
)
{
    (void)callbacks;

    if (
        body == NULL ||
        state == NULL ||
        s_view.body != body ||
        s_view.status_card == NULL ||
        s_view.switch_obj == NULL ||
        s_view.status_label == NULL ||
        s_view.ssid_label == NULL ||
        s_view.ip_label == NULL ||
        s_view.saved_label == NULL ||
        s_view.manage_button == NULL ||
        s_view.portal_button == NULL
    ) {
        return false;
    }

    bool portal_visible = state->portal_active && state->wifi_enabled;
    if (portal_visible != s_view.portal_visible) {
        return false;
    }

    if (state->wifi_enabled) {
        lv_obj_add_state(s_view.switch_obj, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(s_view.switch_obj, LV_STATE_CHECKED);
    }

    lv_obj_set_style_border_color(
        s_view.status_card,
        lv_color_hex(state->wifi_enabled ? UI_COLOR_WIFI_BLUE : UI_COLOR_BORDER_SOFT),
        0
    );

    char line[96];
    build_status_line(line, sizeof(line), state);
    set_label_text_if_changed(s_view.status_label, line);

    build_ssid_line(line, sizeof(line), state);
    set_label_text_if_changed(s_view.ssid_label, line);

    build_ip_line(line, sizeof(line), state);
    set_label_text_if_changed(s_view.ip_label, line);

    build_saved_line(line, sizeof(line), state);
    set_label_text_if_changed(s_view.saved_label, line);

    set_button_enabled(s_view.manage_button, state->wifi_enabled);
    set_button_enabled(s_view.portal_button, state->wifi_enabled);

    return true;
}
