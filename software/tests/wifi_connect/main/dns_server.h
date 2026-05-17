#pragma once

#include "esp_err.h"

esp_err_t dns_server_start(const char *redirect_ip);
esp_err_t dns_server_stop(void);
