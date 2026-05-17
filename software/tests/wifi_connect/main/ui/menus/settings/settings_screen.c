/**
 * @file ui/menus/settings/settings_screen.c
 * @brief Root settings menu screen implementation.
 */

#include "settings_screen.h"

#include <stdio.h>

#include "ui_button.h"
#include "ui_card.h"
#include "ui_label.h"
#include "ui_screen.h"
#include "ui_theme.h"

void settings_screen_render(
    lv_obj_t *screen,
    const ui_manager_state_t *state,
    const ui_manager_callbacks_t *callbacks,
    lv_event_cb_t wifi_button_cb
)
{
    (void)callbacks;

    ui_screen_create_header(screen, "Settings", false, NULL);

    lv_obj_t *content = ui_screen_create_content(screen);

    lv_obj_t *card = ui_card_create(content);
    ui_label_create(card, "Wi-Fi", UI_COLOR_TEXT, UI_THEME_CARD_INNER_WIDTH);

    char status_line[96];
    snprintf(status_line, sizeof(status_line), "Status: %s", state->wifi_status);
    ui_label_create(card, status_line, UI_COLOR_MUTED, UI_THEME_CARD_INNER_WIDTH);

    if (state->wifi_ssid[0] != '\0') {
        char ssid_line[96];
        snprintf(ssid_line, sizeof(ssid_line), "SSID: %s", state->wifi_ssid);
        ui_label_create(card, ssid_line, UI_COLOR_DIM, UI_THEME_CARD_INNER_WIDTH);
    }

    if (state->portal_active) {
        ui_label_create(card, "Setup portal: LookAI-Setup", UI_COLOR_SUCCESS_TEXT, UI_THEME_CARD_INNER_WIDTH);
        ui_label_create(card, "Portal IP: 192.168.4.1", UI_COLOR_SUCCESS_TEXT, UI_THEME_CARD_INNER_WIDTH);
    }

    ui_button_create(
        content,
        "Open Wi-Fi settings",
        UI_THEME_BUTTON_WIDTH,
        UI_THEME_BUTTON_HEIGHT,
        UI_COLOR_PRIMARY,
        wifi_button_cb,
        NULL
    );
}
