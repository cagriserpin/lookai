/**
 * @file ui/items/ui_label.h
 * @brief Reusable LVGL label helpers.
 */

#pragma once

#include <stdint.h>

#include "lvgl.h"

/**
 * @brief Create a wrapped left-aligned label.
 *
 * @param parent Parent LVGL object.
 * @param text Initial label text.
 * @param color RGB color value.
 * @param width Label width.
 * @return Created label object.
 */
lv_obj_t *ui_label_create(lv_obj_t *parent, const char *text, uint32_t color, int width);
