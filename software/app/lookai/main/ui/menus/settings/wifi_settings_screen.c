/**
 * @file ui/menus/settings/wifi_settings_screen.c
 * @brief Wi-Fi settings menu body implementation.
 */

#include "wifi_settings_screen.h"

#include <stdio.h>

#include "ui_button.h"
#include "ui_card.h"
#include "ui_label.h"
#include "ui_theme.h"

void wifi_settings_screen_render(
    lv_obj_t *body,
    const ui_manager_state_t *state,
    const ui_manager_callbacks_t *callbacks,
    lv_event_cb_t manage_saved_cb,
    lv_event_cb_t portal_toggle_cb
)
{
    (void)callbacks;

    lv_obj_t *status_card = ui_card_create(body);
    ui_label_create(status_card, "Connection status", UI_COLOR_TEXT, UI_THEME_CARD_INNER_WIDTH);

    char status_line[96];
    snprintf(status_line, sizeof(status_line), "Status: %s", state->wifi_status);
    ui_label_create(status_card, status_line, UI_COLOR_MUTED, UI_THEME_CARD_INNER_WIDTH);

    char ssid_line[96];
    snprintf(
        ssid_line,
        sizeof(ssid_line),
        "SSID: %s",
        state->wifi_ssid[0] != '\0' ? state->wifi_ssid : "-"
    );
    ui_label_create(status_card, ssid_line, UI_COLOR_MUTED, UI_THEME_CARD_INNER_WIDTH);

    char ip_line[64];
    snprintf(
        ip_line,
        sizeof(ip_line),
        "IP: %s",
        state->wifi_ip[0] != '\0' ? state->wifi_ip : "-"
    );
    ui_label_create(status_card, ip_line, UI_COLOR_MUTED, UI_THEME_CARD_INNER_WIDTH);

    char saved_line[64];
    snprintf(saved_line, sizeof(saved_line), "Saved networks: %d", state->saved_count);
    ui_label_create(status_card, saved_line, UI_COLOR_DIM, UI_THEME_CARD_INNER_WIDTH);

    if (state->portal_active) {
        lv_obj_t *portal_card = ui_card_create(body);
        ui_label_create(portal_card, "Setup portal active", UI_COLOR_SUCCESS_TEXT, UI_THEME_CARD_INNER_WIDTH);
        ui_label_create(portal_card, "Wi-Fi: LookAI-Setup", UI_COLOR_MUTED, UI_THEME_CARD_INNER_WIDTH);
        ui_label_create(portal_card, "Password: 12345678", UI_COLOR_MUTED, UI_THEME_CARD_INNER_WIDTH);
        ui_label_create(portal_card, "IP: 192.168.4.1", UI_COLOR_MUTED, UI_THEME_CARD_INNER_WIDTH);
    }

    ui_button_create(
        body,
        "Manage saved networks",
        UI_THEME_BUTTON_WIDTH,
        UI_THEME_BUTTON_HEIGHT,
        UI_COLOR_SECONDARY,
        manage_saved_cb,
        NULL
    );

    ui_button_create(
        body,
        state->portal_active ? "Close captive portal" : "Connect another network",
        UI_THEME_BUTTON_WIDTH,
        UI_THEME_BUTTON_HEIGHT,
        state->portal_active ? UI_COLOR_DANGER : UI_COLOR_PRIMARY,
        portal_toggle_cb,
        NULL
    );
}
