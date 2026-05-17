/**
 * @file ui/menus/settings/manage_networks_screen.c
 * @brief Saved Wi-Fi networks management body implementation.
 */

#include "manage_networks_screen.h"

#include "ui_button.h"
#include "ui_card.h"
#include "ui_label.h"
#include "ui_theme.h"

static void render_network_card(
    lv_obj_t *body,
    const ui_manager_saved_network_t *item,
    lv_event_cb_t connect_saved_cb,
    lv_event_cb_t forget_saved_cb
)
{
    lv_obj_t *card = ui_card_create(body);

    lv_obj_t *row = lv_obj_create(card);
    lv_obj_set_width(row, UI_THEME_CARD_INNER_WIDTH);
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_gap(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        row,
        LV_FLEX_ALIGN_SPACE_BETWEEN,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );

    lv_obj_t *text_box = lv_obj_create(row);
    lv_obj_set_size(text_box, 152, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(text_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(text_box, 0, 0);
    lv_obj_set_style_pad_all(text_box, 0, 0);
    lv_obj_set_style_pad_gap(text_box, 4, 0);
    lv_obj_set_flex_flow(text_box, LV_FLEX_FLOW_COLUMN);

    ui_label_create(text_box, item->ssid, UI_COLOR_TEXT, 148);
    ui_label_create(
        text_box,
        item->connected ? "Connected" : "Saved",
        item->connected ? UI_COLOR_SUCCESS_TEXT : UI_COLOR_DIM,
        148
    );

    lv_obj_t *actions = lv_obj_create(row);
    lv_obj_set_size(actions, 108, 52);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_style_pad_gap(actions, 8, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);

    ui_button_create(
        actions,
        LV_SYMBOL_OK,
        50,
        50,
        UI_COLOR_SUCCESS,
        connect_saved_cb,
        (void *)item->ssid
    );

    ui_button_create(
        actions,
        LV_SYMBOL_CLOSE,
        50,
        50,
        UI_COLOR_DANGER,
        forget_saved_cb,
        (void *)item->ssid
    );
}

void manage_networks_screen_render(
    lv_obj_t *body,
    const ui_manager_state_t *state,
    const ui_manager_callbacks_t *callbacks,
    lv_event_cb_t connect_saved_cb,
    lv_event_cb_t forget_saved_cb,
    lv_event_cb_t portal_toggle_cb
)
{
    (void)callbacks;

    if (state->saved_items_count <= 0) {
        lv_obj_t *card = ui_card_create(body);
        ui_label_create(card, "No saved networks", UI_COLOR_TEXT, UI_THEME_CARD_INNER_WIDTH);
        ui_label_create(card, "Use Connect another network to add one.", UI_COLOR_MUTED, UI_THEME_CARD_INNER_WIDTH);

        ui_button_create(
            body,
            state->portal_active ? "Close captive portal" : "Connect another network",
            UI_THEME_BUTTON_WIDTH,
            UI_THEME_BUTTON_HEIGHT,
            state->portal_active ? UI_COLOR_DANGER : UI_COLOR_PRIMARY,
            portal_toggle_cb,
            NULL
        );

        return;
    }

    for (int i = 0; i < state->saved_items_count; i++) {
        render_network_card(
            body,
            &state->saved_items[i],
            connect_saved_cb,
            forget_saved_cb
        );
    }

    ui_button_create(
        body,
        state->portal_active ? "Close captive portal" : "Add new network",
        UI_THEME_BUTTON_WIDTH,
        UI_THEME_BUTTON_HEIGHT,
        state->portal_active ? UI_COLOR_DANGER : UI_COLOR_PRIMARY,
        portal_toggle_cb,
        NULL
    );
}
