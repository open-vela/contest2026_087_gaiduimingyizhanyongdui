#define _POSIX_C_SOURCE 200809L

#include "lcd.h"
#include "mimo.h"
#include "ui_draw.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

int st7789v_init(void) { return 0; }
uint16_t *st7789v_get_framebuffer(void) { return NULL; }
int st7789v_present(const uint16_t *pixels, int width, int height)
{
  assert(pixels != NULL);
  assert(width == LCD_WIDTH && height == LCD_HEIGHT);
  return 0;
}
int mimo_http_post(const char *url, const char *json, char *response,
                   size_t response_size, unsigned int timeout_ms)
{
  assert(url != NULL && json != NULL && response != NULL);
  assert(response_size > 0 && timeout_ms == 5000);
  return -1;
}

static void wait_ms(unsigned int ms)
{
  struct timespec delay = {ms / 1000U, (long)(ms % 1000U) * 1000000L};
  nanosleep(&delay, NULL);
}

static size_t count_color(const uint16_t *pixels, uint16_t color)
{
  size_t count = 0;
  size_t index;
  for (index = 0; index < LCD_WIDTH * LCD_HEIGHT; index++)
    if (pixels[index] == color) count++;
  return count;
}

int main(void)
{
  session_stats_t stats = {0, 3661, 3000, 3, 82, 0};
  study_state_t study = {FOCUSED, NONE, "", false, 0, 0};
  session_report_t report;
  char advice[256];
  const uint16_t *pixels;

  assert(lcd_init() == 0);
  assert(lcd_show_status(DEVICE_MONITORING, &stats, &study) == 0);
  pixels = lcd_debug_framebuffer();
  assert(pixels != NULL);
  assert(count_color(pixels, UI_RGB565(0, 200, 83)) > 100);

  stats.current_mode = 1;
  study.status = PLAYING_PHONE;
  study.milestone_minutes = 28;
  assert(lcd_show_status(DEVICE_MONITORING, &stats, &study) == 0);
  assert(count_color(pixels, UI_RGB565(255, 152, 0)) > 100);

  assert(lcd_show_message("PUT DOWN THE PHONE", REMIND) == 0);
  assert(lcd_debug_message_active());
  wait_ms(300);
  assert(!lcd_debug_message_active());

  memset(&report, 0, sizeof(report));
  report.stats = stats;
  report.distraction_by_type[0] = 2;
  report.distraction_by_type[1] = 1;
  report.distraction_by_type[3] = 1;
  assert(lcd_show_report(&report, NULL) == 0);

  assert(mimo_get_advice(&stats, report.distraction_by_type,
                         advice, sizeof(advice)) == 0);
  assert(advice[0] != '\0');
  puts("focus_aiot host tests passed");
  return 0;
}
