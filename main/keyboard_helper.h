/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "lvgl.h"

/* Initialize keyboard helper (lazy overlay creation happens on first use). */
void keyboard_helper_init(void);

/* Attach keyboard overlay handler to a textarea (LV_EVENT_FOCUSED triggers overlay). */
void keyboard_helper_attach(lv_obj_t *textarea);
