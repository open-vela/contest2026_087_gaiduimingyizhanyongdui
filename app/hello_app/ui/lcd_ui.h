#ifndef FOCUS_AIOT_LCD_UI_H
#define FOCUS_AIOT_LCD_UI_H

#include <stdint.h>

#define LCD_WIDTH  240
#define LCD_HEIGHT 240

#ifdef UI_UNIT_TEST
const uint16_t *lcd_debug_framebuffer(void);
int lcd_debug_message_active(void);
#endif

#endif
