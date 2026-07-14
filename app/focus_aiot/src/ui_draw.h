#ifndef FOCUS_AIOT_UI_DRAW_H
#define FOCUS_AIOT_UI_DRAW_H

#include <stdint.h>

typedef struct
{
  uint16_t *pixels;
  int width;
  int height;
  int stride;
} ui_surface_t;

#define UI_RGB565(r, g, b) \
  ((uint16_t)((((uint16_t)(r) & 0xf8) << 8) | \
              (((uint16_t)(g) & 0xfc) << 3) | ((uint16_t)(b) >> 3)))

void ui_clear(ui_surface_t *surface, uint16_t color);
void ui_fill_rect(ui_surface_t *surface, int x, int y, int width, int height,
                  uint16_t color);
void ui_rect(ui_surface_t *surface, int x, int y, int width, int height,
             int thickness, uint16_t color);
void ui_line(ui_surface_t *surface, int x0, int y0, int x1, int y1,
             uint16_t color);
void ui_circle(ui_surface_t *surface, int cx, int cy, int radius,
               int thickness, uint16_t color);
void ui_fill_circle(ui_surface_t *surface, int cx, int cy, int radius,
                    uint16_t color);
int ui_text_width(const char *text, int scale);
void ui_text(ui_surface_t *surface, int x, int y, const char *text, int scale,
             uint16_t color);
void ui_text_center(ui_surface_t *surface, int center_x, int y,
                    const char *text, int scale, uint16_t color);

#endif
