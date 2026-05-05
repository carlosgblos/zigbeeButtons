/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "main_tab.h"

#include <stdio.h>
#include "display_helper.h"

static char s_labels[6][16];
static const char *btnm_map[] = {
    s_labels[0], s_labels[1], "\n",
    s_labels[2], s_labels[3], "\n",
    s_labels[4], s_labels[5], ""
};

static lv_obj_t *s_btnm;
static main_tab_button_cb_t s_btn_cb;
static void *s_btn_cb_user;

static void btnm_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    lv_obj_t *btnm = lv_event_get_target(e);

    /* Prefer the button id passed as event param (sent by the buttonmatrix) */
    uint32_t id = LV_BUTTONMATRIX_BUTTON_NONE;
    uint32_t *p = (uint32_t *)lv_event_get_param(e);
    if(p) id = *p;
    else id = lv_buttonmatrix_get_selected_button(btnm);

    if (id == LV_BUTTONMATRIX_BUTTON_NONE) return;

    const char *txt = lv_buttonmatrix_get_button_text(btnm, id);

    /* LVGL already toggles the checked state for checkable buttons. Just read it. */
    if (lv_buttonmatrix_has_button_ctrl(btnm, id, LV_BUTTONMATRIX_CTRL_CHECKED)) {
        printf("%s -> ON\n", txt ? txt : "(null)");
        if (s_btn_cb) s_btn_cb(id, true, s_btn_cb_user);
    } else {
        printf("%s -> OFF\n", txt ? txt : "(null)");
        if (s_btn_cb) s_btn_cb(id, false, s_btn_cb_user);
    }
}

void main_tab_init(lv_obj_t *tab)
{
    if (!tab) return;

    for (uint32_t i = 0; i < 6; i++) {
        snprintf(s_labels[i], sizeof(s_labels[i]), "BTN%u", (unsigned)i + 1);
    }

    /* Make the tab a flex column and remove previous content */
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(tab, 0, 0);
    lv_obj_set_style_pad_gap(tab, 0, 0);

    /* Create a panel that fills the tab and holds the button matrix */
    lv_obj_t *panel = lv_obj_create(tab);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(panel, 1);

    lv_obj_t *btnm = lv_buttonmatrix_create(panel);
    lv_buttonmatrix_set_map(btnm, btnm_map);
    lv_obj_set_size(btnm, LV_PCT(100), LV_PCT(100));
    s_btnm = btnm;

    /* Material-like style for buttons */
    static lv_style_t mat_btn_style;
    lv_style_init(&mat_btn_style);
    /* Blue gradient button face, white text, rounded with subtle shadow */
    lv_style_set_bg_color(&mat_btn_style, lv_color_hex(0x1E90FF));
    lv_style_set_bg_grad_color(&mat_btn_style, lv_color_hex(0x0B61B8));
    lv_style_set_bg_grad_dir(&mat_btn_style, LV_GRAD_DIR_VER);
    lv_style_set_bg_opa(&mat_btn_style, LV_OPA_COVER);
    lv_style_set_text_color(&mat_btn_style, lv_color_hex(0xFFFFFF));
    lv_style_set_text_font(&mat_btn_style, &lv_font_montserrat_20);
    lv_style_set_pad_all(&mat_btn_style, 12);
    lv_style_set_radius(&mat_btn_style, 8);
    lv_style_set_border_width(&mat_btn_style, 2);
    lv_style_set_border_color(&mat_btn_style, lv_color_hex(0x0A3A66));
    lv_style_set_shadow_width(&mat_btn_style, 6);
    lv_style_set_shadow_ofs_y(&mat_btn_style, 3);
    lv_style_set_shadow_color(&mat_btn_style, lv_color_hex(0x02263f));

    /* Pressed style */
    static lv_style_t mat_btn_pr;
    lv_style_init(&mat_btn_pr);
    /* Pressed: darker gradient and slight inset look */
    lv_style_set_bg_color(&mat_btn_pr, lv_color_hex(0x0B61B8));
    lv_style_set_bg_grad_color(&mat_btn_pr, lv_color_hex(0x083B6A));
    lv_style_set_bg_grad_dir(&mat_btn_pr, LV_GRAD_DIR_VER);
    lv_style_set_bg_opa(&mat_btn_pr, LV_OPA_COVER);
    lv_style_set_text_color(&mat_btn_pr, lv_color_hex(0xFFFFFF));
    lv_style_set_border_color(&mat_btn_pr, lv_color_hex(0x04243d));
    lv_style_set_shadow_width(&mat_btn_pr, 2);
    lv_style_set_shadow_ofs_y(&mat_btn_pr, 1);

    /* Apply styles to the buttonmatrix items (buttons) only; ensure default state uses white face */
    lv_obj_add_style(btnm, &mat_btn_style, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_add_style(btnm, &mat_btn_pr, LV_PART_ITEMS | LV_STATE_PRESSED);

    /* Checked (ON) style: keep the blue face but use pressed border/shadow */
    static lv_style_t mat_btn_checked;
    lv_style_init(&mat_btn_checked);
    /* ON state should look like a permanently pressed button */
    lv_style_set_bg_color(&mat_btn_checked, lv_color_hex(0x0B61B8));
    lv_style_set_bg_grad_color(&mat_btn_checked, lv_color_hex(0x083B6A));
    lv_style_set_bg_grad_dir(&mat_btn_checked, LV_GRAD_DIR_VER);
    lv_style_set_bg_opa(&mat_btn_checked, LV_OPA_COVER);
    lv_style_set_text_color(&mat_btn_checked, lv_color_hex(0xFFFFFF));
    lv_style_set_text_font(&mat_btn_checked, &lv_font_montserrat_20);
    lv_style_set_pad_all(&mat_btn_checked, 12);
    lv_style_set_radius(&mat_btn_checked, 8);
    lv_style_set_border_width(&mat_btn_checked, 2);
    /* Use the pressed border color and lighter shadow so ON looks like pressed */
    lv_style_set_border_color(&mat_btn_checked, lv_color_hex(0x04243d));
    lv_style_set_shadow_width(&mat_btn_checked, 2);
    lv_style_set_shadow_ofs_y(&mat_btn_checked, 1);
    lv_style_set_shadow_color(&mat_btn_checked, lv_color_hex(0x04243d));

    lv_obj_add_style(btnm, &mat_btn_checked, LV_PART_ITEMS | LV_STATE_CHECKED);

    /* Make buttons toggleable (checkable) and fire VALUE_CHANGED on click (release) */
    lv_buttonmatrix_set_button_ctrl_all(btnm, LV_BUTTONMATRIX_CTRL_CHECKABLE | LV_BUTTONMATRIX_CTRL_CLICK_TRIG);

    lv_obj_add_event_cb(btnm, btnm_event_cb, LV_EVENT_ALL, NULL);
}

void main_tab_button_set_on(lv_obj_t *btnm, uint32_t btn_id)
{
    if (!btnm) return;
    lv_buttonmatrix_set_button_ctrl(btnm, btn_id, LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_CHECKABLE);
}

void main_tab_button_set_off(lv_obj_t *btnm, uint32_t btn_id)
{
    if (!btnm) return;
    /* Ensure button remains checkable but clear checked flag */
    lv_buttonmatrix_clear_button_ctrl(btnm, btn_id, LV_BUTTONMATRIX_CTRL_CHECKED);
    lv_buttonmatrix_set_button_ctrl(btnm, btn_id, LV_BUTTONMATRIX_CTRL_CHECKABLE);
}

void main_tab_button_toggle(lv_obj_t *btnm, uint32_t btn_id)
{
    if (!btnm) return;
    if (lv_buttonmatrix_has_button_ctrl(btnm, btn_id, LV_BUTTONMATRIX_CTRL_CHECKED)) {
        main_tab_button_set_off(btnm, btn_id);
    } else {
        main_tab_button_set_on(btnm, btn_id);
    }
}

lv_obj_t *main_tab_get_btnm(void)
{
    return s_btnm;
}

void main_tab_register_button_cb(main_tab_button_cb_t cb, void *user_data)
{
    s_btn_cb = cb;
    s_btn_cb_user = user_data;
}

void main_tab_set_button_label(uint32_t btn_id, const char *text)
{
    if (!s_btnm || btn_id >= 6 || !text) {
        return;
    }
    snprintf(s_labels[btn_id], sizeof(s_labels[btn_id]), "%.*s",
             (int)(sizeof(s_labels[btn_id]) - 1), text);
    lv_buttonmatrix_set_map(s_btnm, btnm_map); /* Refresh map to apply new label */
}
