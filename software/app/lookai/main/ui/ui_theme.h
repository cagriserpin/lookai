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
#define UI_THEME_BODY_RIGHT_SCROLL_PADDING 14

/**
 * @brief Width of the top-slice title label.
 */
#define UI_THEME_TITLE_WIDTH 250

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
#define UI_THEME_SETTINGS_ITEM_HEIGHT 72

#define UI_THEME_CARD_RADIUS 22
#define UI_THEME_PILL_RADIUS 999
#define UI_THEME_COMPACT_GAP 10
#define UI_THEME_SECTION_LABEL_HEIGHT 26

/**
 * @brief Common UI colors.
 *
 * The palette is tuned for the 1.75" round AMOLED panel: deep background,
 * slightly lifted surfaces, thin cool borders, and strong but sparse accents.
 */
enum {
    UI_COLOR_BG = 0x030610,
    UI_COLOR_BG_RAISED = 0x080D18,
    UI_COLOR_CARD = 0x101827,
    UI_COLOR_CARD_ALT = 0x151F31,
    UI_COLOR_CARD_PRESSED = 0x1B2940,
    UI_COLOR_BORDER = 0x263448,
    UI_COLOR_BORDER_SOFT = 0x1A2535,
    UI_COLOR_TEXT = 0xF8FAFC,
    UI_COLOR_MUTED = 0xA8B3C7,
    UI_COLOR_DIM = 0x6F7E96,
    UI_COLOR_PRIMARY = 0x3B82F6,
    UI_COLOR_PRIMARY_SOFT = 0x93C5FD,
    UI_COLOR_SECONDARY = 0x1D2738,
    UI_COLOR_SUCCESS = 0x22C55E,
    UI_COLOR_SUCCESS_DARK = 0x14532D,
    UI_COLOR_SUCCESS_TEXT = 0x86EFAC,
    UI_COLOR_DANGER = 0xF43F5E,
    UI_COLOR_WARNING = 0xF59E0B,
    UI_COLOR_WIFI_BLUE = 0x38BDF8,
    UI_COLOR_BRIGHTNESS_ORANGE = 0xF59E0B,
    UI_COLOR_AI_CYAN = 0x38BDF8,
    UI_COLOR_STT_GREEN = 0x22C55E,
    UI_COLOR_TTS_PURPLE = 0xA855F7,
};
