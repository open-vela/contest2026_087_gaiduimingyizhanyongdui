#include "ui_draw.h"
#include "ui_cjk_font.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

static void ui_pixel(ui_surface_t *surface, int x, int y, uint16_t color)
{
  if (surface != NULL && surface->pixels != NULL &&
      x >= 0 && y >= 0 && x < surface->width &&
      y < surface->height)
    {
      surface->pixels[y * surface->stride + x] = color;
    }
}

void ui_clear(ui_surface_t *surface, uint16_t color)
{
  if (surface == NULL)
    {
      return;
    }
  ui_fill_rect(surface, 0, 0, surface->width, surface->height, color);
}

void ui_fill_rect(ui_surface_t *surface, int x, int y, int width, int height,
                  uint16_t color)
{
  int x0;
  int y0;
  int x1;
  int y1;
  int row;

  if (surface == NULL || surface->pixels == NULL ||
      width <= 0 || height <= 0)
    {
      return;
    }

  x0 = x < 0 ? 0 : x;
  y0 = y < 0 ? 0 : y;
  x1 = x + width > surface->width ? surface->width : x + width;
  y1 = y + height > surface->height ? surface->height : y + height;

  if (x0 >= x1 || y0 >= y1)
    {
      return;
    }

  for (row = y0; row < y1; row++)
    {
      int col;
      uint16_t *dest = surface->pixels + row * surface->stride + x0;
      for (col = x0; col < x1; col++)
        {
          *dest++ = color;
        }
    }
}

void ui_rect(ui_surface_t *surface, int x, int y, int width, int height,
             int thickness, uint16_t color)
{
  ui_fill_rect(surface, x, y, width, thickness, color);
  ui_fill_rect(surface, x, y + height - thickness, width, thickness, color);
  ui_fill_rect(surface, x, y, thickness, height, color);
  ui_fill_rect(surface, x + width - thickness, y, thickness, height, color);
}

void ui_line(ui_surface_t *surface, int x0, int y0, int x1, int y1,
             uint16_t color)
{
  int dx = x1 > x0 ? x1 - x0 : x0 - x1;
  int sx = x0 < x1 ? 1 : -1;
  int dy = y1 > y0 ? y0 - y1 : y1 - y0;
  int sy = y0 < y1 ? 1 : -1;
  int error = dx + dy;

  for (;;)
    {
      int twice;
      ui_pixel(surface, x0, y0, color);
      if (x0 == x1 && y0 == y1)
        {
          break;
        }

      twice = error * 2;
      if (twice >= dy)
        {
          error += dy;
          x0 += sx;
        }

      if (twice <= dx)
        {
          error += dx;
          y0 += sy;
        }
    }
}

void ui_circle(ui_surface_t *surface, int cx, int cy, int radius,
               int thickness, uint16_t color)
{
  int outer;
  for (outer = 0; outer < thickness; outer++)
    {
      int x = radius - outer;
      int y = 0;
      int error = 1 - x;
      while (x >= y)
        {
          ui_pixel(surface, cx + x, cy + y, color);
          ui_pixel(surface, cx + y, cy + x, color);
          ui_pixel(surface, cx - y, cy + x, color);
          ui_pixel(surface, cx - x, cy + y, color);
          ui_pixel(surface, cx - x, cy - y, color);
          ui_pixel(surface, cx - y, cy - x, color);
          ui_pixel(surface, cx + y, cy - x, color);
          ui_pixel(surface, cx + x, cy - y, color);
          y++;
          if (error < 0)
            {
              error += 2 * y + 1;
            }
          else
            {
              x--;
              error += 2 * (y - x + 1);
            }
        }
    }
}

void ui_fill_circle(ui_surface_t *surface, int cx, int cy, int radius,
                    uint16_t color)
{
  int y;
  for (y = -radius; y <= radius; y++)
    {
      int x = 0;
      while ((x + 1) * (x + 1) + y * y <= radius * radius)
        {
          x++;
        }
      ui_fill_rect(surface, cx - x, cy + y, x * 2 + 1, 1, color);
    }
}

static const uint8_t *ui_glyph(char input)
{
  static const uint8_t blank[7] = {0, 0, 0, 0, 0, 0, 0};
  static const uint8_t question[7] = {14, 17, 1, 2, 4, 0, 4};
  static const uint8_t exclamation[7] = {4, 4, 4, 4, 4, 0, 4};
  static const uint8_t colon[7] = {0, 4, 4, 0, 4, 4, 0};
  static const uint8_t dash[7] = {0, 0, 0, 31, 0, 0, 0};
  static const uint8_t dot[7] = {0, 0, 0, 0, 0, 4, 4};
  static const uint8_t slash[7] = {1, 2, 2, 4, 8, 8, 16};
  static const uint8_t star[7] = {0, 21, 14, 31, 14, 21, 0};
  static const uint8_t glyphs[][7] =
  {
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14},
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14}, {7,2,2,2,2,18,12},
    {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
    {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}
  };
  unsigned char ch = (unsigned char)input;

  if (ch >= 'a' && ch <= 'z')
    {
      ch = (unsigned char)toupper(ch);
    }
  if (ch >= '0' && ch <= '9')
    {
      return glyphs[ch - '0'];
    }
  if (ch >= 'A' && ch <= 'Z')
    {
      return glyphs[10 + ch - 'A'];
    }
  if (ch == ' ')
    {
      return blank;
    }
  if (ch == '!') return exclamation;
  if (ch == ':') return colon;
  if (ch == '-') return dash;
  if (ch == '.') return dot;
  if (ch == '/') return slash;
  if (ch == '*') return star;
  return question;
}

static int ui_utf8_decode(const unsigned char *text, uint32_t *codepoint)
{
  if ((*text & 0x80) == 0)
    {
      *codepoint = *text;
      return 1;
    }
  if ((*text & 0xe0) == 0xc0 && text[1] != '\0')
    {
      *codepoint = ((uint32_t)(text[0] & 0x1f) << 6) |
                   (uint32_t)(text[1] & 0x3f);
      return 2;
    }
  if ((*text & 0xf0) == 0xe0 && text[1] != '\0' && text[2] != '\0')
    {
      *codepoint = ((uint32_t)(text[0] & 0x0f) << 12) |
                   ((uint32_t)(text[1] & 0x3f) << 6) |
                   (uint32_t)(text[2] & 0x3f);
      return 3;
    }
  if ((*text & 0xf8) == 0xf0 && text[1] != '\0' &&
      text[2] != '\0' && text[3] != '\0')
    {
      *codepoint = ((uint32_t)(text[0] & 0x07) << 18) |
                   ((uint32_t)(text[1] & 0x3f) << 12) |
                   ((uint32_t)(text[2] & 0x3f) << 6) |
                   (uint32_t)(text[3] & 0x3f);
      return 4;
    }
  *codepoint = '?';
  return 1;
}

static int ui_codepoint_width(uint32_t codepoint, int scale)
{
  return codepoint < 0x80 ? 6 * scale : (UI_CJK_GLYPH_SIZE + 1) * scale;
}

int ui_text_width(const char *text, int scale)
{
  int count = 0;
  const unsigned char *cursor = (const unsigned char *)text;
  if (text == NULL || scale < 1) return 0;
  while (*cursor != '\0')
    {
      uint32_t codepoint;
      int advance = ui_utf8_decode(cursor, &codepoint);
      count += ui_codepoint_width(codepoint, scale);
      cursor += advance;
    }
  return count == 0 ? 0 : count - scale;
}

void ui_text(ui_surface_t *surface, int x, int y, const char *text, int scale,
             uint16_t color)
{
  const unsigned char *cursor = (const unsigned char *)text;
  if (text == NULL || scale < 1) return;

  while (*cursor != '\0')
    {
      uint32_t codepoint;
      int advance = ui_utf8_decode(cursor, &codepoint);
      int row;
      if (codepoint >= 0x80)
        {
          const uint16_t *glyph = ui_cjk_glyph(codepoint);
          if (glyph != NULL)
            {
              for (row = 0; row < UI_CJK_GLYPH_SIZE; row++)
                {
                  int col;
                  for (col = 0; col < UI_CJK_GLYPH_SIZE; col++)
                    if ((glyph[row] & (1 << (UI_CJK_GLYPH_SIZE - 1 - col))) != 0)
                      ui_fill_rect(surface, x + col * scale, y + row * scale,
                                   scale, scale, color);
                }
            }
          else
            {
              const uint8_t *fallback = ui_glyph('?');
              for (row = 0; row < 7; row++)
                {
                  int col;
                  for (col = 0; col < 5; col++)
                    if ((fallback[row] & (1 << (4 - col))) != 0)
                      ui_fill_rect(surface, x + col * scale,
                                   y + row * scale, scale, scale, color);
                }
            }
        }
      else
        {
          const uint8_t *glyph = ui_glyph((char)codepoint);
          for (row = 0; row < 7; row++)
            {
              int col;
              for (col = 0; col < 5; col++)
                if ((glyph[row] & (1 << (4 - col))) != 0)
                  ui_fill_rect(surface, x + col * scale, y + row * scale,
                               scale, scale, color);
            }
        }
      x += ui_codepoint_width(codepoint, scale);
      cursor += advance;
    }
}

void ui_text_center(ui_surface_t *surface, int center_x, int y,
                    const char *text, int scale, uint16_t color)
{
  ui_text(surface, center_x - ui_text_width(text, scale) / 2, y,
          text, scale, color);
}

void ui_text_wrap_center(ui_surface_t *surface, int center_x, int y,
                         int max_width, int max_lines, const char *text,
                         int scale, uint16_t color)
{
  const unsigned char *cursor = (const unsigned char *)text;
  char line[128];
  int line_number = 0;

  if (text == NULL || scale < 1 || max_lines < 1) return;
  while (*cursor != '\0' && line_number < max_lines)
    {
      size_t bytes = 0;
      int width = 0;
      while (cursor[bytes] != '\0')
        {
          uint32_t codepoint;
          int advance = ui_utf8_decode(cursor + bytes, &codepoint);
          int next_width = width + ui_codepoint_width(codepoint, scale);
          if (bytes > 0 && next_width - scale > max_width) break;
          if (bytes + (size_t)advance >= sizeof(line)) break;
          bytes += (size_t)advance;
          width = next_width;
        }
      if (bytes == 0) break;
      memcpy(line, cursor, bytes);
      line[bytes] = '\0';
      ui_text_center(surface, center_x, y + line_number *
                     (UI_CJK_GLYPH_SIZE + 3) * scale, line, scale, color);
      cursor += bytes;
      line_number++;
    }
}
