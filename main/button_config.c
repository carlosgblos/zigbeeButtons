/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "button_config.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NVS_NS "btn_cfg"

static const char *TAG = "btn_config";

static const char *s_symbols[BTN_ICON_COUNT] = {
    LV_SYMBOL_AUDIO,        /* Audio     */
    LV_SYMBOL_BATTERY_FULL, /* Battery   */
    LV_SYMBOL_BELL,         /* Bell      */
    LV_SYMBOL_BLUETOOTH,    /* Bluetooth */
    LV_SYMBOL_CHARGE,       /* Charge    */
    LV_SYMBOL_EYE_OPEN,     /* Eye       */
    LV_SYMBOL_REFRESH,      /* Fan       */
    LV_SYMBOL_GPS,          /* GPS       */
    LV_SYMBOL_HOME,         /* Home      */
    "\xEF\x83\xAB",         /* Light     (fa-lightbulb-o 0xF0EB) */
    LV_SYMBOL_LOOP,         /* Loop      */
    LV_SYMBOL_ENVELOPE,     /* Mail      */
    LV_SYMBOL_CALL,         /* Phone     */
    LV_SYMBOL_POWER,        /* Power     */
    LV_SYMBOL_VIDEO,        /* Projector */
    LV_SYMBOL_SETTINGS,     /* Settings  */
    LV_SYMBOL_VOLUME_MAX,   /* Speaker   */
    LV_SYMBOL_TRASH,        /* Trash     */
    LV_SYMBOL_WARNING,      /* Warning   */
    LV_SYMBOL_TINT,         /* Water     */
    LV_SYMBOL_WIFI,         /* WiFi      */
};

static const char *s_names[BTN_ICON_COUNT] = {
    "Audio", "Battery", "Bell", "Bluetooth", "Charge", "Eye",
    "Fan", "GPS", "Home", "Light", "Loop", "Mail",
    "Phone", "Power", "Projector", "Settings", "Speaker",
    "Trash", "Warning", "Water", "WiFi",
};

static esp_err_t init_nvs(void)
{
    static bool ready;
    if (ready) return ESP_OK;
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret == ESP_OK) ready = true;
    return ret;
}

const char *btn_config_icon_symbol(uint8_t idx)
{
    if (idx >= BTN_ICON_COUNT) idx = 0;
    return s_symbols[idx];
}

const char *btn_config_icon_name(uint8_t idx)
{
    if (idx >= BTN_ICON_COUNT) idx = 0;
    return s_names[idx];
}

const char *btn_config_icon_options_str(void)
{
    static char buf[256];
    static bool built;
    if (!built) {
        buf[0] = '\0';
        for (uint8_t i = 0; i < BTN_ICON_COUNT; i++) {
            if (i > 0) strlcat(buf, "\n", sizeof(buf));
            strlcat(buf, s_names[i], sizeof(buf));
        }
        built = true;
    }
    return buf;
}

void btn_config_load(uint8_t *out_count, btn_cfg_t *out_buttons)
{
    if (!out_count || !out_buttons) {
        return;
    }

    *out_count = BTN_MAX_COUNT;
    for (uint8_t i = 0; i < BTN_MAX_COUNT; i++) {
        snprintf(out_buttons[i].name, BTN_NAME_MAX, "BTN%u", (unsigned)i + 1);
        out_buttons[i].icon_idx = 0;
        out_buttons[i].type = BTN_TYPE_SWITCH;
    }

    if (init_nvs() != ESP_OK) return;

    nvs_handle_t nvs;
    if (nvs_open(NVS_NS, NVS_READONLY, &nvs) != ESP_OK) return;

    uint8_t count = BTN_MAX_COUNT;
    nvs_get_u8(nvs, "count", &count);
    if (count < 1 || count > BTN_MAX_COUNT) count = BTN_MAX_COUNT;
    *out_count = count;

    for (uint8_t i = 0; i < BTN_MAX_COUNT; i++) {
        char key_name[8], key_icon[8], key_type[8];
        snprintf(key_name, sizeof(key_name), "n%u", (unsigned)i);
        snprintf(key_icon, sizeof(key_icon), "i%u", (unsigned)i);
        snprintf(key_type, sizeof(key_type), "t%u", (unsigned)i);

        size_t len = BTN_NAME_MAX;
        nvs_get_str(nvs, key_name, out_buttons[i].name, &len);

        uint8_t icon = 0;
        nvs_get_u8(nvs, key_icon, &icon);
        if (icon >= BTN_ICON_COUNT) icon = 0;
        out_buttons[i].icon_idx = icon;

        uint8_t type = BTN_TYPE_SWITCH;
        nvs_get_u8(nvs, key_type, &type);
        if (type > BTN_TYPE_SCENE) type = BTN_TYPE_SWITCH;
        out_buttons[i].type = type;
    }
    nvs_close(nvs);
}

esp_err_t btn_config_save(uint8_t count, const btn_cfg_t *buttons)
{
    if (count < 1 || count > BTN_MAX_COUNT || !buttons) return ESP_ERR_INVALID_ARG;

    esp_err_t err = init_nvs();
    if (err != ESP_OK) return err;

    nvs_handle_t nvs;
    err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;

    err = nvs_set_u8(nvs, "count", count);
    for (uint8_t i = 0; i < BTN_MAX_COUNT && err == ESP_OK; i++) {
        char key_name[8], key_icon[8], key_type[8];
        snprintf(key_name, sizeof(key_name), "n%u", (unsigned)i);
        snprintf(key_icon, sizeof(key_icon), "i%u", (unsigned)i);
        snprintf(key_type, sizeof(key_type), "t%u", (unsigned)i);
        err = nvs_set_str(nvs, key_name, buttons[i].name);
        if (err == ESP_OK) err = nvs_set_u8(nvs, key_icon, buttons[i].icon_idx);
        if (err == ESP_OK) err = nvs_set_u8(nvs, key_type, buttons[i].type);
    }
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Save failed: %s", esp_err_to_name(err));
    }
    return err;
}
