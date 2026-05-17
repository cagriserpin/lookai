/**
 * @file ui/menus/settings/wifi_settings_screen.c
 * @brief Wi-Fi settings menu screen implementation.
 */

#include "wifi_settings_screen.h"

#include <stdio.h>

#include "ui_button.h"
#include "ui_card.h"
#include "ui_label.h"
#include "ui_screen.h"
#include "ui_theme.h"

void wifi_settings_screen_render(
    lv_obj_t *screen,
    const ui_manager_state_t *state,
    const ui_manager_callbacks_t *callbacks,
    bool can_go_back,
    lv_event_cb_t back_cb,
    lv_event_cb_t manage_saved_cb,
    lv_event_cb_t portal_toggle_cb
)
{
    (void)callbacks;

    ui_screen_create_header(screen, "Wi-Fi", can_go_back, back_cb);

    lv_obj_t *content = ui_screen_create_content(screen);

    lv_obj_t *status_card = ui_card_create(content);
    ui_label_create(status_card, "Connection status", UI_COLOR_TEXT, 360);

    char status_line[96];
    snprintf(status_line, sizeof(status_line), "Status: %s", state->wifi_status);
    ui_label_create(status_card, status_line, UI_COLOR_MUTED, 360);

    char ssid_line[96];
    snprintf(
        ssid_line,
        sizeof(ssid_line),
        "SSID: %s",
        state->wifi_ssid[0] != '\0' ? state->wifi_ssid : "-"
    );
    ui_label_create(status_card, ssid_line, UI_COLOR_MUTED, 360);

    char ip_line[64];
    snprintf(
        ip_line,
        sizeof(ip_line),
        "IP: %s",
        state->wifi_ip[0] != '\0' ? state->wifi_ip : "-"
    );
    ui_label_create(status_card, ip_line, UI_COLOR_MUTED, 360);

    char saved_line[64];
    snprintf(saved_line, sizeof(saved_line), "Saved networks: %d", state->saved_count);
    ui_label_create(status_card, saved_line, UI_COLOR_DIM, 360);

    if (state->portal_active) {
        lv_obj_t *portal_card = ui_card_create(content);
        ui_label_create(portal_card, "Setup portal active", UI_COLOR_SUCCESS_TEXT, 360);
        ui_label_create(portal_card, "Wi-Fi: LookAI-Setup", UI_COLOR_MUTED, 360);
        ui_label_create(portal_card, "Password: 12345678", UI_COLOR_MUTED, 360);
        ui_label_create(portal_card, "IP: 192.168.4.1", UI_COLOR_MUTED, 360);
    }

    ui_button_create(
        content,
        "Manage saved networks",
        UI_THEME_BUTTON_WIDTH,
        56,
        UI_COLOR_SECONDARY,
        manage_saved_cb,
        NULL
    );

    ui_button_create(
        content,
        state->portal_active ? "Close captive portal" : "Connect another network",
        UI_THEME_BUTTON_WIDTH,
        56,
        state->portal_active ? UI_COLOR_DANGER : UI_COLOR_PRIMARY,
        portal_toggle_cb,
        NULL
    );
}
