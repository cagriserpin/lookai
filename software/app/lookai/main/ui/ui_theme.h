/**
 * @file ui/ui_theme.h
 * @brief Shared LVGL UI theme constants for circular scaffold layout.
 */

#pragma once

#include <stdint.h>

/**
 * @brief Physical round AMOLED display width.
 */
#define UI_THEME_SCREEN_WIDTH 466

/**
 * @brief Physical round AMOLED display height.
 */
#define UI_THEME_SCREEN_HEIGHT 466

/**
 * @brief Thickness of each outer slice around the central body.
 */
#define UI_THEME_SLICE_SIZE 74

/**
 * @brief Width of the central UI body rectangle.
 */
#define UI_THEME_BODY_WIDTH 318

/**
 * @brief Height of the central UI body rectangle when bottom slice is used.
 */
#define UI_THEME_BODY_HEIGHT 318

/**
 * @brief Height of the body when no bottom slice is used.
 */
#define UI_THEME_BODY_HEIGHT_NO_BOTTOM (UI_THEME_BODY_HEIGHT + UI_THEME_SLICE_SIZE)

/**
 * @brief Extra bottom padding inside scrollable body.
 *
 * This lets the last content item scroll above the round display's lower edge.
 */
#define UI_THEME_BODY_BOTTOM_SCROLL_PADDING UI_THEME_SLICE_SIZE

/**
 * @brief Extra right padding inside scrollable body.
 *
 * This keeps content away from the vertical scrollbar.
 */
#define UI_THEME_BODY_RIGHT_SCROLL_PADDING 18

/**
 * @brief Width of the top-slice title label.
 */
#define UI_THEME_TITLE_WIDTH 248

/**
 * @brief Card width inside the central UI body.
 */
#define UI_THEME_CARD_WIDTH 292

/**
 * @brief Inner content width inside cards.
 */
#define UI_THEME_CARD_INNER_WIDTH 260

/**
 * @brief Default full-width button width.
 */
#define UI_THEME_BUTTON_WIDTH 292

/**
 * @brief Default button height.
 */
#define UI_THEME_BUTTON_HEIGHT 54

/**
 * @brief Settings row height.
 */
#define UI_THEME_SETTINGS_ITEM_HEIGHT 76

/**
 * @brief Common UI colors.
 */
enum {
    UI_COLOR_BG = 0x05070C,
    UI_COLOR_CARD = 0x141925,
    UI_COLOR_BORDER = 0x2A3448,
    UI_COLOR_TEXT = 0xFFFFFF,
    UI_COLOR_MUTED = 0xAAB6C8,
    UI_COLOR_DIM = 0x7F8DA3,
    UI_COLOR_PRIMARY = 0x2D7EE8,
    UI_COLOR_SECONDARY = 0x222B3C,
    UI_COLOR_SUCCESS = 0x1F9D55,
    UI_COLOR_SUCCESS_TEXT = 0x37D67A,
    UI_COLOR_DANGER = 0xC9344A,
    UI_COLOR_WIFI_BLUE = 0x2D7EE8,
    UI_COLOR_BRIGHTNESS_ORANGE = 0xF59E0B,
};
