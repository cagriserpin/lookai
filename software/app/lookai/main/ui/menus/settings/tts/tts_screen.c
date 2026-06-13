/**
 * @file ui/menus/settings/tts/tts_screen.c
 * @brief Text-to-speech UI body implementation.
 */

#include "tts_screen.h"

#include "ui_card.h"
#include "ui_theme.h"

#define TTS_COLOR_PURPLE 0xA855F7
#define TTS_COLOR_PURPLE_DARK 0x6D28D9
#define TTS_CONTAINER_WIDTH UI_THEME_CARD_WIDTH
#define TTS_CONTAINER_HEIGHT 330
#define TTS_STATUS_WIDTH (UI_THEME_CARD_WIDTH - 34)
#define TTS_CARD_HEIGHT 76

static void style_plain_container(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *create_status_label(lv_obj_t *parent)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, "Ready\nSelect a sample text.");
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, TTS_STATUS_WIDTH);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(label, 4, 0);

    return label;
}

static lv_obj_t *create_sample_card(lv_obj_t *parent, const char *text)
{
    lv_obj_t *card = ui_card_create(parent);

    lv_obj_set_height(card, TTS_CARD_HEIGHT);
    lv_obj_set_style_border_color(card, lv_color_hex(TTS_COLOR_PURPLE), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_shadow_width(card, 10, 0);
    lv_obj_set_style_shadow_spread(card, 1, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(TTS_COLOR_PURPLE_DARK), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);
    lv_obj_set_flex_align(
        card,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *label = lv_label_create(card);
    lv_label_set_text(label, text != NULL ? text : "");
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, UI_THEME_CARD_INNER_WIDTH);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

    return card;
}

void tts_screen_render(lv_obj_t *body)
{
    /*
     * TTS page is also fixed for now. It has status text and two sample cards.
     */
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(body, LV_DIR_NONE);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *container = lv_obj_create(body);

    lv_obj_set_size(container, TTS_CONTAINER_WIDTH, TTS_CONTAINER_HEIGHT);
    style_plain_container(container);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        container,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );
    lv_obj_set_style_pad_gap(container, 18, 0);

    create_status_label(container);

    create_sample_card(
        container,
        "Merhaba! Ben LookAI"
    );

    create_sample_card(
        container,
        "Sana nasil yardimci olabilirim?"
    );
}
