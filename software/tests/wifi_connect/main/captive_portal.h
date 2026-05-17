#pragma once

#include "esp_err.h"

typedef void (*captive_portal_connect_cb_t)(const char *ssid, const char *password);

esp_err_t captive_portal_start(captive_portal_connect_cb_t connect_cb);
esp_err_t captive_portal_stop(void);
