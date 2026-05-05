/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp_board_extra.h"
#include "display_helper.h"
#include "wifi_helper.h"
#include "mqtt_helper.h"
#include "main_tab.h"
#include "wifi_tab.h"
#include "ha_switch.h"


static void create_ui(void);
static const char *TAG = "app";

void app_main(void)
{
    ESP_ERROR_CHECK(bsp_extra_codec_init());

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = 480 * 800,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = true,
        }
    };
    bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();

    bsp_display_lock(0);
    create_ui();
    bsp_display_unlock();

    ha_switch_init();

    ESP_LOGI(TAG, "Starting WiFi...");
    ESP_ERROR_CHECK(wifi_helper_connect_default());

    ESP_LOGI(TAG, "Connecting MQTT (if configured)...");
    // Wait a moment for network to stabilize
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_err_t mqtt_err = mqtt_helper_connect_saved();
    if (mqtt_err == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "MQTT config not found; skip connect");
    } else if (mqtt_err != ESP_OK) {
        ESP_LOGW(TAG, "MQTT connect skipped/failed: %s", esp_err_to_name(mqtt_err));
    }
}

static void create_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_t *root = lv_obj_create(scr);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(root, 0, 0);

    display_helper_create_status_bar(root, "System Status", "Ready");

    display_helper_tabs_t tabs;
    if (display_helper_create_tabs(root, &tabs)) {
        main_tab_init(tabs.main_tab);
        wifi_tab_init(tabs.wifi_tab);
    }
}
