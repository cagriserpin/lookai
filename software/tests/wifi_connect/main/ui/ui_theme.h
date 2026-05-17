/**
 * @file ui/ui_theme.h
 * @brief Shared LVGL UI theme constants tuned for a circular display.
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
 * @brief App bar height.
 *
 * The app bar intentionally stays inside the circular safe area and uses a
 * transparent background.
 */
#define UI_THEME_APPBAR_HEIGHT 64

/**
 * @brief Circular-safe AppBar width.
 *
 * The AppBar is intentionally narrower than the body because it sits near the
 * top of the round display where horizontal safe area is smaller.
 */
#define UI_THEME_APPBAR_WIDTH 286

/**
 * @brief App bar Y offset.
 */
#define UI_THEME_APPBAR_Y 18

/**
 * @brief Scrollable content width.
 *
 * This is narrower than the physical display because the panel is circular.
 * Wider content clips near the top/bottom of the circle.
 */
#define UI_THEME_CONTENT_WIDTH 354

/**
 * @brief Scrollable content height.
 */
#define UI_THEME_CONTENT_HEIGHT 330

/**
 * @brief Scrollable content Y offset.
 */
#define UI_THEME_CONTENT_Y 88

/**
 * @brief Card width inside circular safe area.
 */
#define UI_THEME_CARD_WIDTH 330

/**
 * @brief Inner content width inside cards.
 */
#define UI_THEME_CARD_INNER_WIDTH 292

/**
 * @brief Default full-width button width.
 */
#define UI_THEME_BUTTON_WIDTH 330

/**
 * @brief Default button height.
 */
#define UI_THEME_BUTTON_HEIGHT 54

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
