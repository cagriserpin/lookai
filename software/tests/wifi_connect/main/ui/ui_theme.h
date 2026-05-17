/**
 * @file ui/ui_theme.h
 * @brief Shared LVGL UI theme constants.
 */

#pragma once

#include <stdint.h>

/**
 * @brief Width of the round AMOLED display content area used by our layouts.
 */
#define UI_THEME_SCREEN_WIDTH 466

/**
 * @brief Height of the round AMOLED display content area used by our layouts.
 */
#define UI_THEME_SCREEN_HEIGHT 466

/**
 * @brief Scrollable content width used by menu screens.
 */
#define UI_THEME_CONTENT_WIDTH 422

/**
 * @brief Card width used by menu screens.
 */
#define UI_THEME_CARD_WIDTH 398

/**
 * @brief Default full-width button width.
 */
#define UI_THEME_BUTTON_WIDTH 382

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
};
