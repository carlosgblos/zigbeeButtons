/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#define BTN_MAX_COUNT   6
#define BTN_NAME_MAX    16
#define BTN_ICON_COUNT  21

#define BTN_TYPE_SWITCH 0
#define BTN_TYPE_SCENE  1

typedef struct {
    char    name[BTN_NAME_MAX];
    uint8_t icon_idx;
    uint8_t type;  /* BTN_TYPE_SWITCH or BTN_TYPE_SCENE */
} btn_cfg_t;

/* Load from NVS; fills safe defaults if nothing is saved */
void        btn_config_load(uint8_t *out_count, btn_cfg_t *out_buttons);
esp_err_t   btn_config_save(uint8_t count, const btn_cfg_t *buttons);

const char *btn_config_icon_symbol(uint8_t idx);
const char *btn_config_icon_name(uint8_t idx);
/* Newline-separated list of all icon names, suitable for lv_dropdown_set_options() */
const char *btn_config_icon_options_str(void);
