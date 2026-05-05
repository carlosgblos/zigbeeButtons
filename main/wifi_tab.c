/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "wifi_tab.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "bsp/esp32_p4_function_ev_board.h"
#include "bsp/display.h"
#include "display_helper.h"
#include "wifi_helper.h"
#include "mqtt_helper.h"
#include "keyboard_helper.h"
#include "button_config.h"
#include "main_tab.h"
#include "ha_switch.h"
#include "device_config.h"

typedef struct {
    char ssid[33];
    char password[65];
} wifi_connect_request_t;

static lv_obj_t *s_dropdown;
static lv_obj_t *s_password_ta;
static lv_obj_t *s_device_id_ta;
static lv_obj_t *s_device_name_ta;
static lv_obj_t *s_mqtt_host_ta;
static lv_obj_t *s_mqtt_user_ta;
static lv_obj_t *s_mqtt_pass_ta;
static lv_obj_t *s_status_label;
static bool s_scan_in_progress;
static bool s_connect_in_progress;
static bool s_save_in_progress;

/* Button config UI */
static lv_obj_t *s_btn_count_label;
static uint8_t   s_edit_count = BTN_MAX_COUNT;
static lv_obj_t *s_btn_config_rows[BTN_MAX_COUNT];
static lv_obj_t *s_btn_name_ta[BTN_MAX_COUNT];
static lv_obj_t *s_btn_icon_dd[BTN_MAX_COUNT];
static lv_obj_t *s_btn_type_dd[BTN_MAX_COUNT];

static void set_status_label(const char *text);
static bool get_selected_ssid(char *ssid, size_t len);
static void scan_button_event_cb(lv_event_t *e);
static void connect_button_event_cb(lv_event_t *e);
static void disconnect_button_event_cb(lv_event_t *e);
static void dropdown_changed_event_cb(lv_event_t *e);
static void save_device_button_event_cb(lv_event_t *e);
static void save_button_event_cb(lv_event_t *e);
static void preload_saved_settings(void);
static esp_err_t maybe_save_device_config(void);
static esp_err_t maybe_save_mqtt_config(void);
static void wifi_tab_scan_task(void *param);
static void wifi_tab_connect_task(void *param);
static void count_dec_cb(lv_event_t *e);
static void count_inc_cb(lv_event_t *e);
static void save_buttons_cb(lv_event_t *e);
static void update_count_ui(void);
static void preload_button_config(void);

void wifi_tab_init(lv_obj_t *tab)
{
    if (!tab) {
        return;
    }

    keyboard_helper_init();

    lv_obj_set_size(tab, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(tab, 16, 0);
    lv_obj_set_style_pad_gap(tab, 12, 0);
    lv_obj_set_flex_grow(tab, 1);

    lv_obj_t *title = lv_label_create(tab);
    lv_label_set_text(title, "WiFi Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);

    /* Panel that fills the remaining tab area and contains the controls */
    lv_obj_t *panel = lv_obj_create(tab);
    lv_obj_remove_style_all(panel);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(panel, 12, 0);
    lv_obj_set_style_pad_gap(panel, 10, 0);
    lv_obj_set_width(panel, LV_PCT(100));
    lv_obj_set_flex_grow(panel, 1);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x071122), 0);

    /* Allow the panel to scroll vertically so inputs can be scrolled
     * into view when the keyboard appears. Show scrollbar when needed. */
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *device_section_title = lv_label_create(panel);
    lv_label_set_text(device_section_title, "Device Settings");
    lv_obj_set_style_text_font(device_section_title, &lv_font_montserrat_20, 0);

    s_device_id_ta = lv_textarea_create(panel);
    lv_textarea_set_one_line(s_device_id_ta, true);
    lv_textarea_set_placeholder_text(s_device_id_ta, "Device ID / MQTT root");
    lv_textarea_set_max_length(s_device_id_ta, DEVICE_CONFIG_ID_MAX - 1);
    lv_obj_set_width(s_device_id_ta, LV_PCT(100));
    keyboard_helper_attach(s_device_id_ta);

    s_device_name_ta = lv_textarea_create(panel);
    lv_textarea_set_one_line(s_device_name_ta, true);
    lv_textarea_set_placeholder_text(s_device_name_ta, "Device Name");
    lv_textarea_set_max_length(s_device_name_ta, DEVICE_CONFIG_NAME_MAX - 1);
    lv_obj_set_width(s_device_name_ta, LV_PCT(100));
    keyboard_helper_attach(s_device_name_ta);

    lv_obj_t *device_save_btn = lv_button_create(panel);
    lv_obj_add_event_cb(device_save_btn, save_device_button_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_width(device_save_btn, LV_PCT(100));
    lv_obj_t *device_save_label = lv_label_create(device_save_btn);
    lv_label_set_text(device_save_label, "Apply Device Settings");
    lv_obj_center(device_save_label);

    lv_obj_t *wifi_section_title = lv_label_create(panel);
    lv_label_set_text(wifi_section_title, "WiFi & MQTT Settings");
    lv_obj_set_style_text_font(wifi_section_title, &lv_font_montserrat_20, 0);

    lv_obj_t *button_row = lv_obj_create(panel);
    lv_obj_remove_style_all(button_row);
    lv_obj_set_flex_flow(button_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(button_row, 10, 0);
    lv_obj_set_width(button_row, LV_PCT(100));

    lv_obj_t *scan_btn = lv_button_create(button_row);
    lv_obj_add_event_cb(scan_btn, scan_button_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *scan_label = lv_label_create(scan_btn);
    lv_label_set_text(scan_label, "Scan");
    lv_obj_center(scan_label);

    lv_obj_t *disconnect_btn = lv_button_create(button_row);
    lv_obj_add_event_cb(disconnect_btn, disconnect_button_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *disconnect_label = lv_label_create(disconnect_btn);
    lv_label_set_text(disconnect_label, "Disconnect");
    lv_obj_center(disconnect_label);

    s_dropdown = lv_dropdown_create(panel);
    lv_dropdown_set_options(s_dropdown, "Select network");
    lv_obj_set_width(s_dropdown, LV_PCT(100));
    lv_obj_add_event_cb(s_dropdown, dropdown_changed_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_password_ta = lv_textarea_create(panel);
    lv_textarea_set_one_line(s_password_ta, true);
    lv_textarea_set_password_mode(s_password_ta, true);
    lv_textarea_set_placeholder_text(s_password_ta, "Password");
    lv_textarea_set_max_length(s_password_ta, 64);
    lv_obj_set_width(s_password_ta, LV_PCT(100));
    keyboard_helper_attach(s_password_ta);

    s_mqtt_host_ta = lv_textarea_create(panel);
    lv_textarea_set_one_line(s_mqtt_host_ta, true);
    lv_textarea_set_placeholder_text(s_mqtt_host_ta, "MQTT Broker IP / host");
    lv_textarea_set_max_length(s_mqtt_host_ta, 120);
    lv_obj_set_width(s_mqtt_host_ta, LV_PCT(100));
    keyboard_helper_attach(s_mqtt_host_ta);

    s_mqtt_user_ta = lv_textarea_create(panel);
    lv_textarea_set_one_line(s_mqtt_user_ta, true);
    lv_textarea_set_placeholder_text(s_mqtt_user_ta, "MQTT User");
    lv_textarea_set_max_length(s_mqtt_user_ta, 64);
    lv_obj_set_width(s_mqtt_user_ta, LV_PCT(100));
    keyboard_helper_attach(s_mqtt_user_ta);

    s_mqtt_pass_ta = lv_textarea_create(panel);
    lv_textarea_set_one_line(s_mqtt_pass_ta, true);
    lv_textarea_set_password_mode(s_mqtt_pass_ta, true);
    lv_textarea_set_placeholder_text(s_mqtt_pass_ta, "MQTT Password");
    lv_textarea_set_max_length(s_mqtt_pass_ta, 64);
    lv_obj_set_width(s_mqtt_pass_ta, LV_PCT(100));
    keyboard_helper_attach(s_mqtt_pass_ta);

    lv_obj_t *connect_btn = lv_button_create(panel);
    lv_obj_add_event_cb(connect_btn, connect_button_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_width(connect_btn, LV_PCT(100));
    lv_obj_t *connect_label = lv_label_create(connect_btn);
    lv_label_set_text(connect_label, "Connect");
    lv_obj_center(connect_label);

    lv_obj_t *save_btn = lv_button_create(panel);
    lv_obj_add_event_cb(save_btn, save_button_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_width(save_btn, LV_PCT(100));
    lv_obj_t *save_label = lv_label_create(save_btn);
    lv_label_set_text(save_label, "Save & Connect");
    lv_obj_center(save_label);

    s_status_label = lv_label_create(panel);
    lv_label_set_text(s_status_label, "Idle");
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x94A3B8), 0);

    /* ── Button Config section ── */
    lv_obj_t *divider = lv_obj_create(panel);
    lv_obj_remove_style_all(divider);
    lv_obj_set_size(divider, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0x334155), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);

    lv_obj_t *btn_section_title = lv_label_create(panel);
    lv_label_set_text(btn_section_title, "Button Config");
    lv_obj_set_style_text_font(btn_section_title, &lv_font_montserrat_20, 0);

    /* Count row: [Buttons:] [−] [N] [+] */
    lv_obj_t *count_row = lv_obj_create(panel);
    lv_obj_remove_style_all(count_row);
    lv_obj_set_flex_flow(count_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(count_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(count_row, 10, 0);
    lv_obj_set_size(count_row, LV_PCT(100), LV_SIZE_CONTENT);

    lv_obj_t *count_lbl = lv_label_create(count_row);
    lv_label_set_text(count_lbl, "Buttons:");

    lv_obj_t *dec_btn = lv_button_create(count_row);
    lv_obj_set_size(dec_btn, 44, 44);
    lv_obj_add_event_cb(dec_btn, count_dec_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *dec_lbl = lv_label_create(dec_btn);
    lv_label_set_text(dec_lbl, LV_SYMBOL_MINUS);
    lv_obj_center(dec_lbl);

    s_btn_count_label = lv_label_create(count_row);
    lv_obj_set_style_text_font(s_btn_count_label, &lv_font_montserrat_20, 0);
    lv_obj_set_width(s_btn_count_label, 32);
    lv_obj_set_style_text_align(s_btn_count_label, LV_TEXT_ALIGN_CENTER, 0);
    char count_buf[4];
    snprintf(count_buf, sizeof(count_buf), "%u", (unsigned)s_edit_count);
    lv_label_set_text(s_btn_count_label, count_buf);

    lv_obj_t *inc_btn = lv_button_create(count_row);
    lv_obj_set_size(inc_btn, 44, 44);
    lv_obj_add_event_cb(inc_btn, count_inc_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *inc_lbl = lv_label_create(inc_btn);
    lv_label_set_text(inc_lbl, LV_SYMBOL_PLUS);
    lv_obj_center(inc_lbl);

    /* Per-button rows: [index] [name textarea] [icon dropdown] */
    for (uint8_t i = 0; i < BTN_MAX_COUNT; i++) {
        lv_obj_t *row = lv_obj_create(panel);
        lv_obj_remove_style_all(row);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(row, 6, 0);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        s_btn_config_rows[i] = row;

        lv_obj_t *idx_lbl = lv_label_create(row);
        char idx_buf[4];
        snprintf(idx_buf, sizeof(idx_buf), "%u:", (unsigned)i + 1);
        lv_label_set_text(idx_lbl, idx_buf);
        lv_obj_set_width(idx_lbl, 28);

        lv_obj_t *name_ta = lv_textarea_create(row);
        lv_textarea_set_one_line(name_ta, true);
        lv_textarea_set_max_length(name_ta, BTN_NAME_MAX - 1);
        lv_obj_set_flex_grow(name_ta, 1);
        lv_obj_set_height(name_ta, 44);
        keyboard_helper_attach(name_ta);
        s_btn_name_ta[i] = name_ta;

        lv_obj_t *icon_dd = lv_dropdown_create(row);
        lv_dropdown_set_options(icon_dd, btn_config_icon_options_str());
        lv_obj_set_width(icon_dd, 130);
        s_btn_icon_dd[i] = icon_dd;

        lv_obj_t *type_dd = lv_dropdown_create(row);
        lv_dropdown_set_options(type_dd, "Switch\nScene");
        lv_obj_set_width(type_dd, 100);
        s_btn_type_dd[i] = type_dd;
    }

    lv_obj_t *apply_btn = lv_button_create(panel);
    lv_obj_add_event_cb(apply_btn, save_buttons_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_width(apply_btn, LV_PCT(100));
    lv_obj_t *apply_lbl = lv_label_create(apply_btn);
    lv_label_set_text(apply_lbl, "Apply Button Config");
    lv_obj_center(apply_lbl);

    set_status_label("Tap Scan to find networks");
    preload_saved_settings();
    preload_button_config();
}

static void set_status_label(const char *text)
{
    if (!s_status_label) {
        return;
    }
    if (!bsp_display_lock(1000)) {
        return;
    }
    lv_label_set_text(s_status_label, text ? text : "");
    bsp_display_unlock();
}

static bool get_selected_ssid(char *ssid, size_t len)
{
    if (!ssid || len == 0) {
        return false;
    }
    const char *text = lv_dropdown_get_text(s_dropdown);
    if (text) {
        strlcpy(ssid, text, len);
        return true;
    }
    lv_dropdown_get_selected_str(s_dropdown, ssid, len);
    return true;
}

static void scan_button_event_cb(lv_event_t *e)
{
    (void)e;
    if (s_scan_in_progress) {
        set_status_label("Scan already running...");
        return;
    }
    s_scan_in_progress = true;
    set_status_label("Scanning...");
    if (xTaskCreate(wifi_tab_scan_task, "wifi_scan_task", 6144, NULL, 5, NULL) != pdPASS) {
        s_scan_in_progress = false;
        set_status_label("Failed to start scan");
    }
}

static void connect_button_event_cb(lv_event_t *e)
{
    (void)e;
    if (s_connect_in_progress) {
        set_status_label("Connect already in progress...");
        return;
    }

    char ssid[33];
    get_selected_ssid(ssid, sizeof(ssid));
    const char *password = lv_textarea_get_text(s_password_ta);

    if (strlen(ssid) == 0 || strcmp(ssid, "Select network") == 0 || strcmp(ssid, "No networks found") == 0) {
        set_status_label("Please select a network");
        return;
    }

    maybe_save_device_config();
    maybe_save_mqtt_config();

    wifi_connect_request_t *req = calloc(1, sizeof(wifi_connect_request_t));
    if (!req) {
        set_status_label("Out of memory");
        return;
    }
    strlcpy(req->ssid, ssid, sizeof(req->ssid));
    strlcpy(req->password, password ? password : "", sizeof(req->password));

    s_connect_in_progress = true;
    set_status_label("Connecting...");
    if (xTaskCreate(wifi_tab_connect_task, "wifi_connect_task", 4096, req, 5, NULL) != pdPASS) {
        free(req);
        s_connect_in_progress = false;
        set_status_label("Failed to start connect task");
    }
}

static void save_button_event_cb(lv_event_t *e)
{
    (void)e;
    if (s_save_in_progress || s_connect_in_progress) {
        set_status_label("Operation already running...");
        return;
    }

    char ssid[33];
    get_selected_ssid(ssid, sizeof(ssid));
    const char *password = lv_textarea_get_text(s_password_ta);

    if (strlen(ssid) == 0 || strcmp(ssid, "Select network") == 0 || strcmp(ssid, "No networks found") == 0) {
        set_status_label("Please select a network");
        return;
    }

    s_save_in_progress = true;
    esp_err_t device_saved = maybe_save_device_config();
    esp_err_t mqtt_saved = maybe_save_mqtt_config();
    esp_err_t err = wifi_helper_save_credentials(ssid, password);
    if (err == ESP_OK) {
        const char *msg = "Saved. Connecting...";
        if (device_saved == ESP_OK && mqtt_saved == ESP_OK) {
            msg = "Saved device + WiFi + MQTT. Connecting...";
        } else if (mqtt_saved == ESP_OK) {
            msg = "Saved WiFi + MQTT. Connecting...";
        } else if (mqtt_saved != ESP_ERR_NOT_FOUND) {
            msg = "WiFi saved, MQTT save failed";
        } else if (device_saved != ESP_OK) {
            msg = "WiFi saved, device save failed";
        }
        set_status_label(msg);
        // Reuse connect task for async connect
        wifi_connect_request_t *req = calloc(1, sizeof(wifi_connect_request_t));
        if (req) {
            strlcpy(req->ssid, ssid, sizeof(req->ssid));
            strlcpy(req->password, password ? password : "", sizeof(req->password));
            s_connect_in_progress = true;
            if (xTaskCreate(wifi_tab_connect_task, "wifi_connect_task", 4096, req, 5, NULL) != pdPASS) {
                free(req);
                s_connect_in_progress = false;
                set_status_label("Failed to start connect task");
            }
        } else {
            set_status_label("Out of memory");
        }
    } else {
        set_status_label("Save failed");
    }
    s_save_in_progress = false;
}

static void save_device_button_event_cb(lv_event_t *e)
{
    (void)e;
    esp_err_t err = maybe_save_device_config();
    set_status_label(err == ESP_OK ? "Device settings saved" : "Device settings save failed");
}

static void disconnect_button_event_cb(lv_event_t *e)
{
    (void)e;
    esp_err_t err = wifi_helper_disconnect();
    // Also disconnect MQTT to prevent background reconnection attempts
    mqtt_helper_disconnect();
    set_status_label(err == ESP_OK ? "WiFi disconnected" : "Disconnect failed");
}

static void dropdown_changed_event_cb(lv_event_t *e)
{
    (void)e;
    char ssid[33];
    get_selected_ssid(ssid, sizeof(ssid));
    if (strlen(ssid) == 0 || strcmp(ssid, "Select network") == 0 || strcmp(ssid, "No networks found") == 0) {
        set_status_label("Ready");
        return;
    }
    char msg[64];
    snprintf(msg, sizeof(msg), "Selected: %s", ssid);
    set_status_label(msg);
}

static esp_err_t maybe_save_device_config(void)
{
    if (!s_device_id_ta || !s_device_name_ta) {
        return ESP_ERR_INVALID_STATE;
    }

    const char *id = lv_textarea_get_text(s_device_id_ta);
    const char *name = lv_textarea_get_text(s_device_name_ta);
    if (!id || strlen(id) == 0 || !name || strlen(name) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = device_config_save(id, name);
    if (err == ESP_OK) {
        display_helper_set_device_name(name);
        ha_switch_reload_config();
    }
    return err;
}

static esp_err_t maybe_save_mqtt_config(void)
{
    if (!s_mqtt_host_ta) {
        return ESP_ERR_INVALID_STATE;
    }

    const char *host = lv_textarea_get_text(s_mqtt_host_ta);
    if (!host || strlen(host) == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    const char *user = lv_textarea_get_text(s_mqtt_user_ta);
    const char *pass = lv_textarea_get_text(s_mqtt_pass_ta);
    esp_err_t err = mqtt_helper_save_config(host, user, pass);
    if (err != ESP_OK) {
        set_status_label("MQTT config save failed");
    }
    return err;
}

static void wifi_tab_scan_task(void *param)
{
    (void)param;
    const uint16_t max_aps = 20;
    wifi_ap_record_t *aps = calloc(max_aps, sizeof(wifi_ap_record_t));
    uint16_t count = max_aps;
    esp_err_t err = wifi_helper_scan(aps, &count);

    size_t buf_len = (size_t)(count + 1) * 40;
    char *options = calloc(buf_len, 1);

    if (!options) {
        set_status_label("Out of memory");
    } else if (err == ESP_OK && count > 0) {
        for (uint16_t i = 0; i < count; i++) {
            char line[40];
            snprintf(line, sizeof(line), "%s\n", (char *)aps[i].ssid);
            strlcat(options, line, buf_len);
        }
    } else if (options) {
        strlcpy(options, "No networks found", buf_len);
    }

    if (options && bsp_display_lock(2000)) {
        lv_dropdown_set_options(s_dropdown, options);
        bsp_display_unlock();
    }

    set_status_label(err == ESP_OK ? "Scan complete" : "Scan failed");

    free(options);
    free(aps);
    s_scan_in_progress = false;
    vTaskDelete(NULL);
}

static void wifi_tab_connect_task(void *param)
{
    wifi_connect_request_t *req = (wifi_connect_request_t *)param;
    if (!req) {
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = wifi_helper_connect_with_credentials(req->ssid, req->password);
    if (err == ESP_OK) {
        char host[128] = {0};
        bool have_mqtt = (mqtt_helper_get_saved_config(host, sizeof(host), NULL, 0, NULL, 0) == ESP_OK);
        if (have_mqtt) {
            set_status_label("WiFi connected. MQTT connecting...");
            // Wait a moment for network to stabilize after WiFi reconnection
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_err_t mqtt_err = mqtt_helper_connect_saved();
            if (mqtt_err != ESP_OK) {
                set_status_label("WiFi OK, MQTT failed");
            }
        } else {
            set_status_label("Connected");
        }
    } else {
        set_status_label("Connect failed");
    }

    free(req);
    s_connect_in_progress = false;
    vTaskDelete(NULL);
}

static void preload_saved_settings(void)
{
    char device_id[DEVICE_CONFIG_ID_MAX] = {0};
    char device_name[DEVICE_CONFIG_NAME_MAX] = {0};
    device_config_load(device_id, sizeof(device_id), device_name, sizeof(device_name));
    lv_textarea_set_text(s_device_id_ta, device_id);
    lv_textarea_set_text(s_device_name_ta, device_name);

    char ssid[33] = {0};
    char password[65] = {0};
    if (wifi_helper_get_saved_credentials(ssid, sizeof(ssid), password, sizeof(password)) == ESP_OK) {
        /* If the dropdown already contains the SSID as an option select it, otherwise
         * set the button text. lv_dropdown_set_text stores the pointer directly
         * so don't pass a stack buffer — allocate a persistent copy. */
        int32_t idx = lv_dropdown_get_option_index(s_dropdown, ssid);
        if (idx >= 0) {
            lv_dropdown_set_selected(s_dropdown, (uint32_t)idx);
        } else {
            size_t n = strlen(ssid) + 1;
            char *ssid_copy = lv_malloc(n);
            if (ssid_copy) {
                strncpy(ssid_copy, ssid, n);
                ssid_copy[n - 1] = '\0';
                lv_dropdown_set_text(s_dropdown, ssid_copy);
            }
        }
        lv_textarea_set_text(s_password_ta, password);
        set_status_label("Loaded saved WiFi");
    }

    char host[128] = {0};
    char user[65] = {0};
    char mqtt_pass[65] = {0};
    if (mqtt_helper_get_saved_config(host, sizeof(host), user, sizeof(user), mqtt_pass, sizeof(mqtt_pass)) == ESP_OK) {
        lv_textarea_set_text(s_mqtt_host_ta, host);
        lv_textarea_set_text(s_mqtt_user_ta, user);
        lv_textarea_set_text(s_mqtt_pass_ta, mqtt_pass);
    }
}

static void update_count_ui(void)
{
    if (s_btn_count_label) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%u", (unsigned)s_edit_count);
        lv_label_set_text(s_btn_count_label, buf);
    }
    for (uint8_t i = 0; i < BTN_MAX_COUNT; i++) {
        if (!s_btn_config_rows[i]) continue;
        if (i < s_edit_count) lv_obj_clear_flag(s_btn_config_rows[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_btn_config_rows[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void count_dec_cb(lv_event_t *e)
{
    (void)e;
    if (s_edit_count > 1) {
        s_edit_count--;
        update_count_ui();
    }
}

static void count_inc_cb(lv_event_t *e)
{
    (void)e;
    if (s_edit_count < BTN_MAX_COUNT) {
        s_edit_count++;
        update_count_ui();
    }
}

static void save_buttons_cb(lv_event_t *e)
{
    (void)e;
    btn_cfg_t cfg[BTN_MAX_COUNT];
    for (uint8_t i = 0; i < BTN_MAX_COUNT; i++) {
        const char *name = s_btn_name_ta[i] ? lv_textarea_get_text(s_btn_name_ta[i]) : "";
        strlcpy(cfg[i].name, name ? name : "", BTN_NAME_MAX);
        cfg[i].icon_idx = s_btn_icon_dd[i] ? (uint8_t)lv_dropdown_get_selected(s_btn_icon_dd[i]) : 0;
        cfg[i].type     = s_btn_type_dd[i] ? (uint8_t)lv_dropdown_get_selected(s_btn_type_dd[i]) : BTN_TYPE_SWITCH;
    }
    esp_err_t err = btn_config_save(s_edit_count, cfg);
    set_status_label(err == ESP_OK ? "Button config saved" : "Button save failed");
    if (err == ESP_OK) {
        main_tab_reload_config();
        ha_switch_reload_config();
    }
}

static void preload_button_config(void)
{
    uint8_t count;
    btn_cfg_t cfg[BTN_MAX_COUNT];
    btn_config_load(&count, cfg);
    s_edit_count = count;
    update_count_ui();
    for (uint8_t i = 0; i < BTN_MAX_COUNT; i++) {
        if (s_btn_name_ta[i]) lv_textarea_set_text(s_btn_name_ta[i], cfg[i].name);
        if (s_btn_icon_dd[i]) lv_dropdown_set_selected(s_btn_icon_dd[i], cfg[i].icon_idx);
        if (s_btn_type_dd[i]) lv_dropdown_set_selected(s_btn_type_dd[i], cfg[i].type);
    }
}
