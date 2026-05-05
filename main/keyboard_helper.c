/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "keyboard_helper.h"

static lv_obj_t *s_overlay_bg;
static lv_obj_t *s_overlay_ta;
static lv_obj_t *s_overlay_content;
static lv_obj_t *s_keyboard_container;
static lv_obj_t *s_input_row;
static lv_obj_t *s_placeholder_label;
static lv_obj_t *s_keyboard;
static lv_obj_t *s_overlay_close_btn;
static lv_obj_t *s_active_textarea;

static void keyboard_helper_create_overlay(void);
static void keyboard_helper_show_overlay(lv_obj_t *source_ta);
static void keyboard_helper_hide_overlay(bool apply_changes);
static void keyboard_overlay_bg_event_cb(lv_event_t *e);
static void keyboard_overlay_close_event_cb(lv_event_t *e);
static void keyboard_event_cb(lv_event_t *e);
static void keyboard_focus_event_cb(lv_event_t *e);

void keyboard_helper_init(void)
{
    /* Overlay is created on first use; nothing to do here yet. */
}

void keyboard_helper_attach(lv_obj_t *textarea)
{
    if (!textarea) {
        return;
    }
    lv_obj_add_event_cb(textarea, keyboard_focus_event_cb, LV_EVENT_FOCUSED, NULL);
}

static void keyboard_helper_create_overlay(void)
{
    if (s_overlay_bg) {
        return;
    }

    s_overlay_bg = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_overlay_bg);
    lv_obj_set_size(s_overlay_bg, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_overlay_bg, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_overlay_bg, LV_OPA_60, 0);
    lv_obj_set_flex_flow(s_overlay_bg, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_overlay_bg, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(s_overlay_bg, 14, 0);
    lv_obj_set_style_pad_gap(s_overlay_bg, 0, 0);  /* keep textarea + keyboard visually connected */
    lv_obj_add_flag(s_overlay_bg, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_scroll_dir(s_overlay_bg, LV_DIR_NONE);
    lv_obj_add_event_cb(s_overlay_bg, keyboard_overlay_bg_event_cb, LV_EVENT_CLICKED, NULL);

    s_overlay_close_btn = lv_button_create(s_overlay_bg);
    lv_obj_set_size(s_overlay_close_btn, 60, 38);
    lv_obj_align(s_overlay_close_btn, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_add_event_cb(s_overlay_close_btn, keyboard_overlay_close_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_overlay_close_btn, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_style_radius(s_overlay_close_btn, 10, 0);
    lv_obj_set_style_bg_color(s_overlay_close_btn, lv_color_hex(0x1F2937), 0);
    lv_obj_set_style_bg_opa(s_overlay_close_btn, LV_OPA_90, 0);
    lv_obj_t *close_label = lv_label_create(s_overlay_close_btn);
    lv_label_set_text(close_label, "Cancel");
    lv_obj_center(close_label);

    /* Content column keeps textarea + keyboard centered horizontally. */
    s_overlay_content = lv_obj_create(s_overlay_bg);
    lv_obj_remove_style_all(s_overlay_content);
    lv_obj_set_size(s_overlay_content, 900, LV_SIZE_CONTENT);  /* Set fixed width */
    lv_obj_set_flex_flow(s_overlay_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_overlay_content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(s_overlay_content, 0, 0);  /* Gap between input row and keyboard */
    lv_obj_set_style_pad_left(s_overlay_content, 40, 0);
    lv_obj_clear_flag(s_overlay_content, LV_OBJ_FLAG_SCROLLABLE);

    /* Container for keyboard and input row with light gray background */
    s_keyboard_container = lv_obj_create(s_overlay_content);
    lv_obj_remove_style_all(s_keyboard_container);
    lv_obj_set_flex_flow(s_keyboard_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(s_keyboard_container, LV_PCT(100));
    lv_obj_set_style_bg_opa(s_keyboard_container, LV_OPA_COVER, 0);  /* Make background opaque */
    lv_obj_set_height(s_keyboard_container, 300);  /* Reduced height to fit screen when centered */
    lv_obj_set_style_bg_color(s_keyboard_container, lv_color_hex(0xD3D3D3), 0);  /* Light gray background */
    lv_obj_set_style_pad_all(s_keyboard_container, 0, 0);  /* No padding */
    lv_obj_set_style_radius(s_keyboard_container, 10, 0);  /* Rounded borders */

    /* Row for label and textarea side by side */
    s_input_row = lv_obj_create(s_keyboard_container);
    lv_obj_remove_style_all(s_input_row);
    lv_obj_set_flex_flow(s_input_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(s_input_row, 10, 0);
    lv_obj_set_width(s_input_row, LV_PCT(100));
    lv_obj_set_height(s_input_row, 50);  /* Fixed height to reduce vertical space */
    lv_obj_set_style_bg_opa(s_input_row, LV_OPA_TRANSP, 0);  /* Make background transparent */

    s_placeholder_label = lv_label_create(s_input_row);
    lv_label_set_text(s_placeholder_label, "");
    lv_obj_set_style_text_font(s_placeholder_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_placeholder_label, lv_color_black(), 0);  /* Black font */
    lv_obj_set_style_pad_all(s_placeholder_label, 15, 0);  /* 15px padding */
    lv_obj_set_width(s_placeholder_label, 200);  /* Fixed width for label */

    s_overlay_ta = lv_textarea_create(s_input_row);
    lv_obj_set_flex_grow(s_overlay_ta, 1);  /* Take remaining space */
    lv_textarea_set_max_length(s_overlay_ta, 256);

    s_keyboard = lv_keyboard_create(s_keyboard_container);
    lv_obj_set_size(s_keyboard, 847, 242);  /* Set fixed size for the keyboard */
    lv_obj_set_style_bg_opa(s_keyboard, LV_OPA_TRANSP, 1);  /* Make keyboard background transparent */
    lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_event_cb(s_keyboard, keyboard_event_cb, LV_EVENT_ALL, NULL);
}

static void keyboard_helper_show_overlay(lv_obj_t *source_ta)
{
    if (!source_ta) {
        return;
    }
    keyboard_helper_create_overlay();
    s_active_textarea = source_ta;

    /* Mirror relevant settings from the source textarea into the overlay textarea. */
    if (s_overlay_ta) {
        lv_textarea_set_password_mode(s_overlay_ta, lv_textarea_get_password_mode(source_ta));
        lv_textarea_set_one_line(s_overlay_ta, lv_textarea_get_one_line(source_ta));
        lv_textarea_set_max_length(s_overlay_ta, lv_textarea_get_max_length(source_ta));
        const char *accepted = lv_textarea_get_accepted_chars(source_ta);
        lv_textarea_set_accepted_chars(s_overlay_ta, accepted);
        const char *placeholder = lv_textarea_get_placeholder_text(source_ta);
        lv_textarea_set_placeholder_text(s_overlay_ta, placeholder);
        lv_label_set_text(s_placeholder_label, placeholder ? placeholder : "");
        lv_textarea_set_text(s_overlay_ta, lv_textarea_get_text(source_ta));
        lv_obj_add_state(s_overlay_ta, LV_STATE_FOCUSED);
    }

    if (s_keyboard) {
        lv_keyboard_set_textarea(s_keyboard, s_overlay_ta);
    }

    lv_obj_clear_flag(s_overlay_bg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_overlay_bg);
}

static void keyboard_helper_hide_overlay(bool apply_changes)
{
    if (apply_changes && s_active_textarea && s_overlay_ta) {
        const char *new_text = lv_textarea_get_text(s_overlay_ta);
        lv_textarea_set_text(s_active_textarea, new_text);
    }

    if (s_keyboard) {
        lv_keyboard_set_textarea(s_keyboard, NULL);
    }

    if (s_overlay_bg) {
        lv_obj_add_flag(s_overlay_bg, LV_OBJ_FLAG_HIDDEN);
    }

    s_active_textarea = NULL;
}

static void keyboard_overlay_bg_event_cb(lv_event_t *e)
{
    /* Only close when the dimmed backdrop itself is tapped (not when children are). */
    if (lv_event_get_target(e) == s_overlay_bg) {
        keyboard_helper_hide_overlay(false);
    }
}

static void keyboard_overlay_close_event_cb(lv_event_t *e)
{
    (void)e;
    keyboard_helper_hide_overlay(false);
}

static void keyboard_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CANCEL) {
        keyboard_helper_hide_overlay(false);
    } else if (code == LV_EVENT_READY) {
        keyboard_helper_hide_overlay(true);
    }
}

static void keyboard_focus_event_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    keyboard_helper_show_overlay(ta);
}
