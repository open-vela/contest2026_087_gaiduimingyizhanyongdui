#ifndef FOCUS_AIOT_LCD_H
#define FOCUS_AIOT_LCD_H

#include <stddef.h>
#include <stdint.h>

#include "focus_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define LCD_WIDTH  240
#define LCD_HEIGHT 240

int lcd_init(void);
int lcd_show_status(device_status_t st, session_stats_t *stats,
                    study_state_t *study);
int lcd_show_report(session_report_t *report, const char *advice);
int lcd_show_message(const char *message, action_t type);

/* Optional platform hooks. A board driver may provide strong definitions. */
int st7789v_init(void);
uint16_t *st7789v_get_framebuffer(void);
int st7789v_present(const uint16_t *pixels, int width, int height);

#ifdef UI_UNIT_TEST
const uint16_t *lcd_debug_framebuffer(void);
int lcd_debug_message_active(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
