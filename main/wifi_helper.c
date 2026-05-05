/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "wifi_helper.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lvgl.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "bsp/esp32_p4_function_ev_board.h"
#include "bsp/display.h"
#include "display_helper.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#define WIFI_COLOR_CONNECTED     lv_color_hex(0x3B82F6)  // Blue
#define WIFI_COLOR_DISCONNECTED  lv_color_hex(0xEF4444)  // Red

#define WIFI_HELPER_DEFAULT_SSID CONFIG_ESP_WIFI_SSID
#define WIFI_HELPER_DEFAULT_PASS CONFIG_ESP_WIFI_PASSWORD
#define WIFI_HELPER_MAX_RETRY    CONFIG_ESP_MAXIMUM_RETRY

#if CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define WIFI_HELPER_H2E_IDENTIFIER ""
#elif CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define WIFI_HELPER_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define WIFI_HELPER_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif

#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

static const char *TAG = "wifi_helper";
static const char *WIFI_NVS_NAMESPACE = "wifi_cfg";
static const char *WIFI_NVS_KEY_SSID = "ssid";
static const char *WIFI_NVS_KEY_PASS = "pass";

static EventGroupHandle_t s_wifi_event_group;
static esp_event_handler_instance_t s_any_id_instance;
static esp_event_handler_instance_t s_got_ip_instance;
static esp_netif_t *s_netif;
static int s_retry_num;
static bool s_connected;
static bool s_should_reconnect = true;
static bool s_wifi_initialized;
static char s_active_ssid[33];
static char s_saved_ssid[33];
static char s_saved_pass[65];
static bool s_saved_loaded;

static void wifi_helper_update_ui(bool connected, const char *detail_text);
static esp_err_t wifi_helper_init(void);
static esp_err_t wifi_helper_connect_internal(const char *ssid, const char *password);
static esp_err_t wifi_helper_load_saved_credentials(void);

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (s_should_reconnect) {
            esp_wifi_connect();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        wifi_helper_update_ui(false, "WiFi disconnected");

        if (s_should_reconnect && s_retry_num < WIFI_HELPER_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        char ip[16];
        esp_ip4addr_ntoa(&event->ip_info.ip, ip, sizeof(ip));
        ESP_LOGI(TAG, "got ip:%s", ip);
        s_retry_num = 0;
        s_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        char detail[80];
        if (strlen(s_active_ssid) > 0) {
            snprintf(detail, sizeof(detail), "WiFi %s (%s)", s_active_ssid, ip);
        } else {
            snprintf(detail, sizeof(detail), "WiFi connected (%s)", ip);
        }
        wifi_helper_update_ui(true, detail);
    }
}

static esp_err_t wifi_helper_init(void)
{
    printf("hello world\n");

    if (s_wifi_initialized) {
        return ESP_OK;
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to init NVS");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Failed to init netif");

    esp_err_t loop_ret = esp_event_loop_create_default();
    if (loop_ret != ESP_OK && loop_ret != ESP_ERR_INVALID_STATE) {
        return loop_ret;
    }

    if (s_netif == NULL) {
        s_netif = esp_netif_create_default_wifi_sta();
    }
    ESP_RETURN_ON_FALSE(s_netif != NULL, ESP_FAIL, TAG, "Failed to create default wifi STA");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "Failed to init WiFi");

    if (CONFIG_LOG_MAXIMUM_LEVEL > CONFIG_LOG_DEFAULT_LEVEL) {
        esp_log_level_set("wifi", CONFIG_LOG_MAXIMUM_LEVEL);
    }

    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            &wifi_event_handler,
                                                            NULL,
                                                            &s_any_id_instance),
                        TAG, "Failed to register wifi event handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT,
                                                            IP_EVENT_STA_GOT_IP,
                                                            &wifi_event_handler,
                                                            NULL,
                                                            &s_got_ip_instance),
                        TAG, "Failed to register IP event handler");

    if (s_wifi_event_group == NULL) {
        s_wifi_event_group = xEventGroupCreate();
    }

    s_wifi_initialized = true;
    wifi_helper_load_saved_credentials();
    return ESP_OK;
}

static esp_err_t wifi_helper_connect_internal(const char *ssid, const char *password)
{
    ESP_RETURN_ON_FALSE(ssid && strlen(ssid) > 0, ESP_ERR_INVALID_ARG, TAG, "SSID missing");
    ESP_RETURN_ON_ERROR(wifi_helper_init(), TAG, "wifi_helper_init failed");
    wifi_helper_load_saved_credentials();

    s_should_reconnect = true;
    s_retry_num = 0;
    strlcpy(s_active_ssid, ssid, sizeof(s_active_ssid));
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    wifi_config_t wifi_config = { 0 };
    strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, password ? password : "", sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = (strlen(password) == 0) ? WIFI_AUTH_OPEN : ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD;
    wifi_config.sta.sae_pwe_h2e = ESP_WIFI_SAE_MODE;
    strlcpy((char *)wifi_config.sta.sae_h2e_identifier,
            WIFI_HELPER_H2E_IDENTIFIER,
            sizeof(wifi_config.sta.sae_h2e_identifier));

    esp_wifi_disconnect();
    esp_wifi_stop();

    ESP_LOGI(TAG, "Connecting to SSID:%s", ssid);
    char detail[64];
    snprintf(detail, sizeof(detail), "WiFi connecting (%s)", ssid);
    wifi_helper_update_ui(false, detail);

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Failed to set WiFi mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "Failed to set WiFi config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Failed to start WiFi");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s", ssid);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Failed to connect to SSID:%s", ssid);
    wifi_helper_update_ui(false, "WiFi connection failed");
    return ESP_FAIL;
}

esp_err_t wifi_helper_connect_default(void)
{
    if (strlen(s_saved_ssid) > 0) {
        return wifi_helper_connect_internal(s_saved_ssid, s_saved_pass);
    }
    return wifi_helper_connect_internal(WIFI_HELPER_DEFAULT_SSID, WIFI_HELPER_DEFAULT_PASS);
}

esp_err_t wifi_helper_connect_with_credentials(const char *ssid, const char *password)
{
    return wifi_helper_connect_internal(ssid, password);
}

esp_err_t wifi_helper_disconnect(void)
{
    s_should_reconnect = false;
    s_retry_num = 0;
    s_active_ssid[0] = '\0';

    if (!s_wifi_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED && err != ESP_ERR_WIFI_NOT_INIT) {
        return err;
    }

    err = esp_wifi_stop();
    if (err == ESP_ERR_WIFI_NOT_INIT || err == ESP_ERR_WIFI_STOP_STATE) {
        // already stopped
    } else if (err != ESP_OK) {
        return err;
    }

    s_connected = false;
    wifi_helper_update_ui(false, "WiFi stopped");
    return ESP_OK;
}

esp_err_t wifi_helper_scan(wifi_ap_record_t *records, uint16_t *count)
{
    ESP_RETURN_ON_FALSE(records && count, ESP_ERR_INVALID_ARG, TAG, "Invalid args");
    ESP_RETURN_ON_ERROR(wifi_helper_init(), TAG, "wifi_helper_init failed");

    // Ensure STA mode is set and WiFi is running for scan
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Failed to set WiFi mode");
    esp_err_t start_err = esp_wifi_start();
    if (start_err != ESP_OK && start_err != ESP_ERR_WIFI_CONN && start_err != ESP_ERR_WIFI_NOT_INIT) {
        return start_err;
    }

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = {
                .min = 100,
                .max = 300,
            },
        },
    };

    ESP_RETURN_ON_ERROR(esp_wifi_scan_start(&scan_config, true), TAG, "Scan start failed");

    uint16_t ap_num = 0;
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_num(&ap_num), TAG, "Failed to get AP count");

    if (*count > ap_num) {
        *count = ap_num;
    }

    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_records(count, records), TAG, "Failed to get AP records");
    return ESP_OK;
}

bool wifi_helper_is_connected(void)
{
    return s_connected;
}

static esp_err_t wifi_helper_load_saved_credentials(void)
{
    if (s_saved_loaded) {
        return ESP_OK;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    size_t ssid_len = sizeof(s_saved_ssid);
    size_t pass_len = sizeof(s_saved_pass);
    err = nvs_get_str(nvs, WIFI_NVS_KEY_SSID, s_saved_ssid, &ssid_len);
    if (err == ESP_OK) {
        err = nvs_get_str(nvs, WIFI_NVS_KEY_PASS, s_saved_pass, &pass_len);
    }
    nvs_close(nvs);

    if (err == ESP_OK) {
        s_saved_loaded = true;
    } else {
        s_saved_ssid[0] = '\0';
        s_saved_pass[0] = '\0';
    }
    return err;
}

esp_err_t wifi_helper_save_credentials(const char *ssid, const char *password)
{
    ESP_RETURN_ON_FALSE(ssid && strlen(ssid) > 0, ESP_ERR_INVALID_ARG, TAG, "SSID missing");
    ESP_RETURN_ON_ERROR(wifi_helper_init(), TAG, "wifi_helper_init failed");

    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs), TAG, "Failed to open NVS");
    esp_err_t err = nvs_set_str(nvs, WIFI_NVS_KEY_SSID, ssid);
    if (err != ESP_OK) {
        nvs_close(nvs);
        return err;
    }
    err = nvs_set_str(nvs, WIFI_NVS_KEY_PASS, password ? password : "");
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);

    if (err == ESP_OK) {
        strlcpy(s_saved_ssid, ssid, sizeof(s_saved_ssid));
        strlcpy(s_saved_pass, password ? password : "", sizeof(s_saved_pass));
        s_saved_loaded = true;
    }
    return err;
}

esp_err_t wifi_helper_get_saved_credentials(char *ssid, size_t ssid_len, char *password, size_t password_len)
{
    ESP_RETURN_ON_ERROR(wifi_helper_init(), TAG, "wifi_helper_init failed");
    wifi_helper_load_saved_credentials();
    ESP_RETURN_ON_FALSE(strlen(s_saved_ssid) > 0, ESP_ERR_NOT_FOUND, TAG, "No saved credentials");

    if (ssid && ssid_len > 0) {
        strlcpy(ssid, s_saved_ssid, ssid_len);
    }
    if (password && password_len > 0) {
        strlcpy(password, s_saved_pass, password_len);
    }
    return ESP_OK;
}

static void wifi_helper_update_ui(bool connected, const char *detail_text)
{
    if (!bsp_display_lock(1000)) {
        return;
    }

    display_helper_set_wifi_indicator(connected ? WIFI_COLOR_CONNECTED : WIFI_COLOR_DISCONNECTED);
    if (detail_text) {
        display_helper_set_status_text("System Status", detail_text);
    }

    bsp_display_unlock();
}

int main(void)
{
    printf("Hello World!\n");
    wifi_helper_init();
    // Add your code here
}
