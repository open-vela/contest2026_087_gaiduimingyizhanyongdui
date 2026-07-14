#define _POSIX_C_SOURCE 200809L

#include "lcd.h"

#include "lcd_icons.h"
#include "ui_draw.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef UI_MESSAGE_DURATION_MS
#  define UI_MESSAGE_DURATION_MS 3000U
#endif

#ifndef UI_ANIMATION_STEP_MS
#  define UI_ANIMATION_STEP_MS 100U
#endif

#if defined(__GNUC__)
extern int st7789v_init(void) __attribute__((weak));
extern uint16_t *st7789v_get_framebuffer(void) __attribute__((weak));
extern int st7789v_present(const uint16_t *, int, int) __attribute__((weak));
#endif

enum ui_screen_e
{
  UI_SCREEN_STATUS,
  UI_SCREEN_REPORT
};

struct ui_context_s
{
  pthread_mutex_t lock;
  pthread_cond_t wake;
  ui_surface_t surface;
  int initialized;
  int worker_started;
  enum ui_screen_e screen;
  device_status_t device_status;
  session_stats_t stats;
  study_state_t study;
  session_report_t report;
  char advice[512];
  int message_active;
  action_t message_type;
  char message[128];
  uint64_t message_started_ms;
  unsigned int message_generation;
};

static uint16_t g_fallback_framebuffer[LCD_WIDTH * LCD_HEIGHT];
static struct ui_context_s g_ui =
{
  .lock = PTHREAD_MUTEX_INITIALIZER,
  .wake = PTHREAD_COND_INITIALIZER
};

static const uint16_t COLOR_BACKGROUND = UI_RGB565(9, 16, 28);
static const uint16_t COLOR_PANEL = UI_RGB565(19, 30, 48);
static const uint16_t COLOR_TEXT = UI_RGB565(238, 244, 255);
static const uint16_t COLOR_MUTED = UI_RGB565(145, 160, 181);
static const uint16_t COLOR_GREEN = UI_RGB565(0, 200, 83);
static const uint16_t COLOR_ORANGE = UI_RGB565(255, 152, 0);
static const uint16_t COLOR_RED = UI_RGB565(213, 0, 0);

static uint64_t monotonic_ms(void)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

static void sleep_ms(unsigned int milliseconds)
{
  struct timespec delay;
  delay.tv_sec = milliseconds / 1000U;
  delay.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
  nanosleep(&delay, NULL);
}

static void present_locked(void)
{
  if (st7789v_present != NULL)
    {
      (void)st7789v_present(g_ui.surface.pixels, LCD_WIDTH, LCD_HEIGHT);
    }
}

static uint8_t clamp_score(uint8_t score)
{
  return score > 100 ? 100 : score;
}

static const char *mode_name(uint8_t mode)
{
  return mode == 0 ? "STRICT" : "GENTLE";
}

static const char *status_name(status_t status)
{
  static const char *const names[] =
  {
    "FOCUSED", "GLANCE", "PHONE", "AWAY", "DROWSY"
  };
  return status >= FOCUSED && status <= DROWSY ? names[status] : "UNKNOWN";
}

static void format_duration(char *output, size_t output_size, uint32_t seconds)
{
  unsigned long hours = (unsigned long)(seconds / 3600U);
  unsigned int minutes = (unsigned int)((seconds / 60U) % 60U);
  unsigned int remaining = (unsigned int)(seconds % 60U);
  snprintf(output, output_size, "%02lu:%02u:%02u", hours, minutes, remaining);
}

static void draw_progress(ui_surface_t *surface, int x, int y, int width,
                          int height, unsigned int value, unsigned int maximum,
                          uint16_t color)
{
  unsigned int bounded = value > maximum ? maximum : value;
  int fill = maximum == 0 ? 0 : (int)((unsigned long)width * bounded / maximum);
  ui_fill_rect(surface, x, y, width, height, UI_RGB565(45, 58, 76));
  ui_fill_rect(surface, x, y, fill, height, color);
  ui_rect(surface, x, y, width, height, 1, UI_RGB565(79, 94, 115));
}

static void draw_header(ui_surface_t *surface, const char *left,
                        const char *right, uint16_t accent)
{
  ui_fill_rect(surface, 0, 0, LCD_WIDTH, 28, COLOR_PANEL);
  ui_fill_rect(surface, 0, 27, LCD_WIDTH, 1, accent);
  ui_text(surface, 8, 9, left, 1, accent);
  if (right != NULL)
    {
      ui_text(surface, LCD_WIDTH - ui_text_width(right, 1) - 8, 9,
              right, 1, COLOR_TEXT);
    }
}

static void render_idle(ui_surface_t *surface)
{
  ui_clear(surface, COLOR_BACKGROUND);
  ui_text_center(surface, LCD_WIDTH / 2, 38, "FOCUS AIOT", 3, COLOR_GREEN);
  ui_circle(surface, LCD_WIDTH / 2, 119, 38, 3, COLOR_GREEN);
  ui_text_center(surface, LCD_WIDTH / 2, 105, "AI", 4, COLOR_TEXT);
  ui_line(surface, 102, 154, 138, 154, COLOR_GREEN);
  ui_text_center(surface, LCD_WIDTH / 2, 191, "HOLD START TO BEGIN", 1,
                 COLOR_MUTED);
}

static void render_mode_select(ui_surface_t *surface,
                               const session_stats_t *stats)
{
  uint8_t selected = stats->current_mode == 0 ? 0 : 1;
  ui_clear(surface, COLOR_BACKGROUND);
  draw_header(surface, "SELECT MODE", NULL, COLOR_GREEN);

  ui_fill_rect(surface, 14, 52, 100, 94, COLOR_PANEL);
  ui_fill_rect(surface, 126, 52, 100, 94, COLOR_PANEL);
  ui_rect(surface, 14, 52, 100, 94, selected == 0 ? 3 : 1,
          selected == 0 ? COLOR_GREEN : COLOR_MUTED);
  ui_rect(surface, 126, 52, 100, 94, selected == 1 ? 3 : 1,
          selected == 1 ? COLOR_GREEN : COLOR_MUTED);
  ui_text_center(surface, 64, 77, "STRICT", 2,
                 selected == 0 ? COLOR_GREEN : COLOR_TEXT);
  ui_text_center(surface, 176, 77, "GENTLE", 2,
                 selected == 1 ? COLOR_GREEN : COLOR_TEXT);
  ui_text_center(surface, 64, 115, "SUPERVISE", 1, COLOR_MUTED);
  ui_text_center(surface, 176, 115, "COMPANION", 1, COLOR_MUTED);
  ui_text_center(surface, LCD_WIDTH / 2, 174, "MODE KEY: SWITCH", 1,
                 COLOR_MUTED);
  ui_text_center(surface, LCD_WIDTH / 2, 196, "START KEY: CONFIRM", 1,
                 COLOR_TEXT);
}

static void render_monitoring(ui_surface_t *surface,
                              const session_stats_t *stats,
                              const study_state_t *study)
{
  char total[24];
  char effective[24];
  char line[48];
  uint8_t mode = stats->current_mode == 0 ? 0 : 1;
  uint16_t accent = mode == 0 ? COLOR_GREEN : COLOR_ORANGE;
  unsigned int milestone = study->milestone_minutes > 0 ?
                           (unsigned int)study->milestone_minutes : 0U;
  unsigned int milestone_progress = milestone % 30U;
  unsigned int next_milestone = milestone + (30U - milestone_progress);

  format_duration(total, sizeof(total), stats->total_duration_sec);
  format_duration(effective, sizeof(effective), stats->effective_duration_sec);
  ui_clear(surface, COLOR_BACKGROUND);
  draw_header(surface, mode_name(mode), total, accent);
  lcd_draw_status_icon(surface, 80, 38, mode, study->status);
  ui_text_center(surface, LCD_WIDTH / 2, 123, status_name(study->status), 1,
                 accent);

  snprintf(line, sizeof(line), "EFFECTIVE  %s", effective);
  ui_text(surface, 12, 143, line, 1, COLOR_TEXT);
  snprintf(line, sizeof(line), "DISTRACTIONS  %u", stats->distraction_count);
  ui_text(surface, 12, 159, line, 1, COLOR_TEXT);
  snprintf(line, sizeof(line), "FOCUS %3u", clamp_score(stats->focus_score));
  ui_text(surface, 12, 178, line, 1, COLOR_TEXT);
  draw_progress(surface, 78, 179, 148, 8, clamp_score(stats->focus_score),
                100, accent);

  if (mode != 0)
    {
      if (study->milestone_reached)
        {
          snprintf(line, sizeof(line), "MILESTONE %u MIN!", milestone);
          ui_text_center(surface, LCD_WIDTH / 2, 198, line, 1, COLOR_GREEN);
        }
      else
        {
          snprintf(line, sizeof(line), "NEXT %u MIN", next_milestone);
          ui_text(surface, 12, 199, line, 1, COLOR_MUTED);
        }
      draw_progress(surface, 12, 216, 214, 8, milestone_progress, 30,
                    COLOR_ORANGE);
    }
  else
    {
      ui_text_center(surface, LCD_WIDTH / 2, 211,
                     "STAY PRESENT. KEEP GOING.", 1, COLOR_MUTED);
    }
}

static void render_status_locked(void)
{
  switch (g_ui.device_status)
    {
      case DEVICE_IDLE:
        render_idle(&g_ui.surface);
        break;
      case DEVICE_MODE_SELECT:
        render_mode_select(&g_ui.surface, &g_ui.stats);
        break;
      case DEVICE_MONITORING:
        render_monitoring(&g_ui.surface, &g_ui.stats, &g_ui.study);
        break;
      case DEVICE_REPORT:
      default:
        render_idle(&g_ui.surface);
        break;
    }
}

static const char *score_grade(uint8_t score)
{
  if (score >= 90) return "EXCELLENT";
  if (score >= 75) return "GOOD";
  if (score >= 60) return "FAIR";
  return "KEEP TRYING";
}

static void draw_advice_lines(ui_surface_t *surface, const char *advice)
{
  char line[34];
  size_t length;
  size_t offset = 0;
  int y = 192;

  if (advice == NULL || advice[0] == '\0') advice = "ADVICE IS LOADING...";
  length = strlen(advice);
  while (offset < length && y <= 224)
    {
      size_t count = length - offset > 32 ? 32 : length - offset;
      while (count > 1 && offset + count < length &&
             (advice[offset + count] & 0xc0) == 0x80)
        {
          count--;
        }
      memcpy(line, advice + offset, count);
      line[count] = '\0';
      ui_text(surface, 12, y, line, 1, COLOR_TEXT);
      offset += count;
      y += 13;
    }
}

static void render_report_locked(void)
{
  char line[64];
  const session_report_t *report = &g_ui.report;
  uint16_t accent = report->stats.current_mode == 0 ? COLOR_GREEN : COLOR_ORANGE;

  ui_clear(&g_ui.surface, COLOR_BACKGROUND);
  draw_header(&g_ui.surface, "STUDY REPORT", mode_name(report->stats.current_mode),
              accent);
  snprintf(line, sizeof(line), "TOTAL       %luh %02lum",
           (unsigned long)(report->stats.total_duration_sec / 3600U),
           (unsigned long)((report->stats.total_duration_sec / 60U) % 60U));
  ui_text(&g_ui.surface, 12, 39, line, 1, COLOR_TEXT);
  snprintf(line, sizeof(line), "EFFECTIVE   %luh %02lum",
           (unsigned long)(report->stats.effective_duration_sec / 3600U),
           (unsigned long)((report->stats.effective_duration_sec / 60U) % 60U));
  ui_text(&g_ui.surface, 12, 55, line, 1, COLOR_TEXT);

  ui_text(&g_ui.surface, 12, 79, "DISTRACTION DETAILS", 1, accent);
  snprintf(line, sizeof(line), "PLAY PHONE %u   GLANCE %u",
           report->distraction_by_type[0], report->distraction_by_type[1]);
  ui_text(&g_ui.surface, 12, 96, line, 1, COLOR_TEXT);
  snprintf(line, sizeof(line), "AWAY %u         DROWSY %u",
           report->distraction_by_type[2], report->distraction_by_type[3]);
  ui_text(&g_ui.surface, 12, 112, line, 1, COLOR_TEXT);

  snprintf(line, sizeof(line), "FOCUS %u  %s",
           clamp_score(report->stats.focus_score),
           score_grade(clamp_score(report->stats.focus_score)));
  ui_text(&g_ui.surface, 12, 138, line, 1, accent);
  draw_progress(&g_ui.surface, 12, 154, 214, 8,
                clamp_score(report->stats.focus_score), 100, accent);
  ui_text_center(&g_ui.surface, LCD_WIDTH / 2, 174, "-- MIMO ADVICE --", 1,
                 COLOR_MUTED);
  draw_advice_lines(&g_ui.surface, g_ui.advice);
  ui_text_center(&g_ui.surface, LCD_WIDTH / 2, 229, "START: RETURN", 1,
                 COLOR_MUTED);
}

static void render_message_locked(uint64_t elapsed_ms)
{
  uint16_t background;
  uint16_t foreground = COLOR_TEXT;
  if (g_ui.message_type == REMIND)
    {
      background = ((elapsed_ms / 500U) % 2U) == 0 ?
                   COLOR_RED : UI_RGB565(92, 0, 8);
    }
  else
    {
      unsigned int phase = (unsigned int)((elapsed_ms / 50U) % 20U);
      unsigned int green = 72U + (phase <= 10U ? phase : 20U - phase) * 8U;
      background = UI_RGB565(0, green, 72);
    }

  ui_clear(&g_ui.surface, background);
  if (g_ui.message_type == REMIND)
    {
      ui_line(&g_ui.surface, 120, 48, 84, 112, foreground);
      ui_line(&g_ui.surface, 84, 112, 156, 112, foreground);
      ui_line(&g_ui.surface, 156, 112, 120, 48, foreground);
      ui_fill_rect(&g_ui.surface, 118, 70, 5, 22, foreground);
      ui_fill_circle(&g_ui.surface, 120, 101, 3, foreground);
    }
  else
    {
      ui_circle(&g_ui.surface, 120, 80, 34, 3, foreground);
      ui_text_center(&g_ui.surface, 120, 65, "*", 4, foreground);
    }
  ui_text_center(&g_ui.surface, LCD_WIDTH / 2, 139, g_ui.message, 1,
                 foreground);
  ui_text_center(&g_ui.surface, LCD_WIDTH / 2, 176,
                 g_ui.message_type == REMIND ? "PLEASE REFOCUS" : "GREAT JOB",
                 2, foreground);
}

static void *ui_worker(void *argument)
{
  unsigned int observed_generation = 0;
  (void)argument;

  for (;;)
    {
      uint64_t elapsed;
      pthread_mutex_lock(&g_ui.lock);
      while (!g_ui.message_active)
        {
          pthread_cond_wait(&g_ui.wake, &g_ui.lock);
        }
      observed_generation = g_ui.message_generation;
      elapsed = monotonic_ms() - g_ui.message_started_ms;
      if (elapsed >= UI_MESSAGE_DURATION_MS)
        {
          if (observed_generation == g_ui.message_generation)
            {
              g_ui.message_active = 0;
              if (g_ui.screen == UI_SCREEN_REPORT) render_report_locked();
              else render_status_locked();
              present_locked();
            }
          pthread_mutex_unlock(&g_ui.lock);
          continue;
        }

      render_message_locked(elapsed);
      present_locked();
      pthread_mutex_unlock(&g_ui.lock);
      sleep_ms(UI_ANIMATION_STEP_MS);
    }

  return NULL;
}

int lcd_init(void)
{
  pthread_t worker;
  uint16_t *framebuffer = NULL;
  int ret = 0;

  pthread_mutex_lock(&g_ui.lock);
  if (g_ui.initialized)
    {
      pthread_mutex_unlock(&g_ui.lock);
      return 0;
    }

  if (st7789v_init != NULL)
    {
      ret = st7789v_init();
      if (ret < 0)
        {
          pthread_mutex_unlock(&g_ui.lock);
          return ret;
        }
    }
  if (st7789v_get_framebuffer != NULL)
    {
      framebuffer = st7789v_get_framebuffer();
    }
  if (framebuffer == NULL)
    {
      framebuffer = g_fallback_framebuffer;
    }

  g_ui.surface.pixels = framebuffer;
  g_ui.surface.width = LCD_WIDTH;
  g_ui.surface.height = LCD_HEIGHT;
  g_ui.surface.stride = LCD_WIDTH;
  g_ui.screen = UI_SCREEN_STATUS;
  g_ui.device_status = DEVICE_IDLE;
  g_ui.initialized = 1;
  render_idle(&g_ui.surface);
  present_locked();
  pthread_mutex_unlock(&g_ui.lock);

  ret = pthread_create(&worker, NULL, ui_worker, NULL);
  if (ret != 0)
    {
      return -ret;
    }
  pthread_detach(worker);

  pthread_mutex_lock(&g_ui.lock);
  g_ui.worker_started = 1;
  pthread_mutex_unlock(&g_ui.lock);
  return 0;
}

int lcd_show_status(device_status_t status, session_stats_t *stats,
                    study_state_t *study)
{
  if (!g_ui.initialized)
    {
      int ret = lcd_init();
      if (ret < 0) return ret;
    }
  if (stats == NULL || study == NULL) return -EINVAL;
  if (status < DEVICE_IDLE || status > DEVICE_REPORT) return -EINVAL;

  pthread_mutex_lock(&g_ui.lock);
  g_ui.screen = UI_SCREEN_STATUS;
  g_ui.device_status = status;
  g_ui.stats = *stats;
  g_ui.study = *study;
  if (status != DEVICE_MONITORING)
    {
      g_ui.message_active = 0;
      g_ui.message_generation++;
    }
  if (!g_ui.message_active)
    {
      render_status_locked();
      present_locked();
    }
  pthread_mutex_unlock(&g_ui.lock);
  return 0;
}

int lcd_show_report(session_report_t *report, const char *advice)
{
  if (!g_ui.initialized)
    {
      int ret = lcd_init();
      if (ret < 0) return ret;
    }
  if (report == NULL) return -EINVAL;

  pthread_mutex_lock(&g_ui.lock);
  g_ui.screen = UI_SCREEN_REPORT;
  g_ui.report = *report;
  g_ui.message_active = 0;
  g_ui.message_generation++;
  if (advice == NULL || advice[0] == '\0') advice = report->advice;
  if (advice == NULL || advice[0] == '\0') advice = "ADVICE IS LOADING...";
  snprintf(g_ui.advice, sizeof(g_ui.advice), "%s", advice);
  render_report_locked();
  present_locked();
  pthread_mutex_unlock(&g_ui.lock);
  return 0;
}

int lcd_show_message(const char *message, action_t type)
{
  if (!g_ui.initialized)
    {
      int ret = lcd_init();
      if (ret < 0) return ret;
    }
  if (message == NULL || message[0] == '\0') return -EINVAL;
  if (type != REMIND && type != ENCOURAGE) return -EINVAL;

  pthread_mutex_lock(&g_ui.lock);
  snprintf(g_ui.message, sizeof(g_ui.message), "%s", message);
  g_ui.message_type = type;
  g_ui.message_started_ms = monotonic_ms();
  g_ui.message_generation++;
  g_ui.message_active = 1;
  pthread_cond_signal(&g_ui.wake);
  pthread_mutex_unlock(&g_ui.lock);
  return 0;
}

#ifdef UI_UNIT_TEST
const uint16_t *lcd_debug_framebuffer(void)
{
  return g_ui.surface.pixels;
}

int lcd_debug_message_active(void)
{
  int active;
  pthread_mutex_lock(&g_ui.lock);
  active = g_ui.message_active;
  pthread_mutex_unlock(&g_ui.lock);
  return active;
}
#endif
