/****************************************************************************
 * FOCUS AIoT - RGB565 → JPEG 编码 (core/rgb565_jpeg.c)
 *
 * 摄像头输出 RGB565, 云端 MiMo 识图需要 JPEG。
 * 本模块: RGB565 → RGB888 → TinyJPEG (Baseline JPEG) 编码。
 *
 * 基于 TinyJPEG (https://github.com/serge-rgb/TinyJPEG), public domain。
 ****************************************************************************/

#define TJE_IMPLEMENTATION
#include "../third_party/tiny_jpeg.h"

#include "rgb565_jpeg.h"

#include <stdlib.h>
#include <string.h>

/* ---- JPEG 输出收集 (内存回调) ---- */
typedef struct
{
  uint8_t *buf;    /* 输出缓冲区 */
  size_t   cap;    /* 容量 */
  size_t   used;   /* 已写字节 */
  int      overflowed;  /* 输出超出容量 (会截断, 上层需扩大缓冲) */
} jpeg_sink_t;

static void jpeg_write_cb(void *context, void *data, int size)
{
  jpeg_sink_t *sink = (jpeg_sink_t *)context;

  if (sink->used + (size_t)size <= sink->cap)
    {
      memcpy(sink->buf + sink->used, data, (size_t)size);
      sink->used += (size_t)size;
    }
  else
    {
      /* 溢出: 丢弃并标记。静默截断会丢 JPEG 的 EOI 标记, 云端解码 400。 */
      sink->overflowed = 1;
    }
}

/****************************************************************************
 * Name: rgb565_to_jpeg
 ****************************************************************************/
int rgb565_to_jpeg(const uint8_t *rgb565, int width, int height,
                   uint8_t *jpeg_out, size_t *jpeg_size)
{
  uint8_t *rgb;
  jpeg_sink_t sink;
  int ok;
  int n;
  int i;

  if (rgb565 == NULL || jpeg_out == NULL || jpeg_size == NULL ||
      width <= 0 || height <= 0)
    {
      return -1;
    }

  /* RGB565 → RGB888 (3 通道, TinyJPEG 需要) */
  n = width * height;
  rgb = malloc((size_t)n * 3);
  if (rgb == NULL)
    {
      return -1;
    }

  for (i = 0; i < n; i++)
    {
      uint16_t p = (uint16_t)(rgb565[i * 2]) |
                   (uint16_t)(rgb565[i * 2 + 1]) << 8;
      uint8_t r5 = (uint8_t)((p >> 11) & 0x1F);
      uint8_t g6 = (uint8_t)((p >> 5) & 0x3F);
      uint8_t b5 = (uint8_t)(p & 0x1F);

      rgb[i * 3]     = (uint8_t)((r5 << 3) | (r5 >> 2));   /* R */
      rgb[i * 3 + 1] = (uint8_t)((g6 << 2) | (g6 >> 4));   /* G */
      rgb[i * 3 + 2] = (uint8_t)((b5 << 3) | (b5 >> 2));   /* B */
    }

  /* TinyJPEG 编码 (quality 2: 很好, 约 1/2 尺寸)。
   * 注意: 细节多的帧输出可能 > width*height (实测噪声帧约 2.8×),
   * 调用方缓冲必须给足, 否则截断丢 EOI 标记, 云端解码 400。 */
  sink.buf        = jpeg_out;
  sink.cap        = *jpeg_size;
  sink.used       = 0;
  sink.overflowed = 0;

  ok = tje_encode_with_func(jpeg_write_cb, &sink, 2,
                            width, height, 3, rgb);

  free(rgb);

  if (!ok || sink.overflowed)
    {
      return -1;
    }

  *jpeg_size = sink.used;
  return 0;
}
