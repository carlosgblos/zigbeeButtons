/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "mqtt_helper.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "esp_system.h"
#include "esp_random.h"
#include "esp_mac.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "display_helper.h"

#define MQTT_COLOR_CONNECTED     lv_color_hex(0x3B82F6)  // Blue (match WiFi)
#define MQTT_COLOR_DISCONNECTED  lv_color_hex(0xEF4444)  // Red

static const char *TAG = "mqtt_helper";
static const char *MQTT_NVS_NAMESPACE = "mqtt_cfg";
static const char *MQTT_NVS_KEY_HOST = "host";
static const char *MQTT_NVS_KEY_USER = "user";
static const char *MQTT_NVS_KEY_PASS = "pass";

static esp_mqtt_client_handle_t s_client;
static bool s_connected;
static bool s_saved_loaded;
static char s_saved_host[128];
static char s_saved_user[65];
static char s_saved_pass[65];
static char s_active_uri[128];
static char s_client_id[48];

static mqtt_helper_msg_cb_t s_msg_cb;
static void *s_msg_cb_user;
static mqtt_helper_conn_cb_t s_conn_cb;
static void *s_conn_cb_user;

static bool s_will_set;
static char s_will_topic[128];
static char s_will_payload[128];
static int s_will_qos;
static bool s_will_retain;

static void mqtt_helper_update_ui(bool connected, const char *detail_text);
static esp_err_t mqtt_helper_init_nvs(void);
static esp_err_t mqtt_helper_normalize_uri(const char *host_or_uri, char *out, size_t out_len);
static esp_err_t mqtt_helper_load_saved_config(void);
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static const char *mqtt_helper_generate_client_id(void);
static void mqtt_helper_dispatch_msg(const char *topic, const char *payload, int payload_len);

static esp_err_t mqtt_helper_init_nvs(void)
{
    static bool nvs_ready;
    if (nvs_ready) {
        return ESP_OK;
    }
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to init NVS");
    nvs_ready = true;
    return ESP_OK;
}

static esp_err_t mqtt_helper_normalize_uri(const char *host_or_uri, char *out, size_t out_len)
{
    ESP_RETURN_ON_FALSE(host_or_uri && out && out_len > 0, ESP_ERR_INVALID_ARG, TAG, "Bad args");

    if (strstr(host_or_uri, "://") != NULL) {
        strlcpy(out, host_or_uri, out_len);
        return ESP_OK;
    }

    int written = snprintf(out, out_len, "mqtt://%s", host_or_uri);
    return (written > 0 && (size_t)written < out_len) ? ESP_OK : ESP_ERR_NO_MEM;
}

static void mqtt_helper_update_ui(bool connected, const char *detail_text)
{
    if (!bsp_display_lock(1000)) {
        return;
    }
    display_helper_set_mqtt_indicator(connected ? MQTT_COLOR_CONNECTED : MQTT_COLOR_DISCONNECTED);
    if (detail_text) {
        display_helper_set_status_text("System Status", detail_text);
    }
    bsp_display_unlock();
}

static void mqtt_helper_dispatch_msg(const char *topic, const char *payload, int payload_len)
{
    if (s_msg_cb) {
        s_msg_cb(topic, payload, payload_len, s_msg_cb_user);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        mqtt_helper_update_ui(true, "MQTT connected");
        ESP_LOGI(TAG, "MQTT connected");
        if (s_conn_cb) {
            s_conn_cb(true, s_conn_cb_user);
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        mqtt_helper_update_ui(false, "MQTT disconnected");
        ESP_LOGI(TAG, "MQTT disconnected");
        if (s_conn_cb) {
            s_conn_cb(false, s_conn_cb_user);
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error event");
        break;
    case MQTT_EVENT_DATA: {
        esp_mqtt_event_handle_t ev = (esp_mqtt_event_handle_t)event_data;
        if (ev && ev->topic && ev->topic_len > 0) {
            char topic[128];
            size_t tlen = (ev->topic_len < sizeof(topic) - 1) ? ev->topic_len : sizeof(topic) - 1;
            memcpy(topic, ev->topic, tlen);
            topic[tlen] = '\0';
            mqtt_helper_dispatch_msg(topic, ev->data, ev->data_len);
        }
        break;
    }
    default:
        break;
    }
}

esp_err_t mqtt_helper_save_config(const char *host, const char *username, const char *password)
{
    ESP_RETURN_ON_ERROR(mqtt_helper_init_nvs(), TAG, "NVS init failed");
    ESP_RETURN_ON_FALSE(host && strlen(host) > 0, ESP_ERR_INVALID_ARG, TAG, "Host missing");

    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(MQTT_NVS_NAMESPACE, NVS_READWRITE, &nvs), TAG, "Failed to open NVS");

    char uri[sizeof(s_saved_host)];
    esp_err_t ret = mqtt_helper_normalize_uri(host, uri, sizeof(uri));
    if (ret != ESP_OK) {
        nvs_close(nvs);
        return ret;
    }

    esp_err_t err = nvs_set_str(nvs, MQTT_NVS_KEY_HOST, uri);
    if (err != ESP_OK) {
        goto err;
    }
    err = nvs_set_str(nvs, MQTT_NVS_KEY_USER, username ? username : "");
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, MQTT_NVS_KEY_PASS, password ? password : "");
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

err:
    nvs_close(nvs);
    if (err == ESP_OK) {
        strlcpy(s_saved_host, uri, sizeof(s_saved_host));
        strlcpy(s_saved_user, username ? username : "", sizeof(s_saved_user));
        strlcpy(s_saved_pass, password ? password : "", sizeof(s_saved_pass));
        s_saved_loaded = true;
    }
    return err;
}

static esp_err_t mqtt_helper_load_saved_config(void)
{
    if (s_saved_loaded) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(mqtt_helper_init_nvs(), TAG, "NVS init failed");

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(MQTT_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    size_t host_len = sizeof(s_saved_host);
    size_t user_len = sizeof(s_saved_user);
    size_t pass_len = sizeof(s_saved_pass);
    err = nvs_get_str(nvs, MQTT_NVS_KEY_HOST, s_saved_host, &host_len);
    if (err == ESP_OK) {
        esp_err_t user_err = nvs_get_str(nvs, MQTT_NVS_KEY_USER, s_saved_user, &user_len);
        if (user_err == ESP_ERR_NVS_NOT_FOUND) {
            s_saved_user[0] = '\0'; /* Backward compatibility */
            user_err = ESP_OK;
        }
        err = user_err;
    }
    if (err == ESP_OK) {
        err = nvs_get_str(nvs, MQTT_NVS_KEY_PASS, s_saved_pass, &pass_len);
    }
    nvs_close(nvs);

    if (err == ESP_OK) {
        s_saved_loaded = true;
    } else {
        s_saved_host[0] = '\0';
        s_saved_user[0] = '\0';
        s_saved_pass[0] = '\0';
    }
    return err;
}

esp_err_t mqtt_helper_get_saved_config(char *host, size_t host_len, char *username, size_t user_len,
                                       char *password, size_t password_len)
{
    esp_err_t err = mqtt_helper_load_saved_config();
    if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }
    if (err != ESP_OK) {
        return err;
    }
    ESP_RETURN_ON_FALSE(strlen(s_saved_host) > 0, ESP_ERR_NOT_FOUND, TAG, "No MQTT saved config");

    if (host && host_len > 0) {
        strlcpy(host, s_saved_host, host_len);
    }
    if (username && user_len > 0) {
        strlcpy(username, s_saved_user, user_len);
    }
    if (password && password_len > 0) {
        strlcpy(password, s_saved_pass, password_len);
    }
    return ESP_OK;
}

esp_err_t mqtt_helper_set_will(const char *topic, const char *payload, int qos, bool retain)
{
    if (!topic) {
        s_will_set = false;
        s_will_topic[0] = '\0';
        s_will_payload[0] = '\0';
        s_will_qos = 0;
        s_will_retain = false;
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(strlen(topic) > 0 && qos >= 0 && qos <= 2, ESP_ERR_INVALID_ARG, TAG, "Invalid will");
    strlcpy(s_will_topic, topic, sizeof(s_will_topic));
    strlcpy(s_will_payload, payload ? payload : "", sizeof(s_will_payload));
    s_will_qos = qos;
    s_will_retain = retain;
    s_will_set = true;
    return ESP_OK;
}

static const char *mqtt_helper_generate_client_id(void)
{

    uint8_t mac[6];
    if (esp_efuse_mac_get_default(mac) == ESP_OK) {
        snprintf(s_client_id, sizeof(s_client_id), "esp-p4-%02x%02x%02x%02x%02x%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        uint32_t r = (uint32_t)esp_random();
        snprintf(s_client_id, sizeof(s_client_id), "esp-p4-%08x", (unsigned int)r);
    }
    s_client_id[sizeof(s_client_id) - 1] = '\0';
    return s_client_id;
}

esp_err_t mqtt_helper_connect(const char *host, const char *username, const char *password)
{
    ESP_RETURN_ON_FALSE(host && strlen(host) > 0, ESP_ERR_INVALID_ARG, TAG, "Host missing");
    ESP_RETURN_ON_ERROR(mqtt_helper_init_nvs(), TAG, "NVS init failed");

    ESP_RETURN_ON_ERROR(mqtt_helper_normalize_uri(host, s_active_uri, sizeof(s_active_uri)), TAG, "URI normalize failed");

    if (s_client) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .uri = s_active_uri,
            },
        },
        .credentials = {
            .client_id = mqtt_helper_generate_client_id(),
            .username = (username && strlen(username) > 0) ? username : NULL,
            .authentication = {
                .password = password,
            },
        },
        .session = {
            .last_will = {
                .topic = s_will_set ? s_will_topic : NULL,
                .msg = s_will_set ? s_will_payload : NULL,
                .msg_len = s_will_set ? strlen(s_will_payload) : 0,
                .qos = s_will_set ? s_will_qos : 0,
                .retain = s_will_set ? s_will_retain : false,
            },
            .disable_clean_session = false,  // keep session/subscriptions on broker
        },
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    ESP_RETURN_ON_FALSE(s_client != NULL, ESP_FAIL, TAG, "Failed to init MQTT client");
    ESP_RETURN_ON_ERROR(esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL),
                        TAG, "Failed to register MQTT event");

    s_connected = false;
    mqtt_helper_update_ui(false, "MQTT connecting...");
    ESP_LOGI(TAG, "Connecting to MQTT broker: %s", s_active_uri);
    return esp_mqtt_client_start(s_client);
}

esp_err_t mqtt_helper_connect_saved(void)
{
    esp_err_t err = mqtt_helper_load_saved_config();
    if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }
    if (err != ESP_OK) {
        return err;
    }
    return mqtt_helper_connect(s_saved_host, s_saved_user, s_saved_pass);
}

void mqtt_helper_disconnect(void)
{
    if (s_client) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
    s_connected = false;
    mqtt_helper_update_ui(false, "MQTT stopped");
}

bool mqtt_helper_is_connected(void)
{
    return s_connected;
}

esp_err_t mqtt_helper_publish(const char *topic, const char *payload, int qos, bool retain)
{
    ESP_RETURN_ON_FALSE(s_client && s_connected, ESP_ERR_INVALID_STATE, TAG, "MQTT not connected");
    int msg_id = esp_mqtt_client_publish(s_client, topic, payload, 0, qos, retain);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t mqtt_helper_subscribe(const char *topic, int qos)
{
    ESP_RETURN_ON_FALSE(s_client, ESP_ERR_INVALID_STATE, TAG, "MQTT client not started");
    int msg_id = esp_mqtt_client_subscribe(s_client, topic, qos);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

void mqtt_helper_set_message_cb(mqtt_helper_msg_cb_t cb, void *user_data)
{
    s_msg_cb = cb;
    s_msg_cb_user = user_data;
}

void mqtt_helper_set_connection_cb(mqtt_helper_conn_cb_t cb, void *user_data)
{
    s_conn_cb = cb;
    s_conn_cb_user = user_data;
}
