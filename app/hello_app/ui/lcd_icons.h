#ifndef FOCUS_AIOT_LCD_ICONS_H
#define FOCUS_AIOT_LCD_ICONS_H

#include "../api/behavior.h"
#include "ui_draw.h"

void lcd_draw_status_icon(ui_surface_t *surface, int x, int y,
                          uint8_t mode, status_t status);

#endif
