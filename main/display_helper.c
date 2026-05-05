/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "display_helper.h"

#include <stdbool.h>

#include "esp_log.h"

typedef struct {
    lv_obj_t *container;
    lv_obj_t *title_label;
    lv_obj_t *detail_label;
    lv_obj_t *device_name_label;
    lv_obj_t *wifi_icon;
    lv_obj_t *mqtt_icon;
} display_status_bar_t;

static display_status_bar_t g_status_bar;
static display_helper_tabs_t g_tabs;

static lv_obj_t *create_text_column(lv_obj_t *parent);
static bool ensure_status_bar_exists(void);
static void action_button_event_cb(lv_event_t *e);
static void tab_button_event_cb(lv_event_t *e);
static void style_tab_button_bar(lv_obj_t *tabview);
static void anim_set_scale(void *obj, int32_t v);
static void update_tab_visibility(uint32_t active_idx);

void display_helper_create_status_bar(lv_obj_t *parent, const char *title_text, const char *detail_text)
{
    if (g_status_bar.container) {
        return;
    }

    lv_obj_t *target = parent ? parent : lv_screen_active();

    g_status_bar.container = lv_obj_create(target);
    lv_obj_set_size(g_status_bar.container, LV_PCT(100), 64);
    lv_obj_align(g_status_bar.container, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(g_status_bar.container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_status_bar.container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(g_status_bar.container, 12, 0);    // bias content slightly lower while keeping full text visible
    lv_obj_set_style_pad_bottom(g_status_bar.container, 8, 0);
    lv_obj_set_style_pad_left(g_status_bar.container, 12, 0);
    lv_obj_set_style_pad_right(g_status_bar.container, 12, 0);
    lv_obj_set_style_pad_gap(g_status_bar.container, 12, 0);
    lv_obj_set_style_radius(g_status_bar.container, 0, 0);
    lv_obj_set_style_bg_color(g_status_bar.container, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_bg_opa(g_status_bar.container, LV_OPA_90, 0);
    lv_obj_clear_flag(g_status_bar.container, LV_OBJ_FLAG_SCROLLABLE);

    /* Ensure no default border/outline/shadow is visible on the status bar */
    lv_obj_set_style_border_width(g_status_bar.container, 0, 0);
    lv_obj_set_style_border_opa(g_status_bar.container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_outline_width(g_status_bar.container, 0, 0);
    lv_obj_set_style_outline_opa(g_status_bar.container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(g_status_bar.container, 0, 0);

    /* Create the text column first and let it grow so the icon is pushed to the right */
    lv_obj_t *text_column = create_text_column(g_status_bar.container);
    lv_obj_set_flex_grow(text_column, 1);
    /* Slightly lower the column content to visually center with the bar (top padding only) */
    lv_obj_set_style_pad_top(text_column, 6, 0);

    g_status_bar.title_label = lv_label_create(text_column);
    lv_label_set_text(g_status_bar.title_label, title_text ? title_text : "Status");
    lv_obj_set_style_text_color(g_status_bar.title_label, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_text_font(g_status_bar.title_label, &lv_font_montserrat_20, 0);

    g_status_bar.detail_label = lv_label_create(text_column);
    lv_label_set_text(g_status_bar.detail_label, detail_text ? detail_text : "Ready");
    lv_obj_set_style_text_color(g_status_bar.detail_label, lv_color_hex(0x94A3B8), 0);
    lv_obj_set_style_text_font(g_status_bar.detail_label, &lv_font_montserrat_14, 0);

    g_status_bar.device_name_label = lv_label_create(g_status_bar.container);
    lv_label_set_text(g_status_bar.device_name_label, "");
    lv_label_set_long_mode(g_status_bar.device_name_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(g_status_bar.device_name_label, LV_PCT(42));
    lv_obj_set_style_text_align(g_status_bar.device_name_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(g_status_bar.device_name_label, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_text_font(g_status_bar.device_name_label, &lv_font_montserrat_24, 0);
    lv_obj_align(g_status_bar.device_name_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(g_status_bar.device_name_label, LV_OBJ_FLAG_FLOATING);
    lv_obj_clear_flag(g_status_bar.device_name_label, LV_OBJ_FLAG_CLICKABLE);

    /* Connection icons on the right */
    g_status_bar.wifi_icon = lv_label_create(g_status_bar.container);
    lv_label_set_text(g_status_bar.wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(g_status_bar.wifi_icon, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(g_status_bar.wifi_icon, lv_color_hex(0xEF4444), 0);
    lv_obj_set_style_pad_top(g_status_bar.wifi_icon, 0, 0);
    lv_obj_set_style_pad_bottom(g_status_bar.wifi_icon, 0, 0);

    g_status_bar.mqtt_icon = lv_label_create(g_status_bar.container);
    lv_label_set_text(g_status_bar.mqtt_icon, LV_SYMBOL_LOOP);
    lv_obj_set_style_text_font(g_status_bar.mqtt_icon, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(g_status_bar.mqtt_icon, lv_color_hex(0xEF4444), 0);
    lv_obj_set_style_pad_top(g_status_bar.mqtt_icon, 0, 0);
    lv_obj_set_style_pad_bottom(g_status_bar.mqtt_icon, 0, 0);
}

bool display_helper_create_tabs(lv_obj_t *parent, display_helper_tabs_t *tabs_out)
{
    lv_obj_t *target = parent ? parent : lv_screen_active();
    lv_obj_t *tabview = lv_tabview_create(target);
    lv_tabview_set_tab_bar_position(tabview, LV_DIR_LEFT);
    lv_tabview_set_tab_bar_size(tabview, 64);
    lv_obj_set_size(tabview, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(tabview, 1);
    lv_obj_remove_flag(lv_tabview_get_content(tabview), LV_OBJ_FLAG_SCROLLABLE);
    style_tab_button_bar(tabview);

    lv_obj_t *main_tab = lv_tabview_add_tab(tabview, LV_SYMBOL_HOME);
    lv_obj_set_size(main_tab, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(main_tab, 1);
    lv_obj_t *wifi_tab = lv_tabview_add_tab(tabview, LV_SYMBOL_SETTINGS);
    lv_obj_set_size(wifi_tab, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(wifi_tab, 1);
    lv_obj_t *buttons_tab = lv_tabview_add_tab(tabview, LV_SYMBOL_LIST);
    lv_obj_set_size(buttons_tab, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(buttons_tab, 1);

    g_tabs.tabview = tabview;
    g_tabs.main_tab = main_tab;
    g_tabs.wifi_tab = wifi_tab;
    g_tabs.buttons_tab = buttons_tab;
    if (tabs_out) {
        *tabs_out = g_tabs;
    }

    lv_obj_t *tab_bar = lv_tabview_get_tab_bar(tabview);
    for (uint32_t i = 0; i < lv_tabview_get_tab_count(tabview); i++) {
        lv_obj_t *btn = lv_obj_get_child_by_type(tab_bar, i, &lv_button_class);
        if (btn) {
            lv_obj_add_event_cb(btn, tab_button_event_cb, LV_EVENT_CLICKED, tabview);
        }
    }
    update_tab_visibility(0);

    return true;
}

void display_helper_add_action_button(lv_obj_t *parent)
{
    lv_obj_t *target = parent ? parent : lv_screen_active();

    lv_obj_t *btn = lv_button_create(target);
    lv_obj_set_size(btn, 180, 64);
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_INDIGO), 0);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_grad_color(btn, lv_palette_darken(LV_PALETTE_INDIGO, 2), 0);
    lv_obj_set_style_shadow_width(btn, 12, 0);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 110);
    lv_obj_set_style_transform_pivot_x(btn, lv_obj_get_width(btn) / 2, 0);
    lv_obj_set_style_transform_pivot_y(btn, lv_obj_get_height(btn) / 2, 0);
    lv_obj_add_event_cb(btn, action_button_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, "Press me");
    lv_obj_center(label);
}

void display_helper_set_status_bar_bg(lv_color_t color)
{
    if (!ensure_status_bar_exists()) {
        return;
    }
    lv_obj_set_style_bg_color(g_status_bar.container, color, 0);
}

void display_helper_set_status_text(const char *title_text, const char *detail_text)
{
    if (!ensure_status_bar_exists()) {
        return;
    }

    if (title_text && g_status_bar.title_label) {
        lv_label_set_text(g_status_bar.title_label, title_text);
    }

    if (detail_text && g_status_bar.detail_label) {
        lv_label_set_text(g_status_bar.detail_label, detail_text);
    }
}

void display_helper_set_device_name(const char *device_name)
{
    if (!ensure_status_bar_exists() || g_status_bar.device_name_label == NULL) {
        return;
    }
    lv_label_set_text(g_status_bar.device_name_label, device_name ? device_name : "");
}

void display_helper_set_wifi_indicator(lv_color_t color)
{
    if (!ensure_status_bar_exists() || g_status_bar.wifi_icon == NULL) {
        return;
    }
    lv_obj_set_style_text_color(g_status_bar.wifi_icon, color, 0);
}

void display_helper_set_mqtt_indicator(lv_color_t color)
{
    if (!ensure_status_bar_exists() || g_status_bar.mqtt_icon == NULL) {
        return;
    }
    lv_obj_set_style_text_color(g_status_bar.mqtt_icon, color, 0);
}

static lv_obj_t *create_text_column(lv_obj_t *parent)
{
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_remove_style_all(col);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(col, 10, 0);
    lv_obj_set_style_pad_gap(col, 4, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    return col;
}

static bool ensure_status_bar_exists(void)
{
    if (g_status_bar.container == NULL) {
        ESP_LOGE("display_helper", "%s: Status bar not created", __func__);
        return false;
    }
    return true;
}

static void action_button_event_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    if (label) {
        lv_label_set_text(label, "Tapped!");
    }

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, btn);
    lv_anim_set_values(&anim, 256, 310);
    lv_anim_set_duration(&anim, 120);
    lv_anim_set_playback_time(&anim, 120);
    lv_anim_set_repeat_count(&anim, 1);
    lv_anim_set_exec_cb(&anim, anim_set_scale);
    lv_anim_start(&anim);

    display_helper_set_status_text("System Status", "Button tapped");
}

static void anim_set_scale(void *obj, int32_t v)
{
    lv_obj_set_style_transform_scale((lv_obj_t *)obj, v, 0);
}

static void tab_button_event_cb(lv_event_t *e)
{
    /* user_data holds the tabview object we passed when registering the callback */
    lv_obj_t *tabview = (lv_obj_t *)lv_event_get_user_data(e);
    if (tabview == NULL) {
        /* fallback: find tabview by walking up from the button */
        lv_obj_t *btn = lv_event_get_current_target(e);
        tabview = lv_obj_get_parent(lv_obj_get_parent(btn));
    }

    uint32_t active = lv_tabview_get_tab_active(tabview);
    lv_tabview_set_active(tabview, active, LV_ANIM_OFF);
    update_tab_visibility(active);
}

static void style_tab_button_bar(lv_obj_t *tabview)
{
    lv_obj_t *tab_bar = lv_tabview_get_tab_bar(tabview);
    lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x0B1224), 0);
    lv_obj_set_style_bg_opa(tab_bar, LV_OPA_90, 0);
    lv_obj_set_style_pad_all(tab_bar, 6, 0);
    lv_obj_set_style_pad_gap(tab_bar, 8, 0);
    lv_obj_set_style_text_color(tab_bar, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_text_font(tab_bar, &lv_font_montserrat_20, 0);
}

static void update_tab_visibility(uint32_t active_idx)
{
    lv_obj_t *tabs[] = {
        g_tabs.main_tab,
        g_tabs.wifi_tab,
        g_tabs.buttons_tab,
    };

    for (uint32_t i = 0; i < sizeof(tabs) / sizeof(tabs[0]); i++) {
        if (!tabs[i]) continue;
        if (i == active_idx) lv_obj_clear_flag(tabs[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(tabs[i], LV_OBJ_FLAG_HIDDEN);
    }
}
