/**
 * @file ui/items/ui_loading_dots.h
 * @brief Small reusable three-dot loading indicator for lightweight UI states.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

lv_obj_t *ui_loading_dots_create(lv_obj_t *parent, uint32_t color);
void ui_loading_dots_set_active(lv_obj_t *dots, bool active);
void ui_loading_dots_set_color(lv_obj_t *dots, uint32_t color);
