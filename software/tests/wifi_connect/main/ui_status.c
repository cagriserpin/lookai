#include "ui_status.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "lvgl.h"

#include "menu_controller.h"

static const char *TAG = "ui_status";

static menu_controller_t s_menu;

static ui_status_callbacks_t s_callbacks = {0};

static char s_wifi_status[64] = "Starting";
static char s_wifi_ssid[33] = "";
static char s_wifi_ip[16] = "";
static int s_saved_count = 0;
static bool s_portal_active = false;

static ui_status_saved_network_t s_saved_items[UI_STATUS_MAX_SAVED_NETWORKS] = {0};
static int s_saved_items_count = 0;

static lv_obj_t *get_active_screen(void)
{
#if LVGL_VERSION_MAJOR >= 9
    return lv_screen_active();
#else
    return lv_scr_act();
#endif
}

static lv_obj_t *create_label(lv_obj_t *parent, const char *text, uint32_t color, int width)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text != NULL ? text : "");
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);

    return label;
}

static lv_obj_t *create_button(
    lv_obj_t *parent,
    const char *text,
    int width,
    int height,
    uint32_t color,
    lv_event_cb_t cb,
    void *user_data
)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, width, height);
    lv_obj_set_style_radius(btn, 18, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);

    if (cb != NULL) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    }

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);

    return btn;
}

static lv_obj_t *create_card(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, 398);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(card, 20, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x141925), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2A3448), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_style_pad_gap(card, 10, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    return card;
}

static lv_obj_t *create_content(lv_obj_t *screen)
{
    lv_obj_t *content = lv_obj_create(screen);
    lv_obj_set_size(content, 422, 360);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 88);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 10, 0);
    lv_obj_set_style_pad_gap(content, 12, 0);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);

    return content;
}

static void render_current_unlocked(void);

static void menu_changed_cb(menu_screen_t screen, void *user_ctx)
{
    (void)screen;
    (void)user_ctx;
    render_current_unlocked();
}

static void wifi_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        menu_controller_push(&s_menu, MENU_SCREEN_WIFI);
    }
}

static void back_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        menu_controller_pop(&s_menu);
    }
}

static void manage_saved_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        menu_controller_push(&s_menu, MENU_SCREEN_SAVED_NETWORKS);
    }
}

static void connect_another_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    /*
     * UX requirement:
     * If Add New Network is pressed from a nested screen, return to Wi-Fi status
     * before enabling setup portal.
     */
    menu_controller_show(&s_menu, MENU_SCREEN_WIFI);

    if (s_callbacks.connect_another != NULL) {
        s_callbacks.connect_another();
    }
}

static void connect_saved_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    const char *ssid = (const char *)lv_event_get_user_data(event);
    if (ssid == NULL || ssid[0] == '\0') {
        return;
    }

    menu_controller_show(&s_menu, MENU_SCREEN_WIFI);

    if (s_callbacks.connect_saved != NULL) {
        s_callbacks.connect_saved(ssid);
    }
}

static void forget_network_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    const char *ssid = (const char *)lv_event_get_user_data(event);
    if (ssid == NULL || ssid[0] == '\0') {
        return;
    }

    if (s_callbacks.forget_saved != NULL) {
        s_callbacks.forget_saved(ssid);
    }
}

static void render_header(lv_obj_t *screen, const char *title)
{
    lv_obj_t *header = lv_obj_create(screen);
    lv_obj_set_size(header, 430, 72);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    if (menu_controller_can_go_back(&s_menu)) {
        lv_obj_t *back = create_button(header, "< Back", 100, 44, 0x222B3C, back_event_cb, NULL);
        lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
    }

    lv_obj_t *label = lv_label_create(header);
    lv_label_set_text(label, title);
    lv_obj_set_width(label, menu_controller_can_go_back(&s_menu) ? 290 : 390);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(label, menu_controller_can_go_back(&s_menu) ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_RIGHT_MID, 0, 0);
}

static void render_base(lv_obj_t *screen)
{
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x05070C), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
}

static void render_settings(lv_obj_t *screen)
{
    render_header(screen, "Settings");
    lv_obj_t *content = create_content(screen);

    lv_obj_t *card = create_card(content);
    create_label(card, "Wi-Fi", 0xFFFFFF, 360);

    char status_line[96];
    snprintf(status_line, sizeof(status_line), "Status: %s", s_wifi_status);
    create_label(card, status_line, 0xAAB6C8, 360);

    if (s_wifi_ssid[0] != '\0') {
        char ssid_line[96];
        snprintf(ssid_line, sizeof(ssid_line), "SSID: %s", s_wifi_ssid);
        create_label(card, ssid_line, 0x7F8DA3, 360);
    }

    if (s_portal_active) {
        create_label(card, "Setup portal: LookAI-Setup", 0x37D67A, 360);
        create_label(card, "Portal IP: 192.168.4.1", 0x37D67A, 360);
    }

    create_button(content, "Open Wi-Fi settings", 382, 56, 0x2D7EE8, wifi_button_event_cb, NULL);
}

static void render_wifi(lv_obj_t *screen)
{
    render_header(screen, "Wi-Fi");
    lv_obj_t *content = create_content(screen);

    lv_obj_t *status_card = create_card(content);

    create_label(status_card, "Connection status", 0xFFFFFF, 360);

    char status_line[96];
    snprintf(status_line, sizeof(status_line), "Status: %s", s_wifi_status);
    create_label(status_card, status_line, 0xC8D4E8, 360);

    char ssid_line[96];
    snprintf(ssid_line, sizeof(ssid_line), "SSID: %s", s_wifi_ssid[0] != '\0' ? s_wifi_ssid : "-");
    create_label(status_card, ssid_line, 0xAAB6C8, 360);

    char ip_line[64];
    snprintf(ip_line, sizeof(ip_line), "IP: %s", s_wifi_ip[0] != '\0' ? s_wifi_ip : "-");
    create_label(status_card, ip_line, 0xAAB6C8, 360);

    char saved_line[64];
    snprintf(saved_line, sizeof(saved_line), "Saved networks: %d", s_saved_count);
    create_label(status_card, saved_line, 0x7F8DA3, 360);

    if (s_portal_active) {
        lv_obj_t *portal_card = create_card(content);
        create_label(portal_card, "Setup portal active", 0x37D67A, 360);
        create_label(portal_card, "Wi-Fi: LookAI-Setup", 0xC8D4E8, 360);
        create_label(portal_card, "Password: 12345678", 0xC8D4E8, 360);
        create_label(portal_card, "IP: 192.168.4.1", 0xC8D4E8, 360);
    }

    create_button(content, "Manage saved networks", 382, 56, 0x222B3C, manage_saved_event_cb, NULL);
    create_button(content, "Connect another network", 382, 56, 0x2D7EE8, connect_another_event_cb, NULL);
}

static void render_saved_network_card(lv_obj_t *content, const ui_status_saved_network_t *item)
{
    lv_obj_t *card = create_card(content);

    lv_obj_t *row = lv_obj_create(card);
    lv_obj_set_width(row, 360);
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_gap(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *text_box = lv_obj_create(row);
    lv_obj_set_size(text_box, 230, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(text_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(text_box, 0, 0);
    lv_obj_set_style_pad_all(text_box, 0, 0);
    lv_obj_set_style_pad_gap(text_box, 4, 0);
    lv_obj_set_flex_flow(text_box, LV_FLEX_FLOW_COLUMN);

    create_label(text_box, item->ssid, 0xFFFFFF, 220);
    create_label(text_box, item->connected ? "Currently connected" : "Saved network", item->connected ? 0x37D67A : 0x7F8DA3, 220);

    lv_obj_t *actions = lv_obj_create(row);
    lv_obj_set_size(actions, 108, 52);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_style_pad_gap(actions, 8, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);

    create_button(actions, LV_SYMBOL_OK, 50, 50, 0x1F9D55, connect_saved_event_cb, (void *)item->ssid);
    create_button(actions, LV_SYMBOL_CLOSE, 50, 50, 0xC9344A, forget_network_event_cb, (void *)item->ssid);
}

static void render_saved(lv_obj_t *screen)
{
    render_header(screen, "Saved Wi-Fi");
    lv_obj_t *content = create_content(screen);

    if (s_saved_items_count <= 0) {
        lv_obj_t *card = create_card(content);
        create_label(card, "No saved networks", 0xFFFFFF, 360);
        create_label(card, "Use Connect another network to add one.", 0xAAB6C8, 360);
        create_button(content, "Connect another network", 382, 56, 0x2D7EE8, connect_another_event_cb, NULL);
        return;
    }

    for (int i = 0; i < s_saved_items_count; i++) {
        render_saved_network_card(content, &s_saved_items[i]);
    }

    create_button(content, "Add new network", 382, 56, 0x2D7EE8, connect_another_event_cb, NULL);
}

static void render_current_unlocked(void)
{
    lv_obj_t *screen = get_active_screen();
    render_base(screen);

    menu_screen_t screen_id = menu_controller_current(&s_menu);

    if (screen_id == MENU_SCREEN_SETTINGS) {
        render_settings(screen);
    } else if (screen_id == MENU_SCREEN_WIFI) {
        render_wifi(screen);
    } else {
        render_saved(screen);
    }
}

static void render_current_locked(void)
{
    if (bsp_display_lock(1000) != ESP_OK) {
        return;
    }

    render_current_unlocked();

    bsp_display_unlock();
}

esp_err_t ui_status_init(void)
{
    ESP_LOGI(TAG, "Initializing display UI");

    lv_display_t *display = bsp_display_start();
    if (display == NULL) {
        ESP_LOGE(TAG, "Display init failed");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(bsp_display_backlight_on());

    /*
     * menu_controller_init() triggers the initial render through its changed
     * callback, so it must run while the LVGL/display lock is held.
     */
    if (bsp_display_lock(1000) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to lock display for initial render");
        return ESP_FAIL;
    }

    menu_controller_init(&s_menu, MENU_SCREEN_SETTINGS, menu_changed_cb, NULL);

    bsp_display_unlock();

    return ESP_OK;
}

void ui_status_set_callbacks(const ui_status_callbacks_t *callbacks)
{
    if (callbacks == NULL) {
        memset(&s_callbacks, 0, sizeof(s_callbacks));
        return;
    }

    s_callbacks = *callbacks;
}

void ui_status_show_settings(void)
{
    if (bsp_display_lock(1000) != ESP_OK) {
        return;
    }

    menu_controller_reset(&s_menu, MENU_SCREEN_SETTINGS);

    bsp_display_unlock();
}

void ui_status_show_wifi(void)
{
    if (bsp_display_lock(1000) != ESP_OK) {
        return;
    }

    menu_controller_reset(&s_menu, MENU_SCREEN_SETTINGS);
    menu_controller_push(&s_menu, MENU_SCREEN_WIFI);

    bsp_display_unlock();
}

void ui_status_update_wifi_status(
    const char *status,
    const char *ssid,
    const char *ip,
    int saved_count,
    bool portal_active
)
{
    if (status != NULL) {
        strncpy(s_wifi_status, status, sizeof(s_wifi_status) - 1);
        s_wifi_status[sizeof(s_wifi_status) - 1] = '\0';
    }

    if (ssid != NULL) {
        strncpy(s_wifi_ssid, ssid, sizeof(s_wifi_ssid) - 1);
        s_wifi_ssid[sizeof(s_wifi_ssid) - 1] = '\0';
    }

    if (ip != NULL) {
        strncpy(s_wifi_ip, ip, sizeof(s_wifi_ip) - 1);
        s_wifi_ip[sizeof(s_wifi_ip) - 1] = '\0';
    }

    s_saved_count = saved_count;
    s_portal_active = portal_active;

    render_current_locked();
}

void ui_status_set_saved_networks(
    const ui_status_saved_network_t *items,
    int count
)
{
    if (count < 0) {
        count = 0;
    }

    if (count > UI_STATUS_MAX_SAVED_NETWORKS) {
        count = UI_STATUS_MAX_SAVED_NETWORKS;
    }

    memset(s_saved_items, 0, sizeof(s_saved_items));
    s_saved_items_count = count;
    s_saved_count = count;

    for (int i = 0; i < count; i++) {
        strncpy(s_saved_items[i].ssid, items[i].ssid, sizeof(s_saved_items[i].ssid) - 1);
        s_saved_items[i].ssid[sizeof(s_saved_items[i].ssid) - 1] = '\0';
        s_saved_items[i].connected = items[i].connected;
    }

    render_current_locked();
}
