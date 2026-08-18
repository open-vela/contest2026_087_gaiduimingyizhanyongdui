/****************************************************************************
 * FOCUS AIoT - 摄像头 mock 桩 (hardware/camera_stub.c)
 *
 * 负责人: 张沐泽
 * 说明: 弱符号提供 camera_init/camera_capture_frame/camera_deinit 及帧缓冲
 *       外部变量，返回空帧。真实实现由 hardware/camera_ov2640.c (强符号)
 *       提供，Kconfig 二选一编译。
 ****************************************************************************/

#include "../api/camera.h"
#include "../api/error.h"

#include <stddef.h>
#include <stdint.h>

#if defined(__GNUC__) && !defined(_WIN32)
#  define FOCUS_WEAK __attribute__((weak))
#else
#  define FOCUS_WEAK
#endif

FOCUS_WEAK uint8_t *camera_frame_buffer = NULL;
FOCUS_WEAK size_t   camera_frame_size   = 0;

FOCUS_WEAK int camera_init(void)
{
  return FOCUS_OK;
}

FOCUS_WEAK int camera_capture_frame(uint8_t *buf, size_t *size)
{
  (void)buf;
  if (size != NULL)
    {
      *size = 0;
    }
  return FOCUS_OK;
}

FOCUS_WEAK void camera_deinit(void)
{
  /* no-op */
}
