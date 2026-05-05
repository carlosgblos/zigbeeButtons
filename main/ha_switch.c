/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ha_switch.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_log.h"
#include "mqtt_helper.h"
#include "main_tab.h"
#include "button_config.h"

#define HA_DEVICE_ID        "espSwitch"
#define HA_DEVICE_NAME      "espSwitch"
#define HA_MODEL            "ESP32-P4 Switch"
#define HA_MANUFACTURER     "ESP"
#define HA_SW_VERSION       "1.0"

#define HA_AVAIL_TOPIC      HA_DEVICE_ID "/status"
#define HA_PAYLOAD_ON       "ON"
#define HA_PAYLOAD_OFF      "OFF"

static const char *TAG = "ha_switch";

static const char *s_command_topics[6];
static const char *s_state_topics[6];
static const char *s_label_topics[6];
static bool s_states[6];
static uint8_t s_btn_types[6];

static void ha_switch_publish_discovery(void);
static void ha_switch_publish_state(uint32_t idx);
static void ha_switch_publish_all_states(void);
static void ha_switch_publish_available(bool online);
static void ha_switch_on_button(uint32_t btn_id, bool on, void *user);
static void ha_switch_on_mqtt_msg(const char *topic, const char *payload, int payload_len, void *user);
static void ha_switch_on_mqtt_conn(bool connected, void *user);
static bool ha_switch_parse_payload(const char *payload, int len, bool *out_on);
static void ha_switch_apply_remote(uint32_t idx, bool on);
/* Async helper to apply UI changes on LVGL thread */
struct ha_apply_arg {
    uint32_t idx;
    bool on;
};
static void ha_switch_apply_remote_async(void *arg)
{
    struct ha_apply_arg *a = (struct ha_apply_arg *)arg;
    if (!a) return;
    if (a->on) main_tab_button_set_on(NULL, a->idx);
    else main_tab_button_set_off(NULL, a->idx);
    free(a);
}
static void ha_switch_set_label(uint32_t idx, const char *text);
/* Async helper for setting label on LVGL thread */
struct ha_label_arg {
    uint32_t idx;
    char text[16];
};
static void ha_switch_set_label_async(void *arg)
{
    struct ha_label_arg *a = (struct ha_label_arg *)arg;
    if (!a) return;
    main_tab_set_button_label(a->idx, a->text);
    free(a);
}

void ha_switch_init(void)
{
    static const char *cmds[] = {
        HA_DEVICE_ID "/btn1/set",
        HA_DEVICE_ID "/btn2/set",
        HA_DEVICE_ID "/btn3/set",
        HA_DEVICE_ID "/btn4/set",
        HA_DEVICE_ID "/btn5/set",
        HA_DEVICE_ID "/btn6/set",
    };
    static const char *states[] = {
        HA_DEVICE_ID "/btn1/state",
        HA_DEVICE_ID "/btn2/state",
        HA_DEVICE_ID "/btn3/state",
        HA_DEVICE_ID "/btn4/state",
        HA_DEVICE_ID "/btn5/state",
        HA_DEVICE_ID "/btn6/state",
    };
    static const char *labels[] = {
        HA_DEVICE_ID "/btn1/label",
        HA_DEVICE_ID "/btn2/label",
        HA_DEVICE_ID "/btn3/label",
        HA_DEVICE_ID "/btn4/label",
        HA_DEVICE_ID "/btn5/label",
        HA_DEVICE_ID "/btn6/label",
    };
    memcpy(s_command_topics, cmds, sizeof(cmds));
    memcpy(s_state_topics, states, sizeof(states));
    memcpy(s_label_topics, labels, sizeof(labels));

    ha_switch_reload_config();

    /* Ensure HA sees offline if we drop unexpectedly */
    mqtt_helper_set_will(HA_AVAIL_TOPIC, "offline", 1, false);
    mqtt_helper_set_message_cb(ha_switch_on_mqtt_msg, NULL);
    mqtt_helper_set_connection_cb(ha_switch_on_mqtt_conn, NULL);
    main_tab_register_button_cb(ha_switch_on_button, NULL);
}

void ha_switch_reload_config(void)
{
    uint8_t count;
    btn_cfg_t cfg[6];
    btn_config_load(&count, cfg);
    for (uint8_t i = 0; i < 6; i++) s_btn_types[i] = cfg[i].type;
}

static void ha_switch_on_button(uint32_t btn_id, bool on, void *user)
{
    (void)user;
    if (btn_id >= 6) {
        return;
    }
    s_states[btn_id] = on;
    ha_switch_publish_state(btn_id);
}

static void ha_switch_on_mqtt_conn(bool connected, void *user)
{
    (void)user;
    if (!connected) {
        return;
    }

    /* Refresh types so subscribe decisions reflect the current config. */
    ha_switch_reload_config();

    ha_switch_publish_available(true);

    /* Listen to our own availability topic to counter unexpected offline announcements. */
    mqtt_helper_subscribe(HA_AVAIL_TOPIC, 1);

    for (uint32_t i = 0; i < 6; i++) {
        /* Scene buttons are send-only — never subscribe to their command topic so
         * retained HA messages cannot reach the device and corrupt toggle state. */
        if (s_btn_types[i] != BTN_TYPE_SCENE) {
            mqtt_helper_subscribe(s_command_topics[i], 1);
        }
        mqtt_helper_subscribe(s_label_topics[i], 1);
    }

    ha_switch_publish_discovery();
}

static void ha_switch_on_mqtt_msg(const char *topic, const char *payload, int payload_len, void *user)
{
    (void)user;
    ESP_LOGI(TAG, "MQTT rx topic=\"%s\" payload=\"%.*s\" len=%d", topic ? topic : "(null)",
             payload_len, payload ? payload : "", payload_len);
    if (topic && strcmp(topic, HA_AVAIL_TOPIC) == 0 && payload && payload_len == 7 &&
        strncasecmp(payload, "offline", 7) == 0) {
        ha_switch_publish_available(true);
    }
    for (uint32_t i = 0; i < 6; i++) {
        if (strcmp(topic, s_label_topics[i]) == 0) {
            /* Treat payload as new label text (max 15 chars) */
            char label[16];
            size_t len = (payload_len < (int)sizeof(label) - 1) ? (size_t)payload_len : sizeof(label) - 1;
            if (len > 0) {
                memcpy(label, payload, len);
                label[len] = '\0';
                ha_switch_set_label(i, label);
            }
            return;
        }
        if (strcmp(topic, s_command_topics[i]) == 0) {
            if (s_btn_types[i] == BTN_TYPE_SCENE) {
                ESP_LOGI(TAG, "MQTT cmd btn%u ignored (scene)", (unsigned)i + 1);
                return;
            }
            bool on = false;
            if (ha_switch_parse_payload(payload, payload_len, &on)) {
                if (on != s_states[i]) {
                    ha_switch_apply_remote(i, on);
                    s_states[i] = on;
                    ha_switch_publish_state(i);
                }
                ESP_LOGI(TAG, "MQTT cmd btn%u -> %s", (unsigned)i + 1, on ? "ON" : "OFF");
            }
            return;
        }
    }
}

static bool ha_switch_parse_payload(const char *payload, int len, bool *out_on)
{
    if (!payload || len <= 0 || !out_on) {
        return false;
    }

    if ((len == 2 && strncasecmp(payload, "on", 2) == 0) ||
        (len == 3 && strncasecmp(payload, "yes", 3) == 0) ||
        (len == 4 && strncasecmp(payload, "true", 4) == 0) ||
        (len == 1 && (payload[0] == '1' || payload[0] == 'T' || payload[0] == 't'))) {
        *out_on = true;
        return true;
    }
    if ((len == 3 && strncasecmp(payload, "off", 3) == 0) ||
        (len == 2 && strncasecmp(payload, "no", 2) == 0) ||
        (len == 5 && strncasecmp(payload, "false", 5) == 0) ||
        (len == 1 && payload[0] == '0')) {
        *out_on = false;
        return true;
    }
    return false;
}

static void ha_switch_apply_remote(uint32_t idx, bool on)
{
    /* Schedule the UI change on the LVGL thread to avoid calling LVGL from MQTT task */
    struct ha_apply_arg *arg = malloc(sizeof(*arg));
    if (!arg) return;
    arg->idx = idx;
    arg->on = on;
    lv_async_call(ha_switch_apply_remote_async, arg);
}

static void ha_switch_set_label(uint32_t idx, const char *text)
{
    if (!text || idx >= 6) return;
    struct ha_label_arg *arg = malloc(sizeof(*arg));
    if (!arg) return;
    arg->idx = idx;
    snprintf(arg->text, sizeof(arg->text), "%.*s", (int)(sizeof(arg->text) - 1), text);
    lv_async_call(ha_switch_set_label_async, arg);
}

static void ha_switch_publish_discovery(void)
{
    for (uint32_t i = 0; i < 6; i++) {
        char topic[128];
        char payload[512];
        bool is_scene = (s_btn_types[i] == BTN_TYPE_SCENE);

        if (is_scene) {
            /* Scene: read-only, no availability tracking.
             * Omitting availability_topic means HA never marks it unavailable,
             * so there is no unavailable→state transition on reconnect that
             * would fire automations. Omitting command_topic makes it read-only. */
            snprintf(topic, sizeof(topic),
                     "homeassistant/switch/%s/btn%u/config", HA_DEVICE_ID, (unsigned)i + 1);
            snprintf(payload, sizeof(payload),
                     "{"
                     "\"name\":\"Scene %u\","
                     "\"unique_id\":\"%s_btn%u\","
                     "\"state_topic\":\"%s\","
                     "\"payload_on\":\"%s\","
                     "\"payload_off\":\"%s\","
                     "\"device\":{"
                     "\"identifiers\":[\"%s\"],"
                     "\"name\":\"%s\","
                     "\"manufacturer\":\"%s\","
                     "\"model\":\"%s\","
                     "\"sw_version\":\"%s\""
                     "}"
                     "}",
                     (unsigned)i + 1,
                     HA_DEVICE_ID, (unsigned)i + 1,
                     s_state_topics[i],
                     HA_PAYLOAD_ON,
                     HA_PAYLOAD_OFF,
                     HA_DEVICE_ID,
                     HA_DEVICE_NAME,
                     HA_MANUFACTURER,
                     HA_MODEL,
                     HA_SW_VERSION);
        } else {
            snprintf(topic, sizeof(topic),
                     "homeassistant/switch/%s/btn%u/config", HA_DEVICE_ID, (unsigned)i + 1);
            snprintf(payload, sizeof(payload),
                     "{"
                     "\"name\":\"Switch %u\","
                     "\"unique_id\":\"%s_btn%u\","
                     "\"state_topic\":\"%s\","
                     "\"command_topic\":\"%s\","
                     "\"availability_topic\":\"%s\","
                     "\"payload_available\":\"online\","
                     "\"payload_not_available\":\"offline\","
                     "\"payload_on\":\"%s\","
                     "\"payload_off\":\"%s\","
                     "\"device\":{"
                     "\"identifiers\":[\"%s\"],"
                     "\"name\":\"%s\","
                     "\"manufacturer\":\"%s\","
                     "\"model\":\"%s\","
                     "\"sw_version\":\"%s\""
                     "}"
                     "}",
                     (unsigned)i + 1,
                     HA_DEVICE_ID, (unsigned)i + 1,
                     s_state_topics[i],
                     s_command_topics[i],
                     HA_AVAIL_TOPIC,
                     HA_PAYLOAD_ON,
                     HA_PAYLOAD_OFF,
                     HA_DEVICE_ID,
                     HA_DEVICE_NAME,
                     HA_MANUFACTURER,
                     HA_MODEL,
                     HA_SW_VERSION);
        }

        mqtt_helper_publish(topic, payload, 1, true);
    }
}

static void ha_switch_publish_state(uint32_t idx)
{
    if (idx >= 6) {
        return;
    }
    esp_err_t err = mqtt_helper_publish(s_state_topics[idx], s_states[idx] ? HA_PAYLOAD_ON : HA_PAYLOAD_OFF, 1, false);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Publish state btn%u failed: %s", (unsigned)idx + 1, esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Published state btn%u: %s to topic: %s", (unsigned)idx + 1, s_states[idx] ? "ON" : "OFF", s_state_topics[idx]);
    }
}

static void ha_switch_publish_all_states(void)
{
    for (uint32_t i = 0; i < 6; i++) {
        ha_switch_publish_state(i);
    }
}

static void ha_switch_publish_available(bool online)
{
    esp_err_t err = mqtt_helper_publish(HA_AVAIL_TOPIC, online ? "online" : "offline", 1, false);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Publish availability failed: %s", esp_err_to_name(err));
    }
    ESP_LOGW(TAG, "Publish availability : %s", online ? "online" : "offline");

}
