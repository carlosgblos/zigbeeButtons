/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "main_tab.h"
#include "button_config.h"

#define SCENE_REVERT_MS 400

#include <string.h>

#define COLOR_OFF       0x2D3356
#define COLOR_ON        0xE07818
#define COLOR_BG        0x12172B
#define CARD_RADIUS     18
#define GRID_GAP        10
#define GRID_PAD        10

/* Grid layout descriptors — must be static (LVGL holds a pointer) */
static lv_coord_t s_col_dsc[BTN_MAX_COUNT + 2];
static lv_coord_t s_row_dsc[BTN_MAX_COUNT + 2];

/* Widget references */
static lv_obj_t *s_grid;
static lv_obj_t *s_cards[BTN_MAX_COUNT];
static lv_obj_t *s_icon_labels[BTN_MAX_COUNT];
static lv_obj_t *s_name_labels[BTN_MAX_COUNT];

/* State */
static bool s_states[BTN_MAX_COUNT];
static uint8_t s_types[BTN_MAX_COUNT];
static uint8_t s_count;

/* Callback */
static main_tab_button_cb_t s_btn_cb;
static void *s_btn_cb_user;

static void scene_revert_cb(lv_timer_t *t)
{
    uint32_t idx = (uint32_t)(uintptr_t)lv_timer_get_user_data(t);
    if (idx < BTN_MAX_COUNT) {
        s_states[idx] = false;
        if (s_cards[idx]) {
            lv_obj_set_style_bg_color(s_cards[idx], lv_color_hex(COLOR_OFF), 0);
        }
    }
    lv_timer_delete(t);
}

static void card_click_cb(lv_event_t *e)
{
    uint32_t idx = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    if (idx >= s_count) return;

    if (s_types[idx] == BTN_TYPE_SCENE) {
        lv_obj_set_style_bg_color(s_cards[idx], lv_color_hex(COLOR_ON), 0);
        if (s_btn_cb) s_btn_cb(idx, true, s_btn_cb_user);
        lv_timer_create(scene_revert_cb, SCENE_REVERT_MS, (void *)(uintptr_t)idx);
    } else {
        s_states[idx] = !s_states[idx];
        lv_obj_set_style_bg_color(s_cards[idx],
            s_states[idx] ? lv_color_hex(COLOR_ON) : lv_color_hex(COLOR_OFF), 0);
        if (s_btn_cb) s_btn_cb(idx, s_states[idx], s_btn_cb_user);
    }
}

static void build_grid(uint8_t count, const btn_cfg_t *cfg)
{
    lv_obj_clean(s_grid);
    memset(s_cards, 0, sizeof(s_cards));
    memset(s_icon_labels, 0, sizeof(s_icon_labels));
    memset(s_name_labels, 0, sizeof(s_name_labels));
    s_count = count;
    for (uint8_t i = 0; i < count; i++) s_types[i] = cfg[i].type;

    /* Determine grid dimensions */
    uint8_t ncols, nrows;
    if      (count <= 1) { ncols = 1; nrows = 1; }
    else if (count <= 2) { ncols = 2; nrows = 1; }
    else if (count <= 3) { ncols = 3; nrows = 1; }
    else if (count <= 4) { ncols = 2; nrows = 2; }
    else                 { ncols = 3; nrows = 2; }

    for (int i = 0; i < ncols; i++) s_col_dsc[i] = LV_GRID_FR(1);
    s_col_dsc[ncols] = LV_GRID_TEMPLATE_LAST;

    for (int i = 0; i < nrows; i++) s_row_dsc[i] = LV_GRID_FR(1);
    s_row_dsc[nrows] = LV_GRID_TEMPLATE_LAST;

    lv_obj_set_grid_dsc_array(s_grid, s_col_dsc, s_row_dsc);

    for (uint8_t i = 0; i < count; i++) {
        uint8_t col = i % ncols;
        uint8_t row = i / ncols;

        /* Card */
        lv_obj_t *card = lv_obj_create(s_grid);
        lv_obj_remove_style_all(card);
        lv_obj_set_style_bg_color(card,
            s_states[i] ? lv_color_hex(COLOR_ON) : lv_color_hex(COLOR_OFF), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card, CARD_RADIUS, 0);
        lv_obj_set_style_pad_all(card, 16, 0);
        lv_obj_set_style_opa(card, LV_OPA_70, LV_STATE_PRESSED);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_grid_cell(card,
            LV_GRID_ALIGN_STRETCH, col, 1,
            LV_GRID_ALIGN_STRETCH, row, 1);
        lv_obj_add_event_cb(card, card_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        s_cards[i] = card;

        /* Icon */
        lv_obj_t *icon = lv_label_create(card);
        lv_label_set_text(icon, btn_config_icon_symbol(cfg[i].icon_idx));
        lv_obj_set_style_text_color(icon, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_24, 0);
        s_icon_labels[i] = icon;

        /* Name */
        lv_obj_t *label = lv_label_create(card);
        lv_label_set_text(label, cfg[i].name);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
        s_name_labels[i] = label;
    }
}

void main_tab_init(lv_obj_t *tab)
{
    if (!tab) return;

    lv_obj_set_style_bg_color(tab, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tab, 0, 0);
    lv_obj_set_style_pad_all(tab, GRID_PAD, 0);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);

    s_grid = lv_obj_create(tab);
    lv_obj_remove_style_all(s_grid);
    lv_obj_set_width(s_grid, LV_PCT(100));
    lv_obj_set_flex_grow(s_grid, 1);
    lv_obj_set_style_pad_column(s_grid, GRID_GAP, 0);
    lv_obj_set_style_pad_row(s_grid, GRID_GAP, 0);
    lv_obj_clear_flag(s_grid, LV_OBJ_FLAG_SCROLLABLE);

    uint8_t count;
    btn_cfg_t cfg[BTN_MAX_COUNT];
    btn_config_load(&count, cfg);
    build_grid(count, cfg);
}

void main_tab_reload_config(void)
{
    if (!s_grid) return;
    uint8_t count;
    btn_cfg_t cfg[BTN_MAX_COUNT];
    btn_config_load(&count, cfg);
    build_grid(count, cfg);
}

void main_tab_button_set_on(lv_obj_t *btnm, uint32_t btn_id)
{
    (void)btnm;
    if (btn_id >= BTN_MAX_COUNT) return;
    s_states[btn_id] = true;
    if (s_cards[btn_id]) {
        lv_obj_set_style_bg_color(s_cards[btn_id], lv_color_hex(COLOR_ON), 0);
    }
}

void main_tab_button_set_off(lv_obj_t *btnm, uint32_t btn_id)
{
    (void)btnm;
    if (btn_id >= BTN_MAX_COUNT) return;
    s_states[btn_id] = false;
    if (s_cards[btn_id]) {
        lv_obj_set_style_bg_color(s_cards[btn_id], lv_color_hex(COLOR_OFF), 0);
    }
}

void main_tab_button_toggle(lv_obj_t *btnm, uint32_t btn_id)
{
    if (s_states[btn_id]) main_tab_button_set_off(btnm, btn_id);
    else main_tab_button_set_on(btnm, btn_id);
}

lv_obj_t *main_tab_get_btnm(void)
{
    return NULL;
}

void main_tab_register_button_cb(main_tab_button_cb_t cb, void *user_data)
{
    s_btn_cb = cb;
    s_btn_cb_user = user_data;
}

void main_tab_set_button_label(uint32_t btn_id, const char *text)
{
    if (!text || btn_id >= BTN_MAX_COUNT) return;

    /* Persist the new name */
    uint8_t count;
    btn_cfg_t cfg[BTN_MAX_COUNT];
    btn_config_load(&count, cfg);
    strlcpy(cfg[btn_id].name, text, BTN_NAME_MAX);
    btn_config_save(count, cfg);

    /* Update live label if it exists */
    if (s_name_labels[btn_id]) {
        lv_label_set_text(s_name_labels[btn_id], text);
    }
}
