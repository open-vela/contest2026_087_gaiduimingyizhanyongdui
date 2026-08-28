#define _POSIX_C_SOURCE 200809L

#include "behavior.h"
#include "error.h"
#include "lcd.h"
#include "lcd_ui.h"
#include "mimo.h"
#include "session.h"
#include "ui_draw.h"
#include "ui_cjk_font.h"
#include "wifi.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static uint16_t g_framebuffer[LCD_WIDTH * LCD_HEIGHT];
static uint16_t g_camera_frame[320 * 240];
static int g_flush_count;

int lcd_init(void) { return 0; }
uint16_t *lcd_get_framebuffer(void) { return g_framebuffer; }
int lcd_flush(void)
{
  g_flush_count++;
  return 0;
}

bool wifi_is_connected(void) { return true; }
int wifi_http_post(const char *url, const char *json, char *response,
                   size_t response_size)
{
  assert(url != NULL && json != NULL && response != NULL);
  assert(response_size > 0);
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
  session_stats_t stats = {0};
  study_state_t study = {0};
  session_report_t report;
  char advice[256];
  const uint16_t *pixels;

  stats.total_duration_sec = 3661;
  stats.effective_duration_sec = 3000;
  stats.distraction_count = 3;
  stats.focus_score = 82;
  stats.current_mode = MODE_STRICT;
  study.status = FOCUSED;
  study.action = NONE;

  assert(lcd_show_status(DEVICE_MONITORING, &stats, &study) == 0);
  pixels = lcd_debug_framebuffer();
  assert(pixels == g_framebuffer);
  assert(g_flush_count > 0);
  assert(count_color(pixels, UI_RGB565(0, 200, 83)) > 100);
  assert(ui_cjk_glyph(0x8bf7) != NULL); /* 请 */

  /* 预览接口拒绝非监测页面，并将 RGB565 源帧缩放到右上角独立区域。 */
  stats.current_mode = MODE_STRICT;
  assert(lcd_show_status(DEVICE_IDLE, &stats, &study) == 0);
  assert(lcd_show_preview((const uint8_t *)g_camera_frame, 320, 240) ==
         FOCUS_ERR_BUSY);
  for (size_t i = 0; i < sizeof(g_camera_frame) / sizeof(g_camera_frame[0]);
       i++)
    g_camera_frame[i] = (uint16_t)(i & 0xffffU);
  assert(lcd_show_status(DEVICE_MONITORING, &stats, &study) == 0);
  assert(lcd_show_preview((const uint8_t *)g_camera_frame, 320, 240) == 0);
  /* PREVIEW_X=120, PREVIEW_Y=36, width=112, height=84; account for the
   * one-pixel border when checking the interior samples. */
  assert(pixels[37 * LCD_WIDTH + 121] == g_camera_frame[642]);
  assert(pixels[118 * LCD_WIDTH + 230] == g_camera_frame[236 * 320 + 316]);

  stats.current_mode = MODE_GENTLE;
  study.status = PLAYING_PHONE;
  study.milestone_minutes = 28;
  assert(lcd_show_status(DEVICE_MONITORING, &stats, &study) == 0);
  assert(count_color(pixels, UI_RGB565(255, 152, 0)) > 100);

  assert(lcd_show_message("请放下手机!", REMIND) == 0);
  assert(lcd_debug_message_active());
  wait_ms(300);
  assert(!lcd_debug_message_active());

  memset(&report, 0, sizeof(report));
  report.stats = stats;
  report.distraction_by_type[DIST_PLAYING_PHONE] = 2;
  report.distraction_by_type[DIST_GLANCING_PHONE] = 1;
  report.distraction_by_type[DIST_DROWSY] = 1;
  assert(lcd_show_report(&report, NULL) == 0);

  assert(mimo_get_advice(&stats, report.distraction_by_type,
                         advice, sizeof(advice)) == 0);
  assert(advice[0] != '\0');
  puts("focus_aiot host tests passed");
  return 0;
}
