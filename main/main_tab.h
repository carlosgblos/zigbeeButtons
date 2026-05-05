/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include "lvgl.h"

typedef void (*main_tab_button_cb_t)(uint32_t btn_id, bool on, void *user_data);

void main_tab_init(lv_obj_t *tab);

/* Rebuild grid from saved button_config (call after saving new config) */
void main_tab_reload_config(void);

/* btnm param is unused — kept so ha_switch.c compiles without changes */
void main_tab_button_set_on(lv_obj_t *btnm, uint32_t btn_id);
void main_tab_button_set_off(lv_obj_t *btnm, uint32_t btn_id);
void main_tab_button_toggle(lv_obj_t *btnm, uint32_t btn_id);
lv_obj_t *main_tab_get_btnm(void);

void main_tab_register_button_cb(main_tab_button_cb_t cb, void *user_data);
void main_tab_set_button_label(uint32_t btn_id, const char *text);
