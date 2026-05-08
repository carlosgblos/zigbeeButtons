/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>

#include "lvgl.h"

#define DISPLAY_HELPER_PROJECT_NAME    "SmartP4 Switch"
#define DISPLAY_HELPER_PROJECT_VERSION "1.0"
#define DISPLAY_HELPER_PROJECT_TITLE   DISPLAY_HELPER_PROJECT_NAME " v" DISPLAY_HELPER_PROJECT_VERSION

typedef struct {
    lv_obj_t *tabview;
    lv_obj_t *main_tab;
    lv_obj_t *wifi_tab;
    lv_obj_t *buttons_tab;
} display_helper_tabs_t;

void display_helper_create_status_bar(lv_obj_t *parent, const char *title_text, const char *detail_text);
void display_helper_set_status_bar_bg(lv_color_t color);
void display_helper_set_status_text(const char *title_text, const char *detail_text);
void display_helper_set_device_name(const char *device_name);
void display_helper_set_wifi_indicator(lv_color_t color);
void display_helper_set_mqtt_indicator(lv_color_t color);
void display_helper_add_action_button(lv_obj_t *parent);
bool display_helper_create_tabs(lv_obj_t *parent, display_helper_tabs_t *tabs_out);
void display_helper_enable_display_sleep(uint32_t timeout_ms);
