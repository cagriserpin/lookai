/**
 * @file ui/items/ui_card.h
 * @brief Reusable LVGL card container helper.
 */

#pragma once

#include "lvgl.h"

/**
 * @brief Create a styled card container.
 *
 * @param parent Parent LVGL object.
 * @return Created card object.
 */
lv_obj_t *ui_card_create(lv_obj_t *parent);
