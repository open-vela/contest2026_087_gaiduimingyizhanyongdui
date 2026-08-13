/****************************************************************************
 * Temporary framebuffer backend. The board ST7789V driver overrides these
 * weak symbols when its implementation is linked into the application.
 ****************************************************************************/

#include "../api/error.h"
#include "../api/lcd.h"

#include <stdint.h>

#define LCD_STUB_WIDTH  240
#define LCD_STUB_HEIGHT 240

static uint16_t g_lcd_stub_framebuffer[LCD_STUB_WIDTH * LCD_STUB_HEIGHT];

#if defined(__GNUC__) && !defined(_WIN32)
#  define FOCUS_WEAK __attribute__((weak))
#else
#  define FOCUS_WEAK
#endif

FOCUS_WEAK int lcd_init(void)
{
  return FOCUS_OK;
}

FOCUS_WEAK int lcd_flush(void)
{
  return FOCUS_OK;
}

FOCUS_WEAK uint16_t *lcd_get_framebuffer(void)
{
  return g_lcd_stub_framebuffer;
}
