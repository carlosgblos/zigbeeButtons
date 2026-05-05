/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stddef.h>

#include "esp_err.h"

#define DEVICE_CONFIG_ID_MAX       32
#define DEVICE_CONFIG_NAME_MAX     48

#define DEVICE_CONFIG_DEFAULT_ID   "espSwitch"
#define DEVICE_CONFIG_DEFAULT_NAME "ESP P4 Zigbee Switch"

void device_config_load(char *out_id, size_t id_len, char *out_name, size_t name_len);
esp_err_t device_config_save(const char *id, const char *name);
