#include "lcd_icons.h"

static void draw_phone(ui_surface_t *surface, int x, int y, uint16_t color)
{
  ui_rect(surface, x, y, 20, 34, 3, color);
  ui_fill_rect(surface, x + 7, y + 28, 6, 2, color);
}

static void draw_check(ui_surface_t *surface, int x, int y, uint16_t color)
{
  ui_line(surface, x, y + 8, x + 8, y + 16, color);
  ui_line(surface, x + 8, y + 16, x + 24, y, color);
  ui_line(surface, x, y + 9, x + 8, y + 17, color);
  ui_line(surface, x + 8, y + 17, x + 24, y + 1, color);
}

static void draw_star(ui_surface_t *surface, int cx, int cy, uint16_t color)
{
  ui_line(surface, cx, cy - 16, cx + 5, cy - 5, color);
  ui_line(surface, cx + 5, cy - 5, cx + 17, cy - 4, color);
  ui_line(surface, cx + 17, cy - 4, cx + 8, cy + 4, color);
  ui_line(surface, cx + 8, cy + 4, cx + 11, cy + 16, color);
  ui_line(surface, cx + 11, cy + 16, cx, cy + 9, color);
  ui_line(surface, cx, cy + 9, cx - 11, cy + 16, color);
  ui_line(surface, cx - 11, cy + 16, cx - 8, cy + 4, color);
  ui_line(surface, cx - 8, cy + 4, cx - 17, cy - 4, color);
  ui_line(surface, cx - 17, cy - 4, cx - 5, cy - 5, color);
  ui_line(surface, cx - 5, cy - 5, cx, cy - 16, color);
}

void lcd_draw_status_icon(ui_surface_t *surface, int x, int y,
                          uint8_t mode, status_t status)
{
  const uint16_t green = UI_RGB565(0, 200, 83);
  const uint16_t yellow = UI_RGB565(255, 214, 0);
  const uint16_t red = UI_RGB565(213, 0, 0);
  const uint16_t orange = UI_RGB565(255, 152, 0);
  const uint16_t gray = UI_RGB565(158, 158, 158);
  const uint16_t blue_gray = UI_RGB565(96, 125, 139);
  const uint16_t blue = UI_RGB565(68, 138, 255);
  uint16_t color = green;
  int cx = x + 40;
  int cy = y + 40;

  if (status == GLANCING_PHONE) color = mode == 0 ? yellow : orange;
  else if (status == PLAYING_PHONE) color = mode == 0 ? red : orange;
  else if (status == AWAY) color = mode == 0 ? gray : blue_gray;
  else if (status == DROWSY) color = blue;

  ui_rect(surface, x, y, 80, 80, 1, UI_RGB565(55, 65, 81));

  if (status == FOCUSED)
    {
      ui_circle(surface, cx, cy, 31, 3, color);
      if (mode == 0) draw_check(surface, cx - 12, cy - 8, color);
      else draw_star(surface, cx, cy, color);
    }
  else if (status == GLANCING_PHONE)
    {
      if (mode == 0)
        {
          ui_line(surface, cx, cy - 31, cx - 32, cy + 26, color);
          ui_line(surface, cx - 32, cy + 26, cx + 32, cy + 26, color);
          ui_line(surface, cx + 32, cy + 26, cx, cy - 31, color);
        }
      else
        {
          ui_circle(surface, cx, cy, 31, 3, color);
        }
      draw_phone(surface, cx - 10, cy - 16, color);
      if (mode != 0) ui_text(surface, cx + 13, cy - 18, "?", 2, color);
    }
  else if (status == PLAYING_PHONE)
    {
      ui_circle(surface, cx, cy, 31, 3, color);
      draw_phone(surface, cx - 10, cy - 17, color);
      if (mode == 0)
        {
          ui_line(surface, cx - 22, cy + 22, cx + 22, cy - 22, color);
          ui_line(surface, cx - 21, cy + 22, cx + 23, cy - 22, color);
        }
      else
        {
          ui_circle(surface, cx + 19, cy - 17, 6, 2, color);
          ui_line(surface, cx + 14, cy - 10, cx + 24, cy - 10, color);
        }
    }
  else if (status == AWAY)
    {
      if (mode == 0)
        {
          ui_line(surface, cx, cy - 32, cx + 32, cy, color);
          ui_line(surface, cx + 32, cy, cx, cy + 32, color);
          ui_line(surface, cx, cy + 32, cx - 32, cy, color);
          ui_line(surface, cx - 32, cy, cx, cy - 32, color);
          ui_rect(surface, cx - 16, cy - 5, 32, 18, 3, color);
          ui_line(surface, cx - 12, cy + 13, cx - 17, cy + 25, color);
          ui_line(surface, cx + 12, cy + 13, cx + 17, cy + 25, color);
        }
      else
        {
          ui_circle(surface, cx, cy, 31, 3, color);
          ui_rect(surface, cx - 15, cy - 22, 30, 45, 3, color);
          ui_fill_circle(surface, cx + 8, cy, 2, color);
        }
    }
  else
    {
      ui_circle(surface, cx, cy, 31, 3, color);
      if (mode == 0)
        {
          ui_text(surface, cx - 19, cy - 19, "ZZZ", 2, color);
        }
      else
        {
          ui_rect(surface, cx - 18, cy - 8, 31, 22, 3, color);
          ui_circle(surface, cx + 17, cy + 1, 8, 2, color);
          ui_line(surface, cx - 12, cy - 17, cx - 8, cy - 23, color);
          ui_line(surface, cx, cy - 17, cx + 4, cy - 23, color);
        }
    }
}
