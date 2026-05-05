/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "device_config.h"

#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NVS_NS       "device_cfg"
#define NVS_KEY_ID   "id"
#define NVS_KEY_NAME "name"

static const char *TAG = "device_config";

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

void device_config_load(char *out_id, size_t id_len, char *out_name, size_t name_len)
{
    if (out_id && id_len > 0) {
        strlcpy(out_id, DEVICE_CONFIG_DEFAULT_ID, id_len);
    }
    if (out_name && name_len > 0) {
        strlcpy(out_name, DEVICE_CONFIG_DEFAULT_NAME, name_len);
    }

    if (init_nvs() != ESP_OK) return;

    nvs_handle_t nvs;
    if (nvs_open(NVS_NS, NVS_READONLY, &nvs) != ESP_OK) return;

    if (out_id && id_len > 0) {
        size_t len = id_len;
        nvs_get_str(nvs, NVS_KEY_ID, out_id, &len);
        if (out_id[0] == '\0') {
            strlcpy(out_id, DEVICE_CONFIG_DEFAULT_ID, id_len);
        }
    }
    if (out_name && name_len > 0) {
        size_t len = name_len;
        nvs_get_str(nvs, NVS_KEY_NAME, out_name, &len);
        if (out_name[0] == '\0') {
            strlcpy(out_name, DEVICE_CONFIG_DEFAULT_NAME, name_len);
        }
    }

    nvs_close(nvs);
}

esp_err_t device_config_save(const char *id, const char *name)
{
    if (!id || id[0] == '\0' || !name || name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = init_nvs();
    if (err != ESP_OK) return err;

    nvs_handle_t nvs;
    err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;

    err = nvs_set_str(nvs, NVS_KEY_ID, id);
    if (err == ESP_OK) err = nvs_set_str(nvs, NVS_KEY_NAME, name);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Save failed: %s", esp_err_to_name(err));
    }
    return err;
}
