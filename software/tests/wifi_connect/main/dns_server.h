/**
 * @file dns_server.h
 * @brief Small DNS redirect server used by captive portal mode.
 */

#pragma once

#include "esp_err.h"

/**
 * @brief Start a DNS server that redirects all DNS queries to one IP.
 *
 * @param redirect_ip IPv4 address string, usually "192.168.4.1".
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t dns_server_start(const char *redirect_ip);

/**
 * @brief Stop the DNS redirect server.
 *
 * @return ESP_OK.
 */
esp_err_t dns_server_stop(void);
