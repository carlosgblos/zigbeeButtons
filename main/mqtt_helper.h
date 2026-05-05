/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

typedef void (*mqtt_helper_msg_cb_t)(const char *topic, const char *payload, int payload_len, void *user_data);
typedef void (*mqtt_helper_conn_cb_t)(bool connected, void *user_data);

/* Save broker IP/host (URI or raw IP accepted) with username/password */
esp_err_t mqtt_helper_save_config(const char *host, const char *username, const char *password);
esp_err_t mqtt_helper_get_saved_config(char *host, size_t host_len, char *username, size_t user_len,
                                       char *password, size_t password_len);

/* Optional: set a Last Will message to apply on next connect (pass NULL topic to clear) */
esp_err_t mqtt_helper_set_will(const char *topic, const char *payload, int qos, bool retain);

/* Connect using provided host/password (host can be bare IP; mqtt:// is added automatically) */
esp_err_t mqtt_helper_connect(const char *host, const char *username, const char *password);
esp_err_t mqtt_helper_connect_saved(void);
void mqtt_helper_disconnect(void);
bool mqtt_helper_is_connected(void);

/* Convenience publish/subscribe APIs (require connection) */
esp_err_t mqtt_helper_publish(const char *topic, const char *payload, int qos, bool retain);
esp_err_t mqtt_helper_subscribe(const char *topic, int qos);

/* Register callbacks for incoming messages and connection events (single handler each) */
void mqtt_helper_set_message_cb(mqtt_helper_msg_cb_t cb, void *user_data);
void mqtt_helper_set_connection_cb(mqtt_helper_conn_cb_t cb, void *user_data);
