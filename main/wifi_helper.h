/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "esp_wifi.h"

esp_err_t wifi_helper_connect_default(void);
esp_err_t wifi_helper_connect_with_credentials(const char *ssid, const char *password);
esp_err_t wifi_helper_disconnect(void);
esp_err_t wifi_helper_scan(wifi_ap_record_t *records, uint16_t *count);
bool wifi_helper_is_connected(void);
esp_err_t wifi_helper_save_credentials(const char *ssid, const char *password);
esp_err_t wifi_helper_get_saved_credentials(char *ssid, size_t ssid_len, char *password, size_t password_len);
