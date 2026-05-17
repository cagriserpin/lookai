#pragma once

#include <stdbool.h>

#include "esp_err.h"

#define UI_STATUS_MAX_SAVED_NETWORKS 5

typedef void (*ui_status_action_cb_t)(void);
typedef void (*ui_status_ssid_action_cb_t)(const char *ssid);

typedef struct {
    ui_status_action_cb_t connect_another;
    ui_status_ssid_action_cb_t connect_saved;
    ui_status_ssid_action_cb_t forget_saved;
} ui_status_callbacks_t;

typedef struct {
    char ssid[33];
    bool connected;
} ui_status_saved_network_t;

esp_err_t ui_status_init(void);

void ui_status_set_callbacks(const ui_status_callbacks_t *callbacks);

void ui_status_show_settings(void);
void ui_status_show_wifi(void);

void ui_status_update_wifi_status(
    const char *status,
    const char *ssid,
    const char *ip,
    int saved_count,
    bool portal_active
);

void ui_status_set_saved_networks(
    const ui_status_saved_network_t *items,
    int count
);
