/****************************************************************************
 * FOCUS AIoT - RGB565 → JPEG 编码 (core/rgb565_jpeg.h)
 *
 * 基于 TinyJPEG (Baseline JPEG, header-only, public domain)。
 * 摄像头输出 RGB565, 云端 MiMo 需要 JPEG, 由本模块做转换。
 *
 * 返回: 0=成功, -1=失败
 ****************************************************************************/

#ifndef FOCUS_AIOT_CORE_RGB565_JPEG_H
#define FOCUS_AIOT_CORE_RGB565_JPEG_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RGB565 → JPEG (Baseline, quality 2 足够云端识别)。
 * rgb565:  输入 RGB565 像素流 (width*height*2 字节)
 * jpeg_out: 输出 JPEG 缓冲区 (调用方分配, 建议 ≥ width*height 字节)
 * jpeg_size: 输入缓冲区容量, 输出实际 JPEG 大小
 * 返回: 0=成功 */
int rgb565_to_jpeg(const uint8_t *rgb565, int width, int height,
                   uint8_t *jpeg_out, size_t *jpeg_size);

#ifdef __cplusplus
}
#endif

#endif /* FOCUS_AIOT_CORE_RGB565_JPEG_H */
