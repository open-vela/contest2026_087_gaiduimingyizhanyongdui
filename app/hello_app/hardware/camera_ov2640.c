/****************************************************************************
 * FOCUS AIoT - 真实摄像头驱动 (hardware/camera_ov2640.c)
 *
 * 负责人: 张沐泽
 * 职责: 通过 V4L2 从内核 esp32s3_cam 驱动 (/dev/video0) 采集单帧 JPEG。
 *
 * 说明:
 *   - OV2640 传感器初始化由内核 esp32s3_cam 驱动完成, 本文件只走 V4L2。
 *   - QVGA 320x240 JPEG, 缓冲区由调用方在 PSRAM 分配 (≥50KB)。
 *
 * ⚠️ 上板核验: 设备节点名(/dev/video0)、像素格式(JPEG)、ioctl 序列
 *   需与内核 esp32s3_cam 驱动实际实现核对 (见 DEVELOPMENT_TASKS 的
 *   V4L2_BUF_TYPE_STILL_CAPTURE / VIDIOC_TAKEPICT 单帧模式)。
 ****************************************************************************/

#include "../api/camera.h"
#include "../api/error.h"

#include <nuttx/config.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/videoio.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define CAMERA_DEV      "/dev/video0"
#define CAMERA_WIDTH    320
#define CAMERA_HEIGHT   240
#define CAMERA_BUFCOUNT 2

/* 最近一帧 (camera_task 采集后更新, perception 读取) */
uint8_t *camera_frame_buffer = NULL;
size_t   camera_frame_size   = 0;

static int g_camera_fd = -1;

/****************************************************************************
 * Name: camera_init
 ****************************************************************************/
int camera_init(void)
{
  struct v4l2_format fmt;
  struct v4l2_requestbuffers req;

  g_camera_fd = open(CAMERA_DEV, O_RDWR);
  if (g_camera_fd < 0)
    {
      return FOCUS_ERR_HW_NOTREADY;
    }

  /* 设置 QVGA JPEG 输出 */
  memset(&fmt, 0, sizeof(fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = CAMERA_WIDTH;
  fmt.fmt.pix.height = CAMERA_HEIGHT;
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_JPEG;
  fmt.fmt.pix.field = V4L2_FIELD_NONE;
  if (ioctl(g_camera_fd, VIDIOC_S_FMT, (unsigned long)&fmt) < 0)
    {
      close(g_camera_fd);
      g_camera_fd = -1;
      return FOCUS_ERR_HW_NOTREADY;
    }

  /* 请求用户指针缓冲区 */
  memset(&req, 0, sizeof(req));
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_USERPTR;
  req.count = CAMERA_BUFCOUNT;
  if (ioctl(g_camera_fd, VIDIOC_REQBUFS, (unsigned long)&req) < 0)
    {
      close(g_camera_fd);
      g_camera_fd = -1;
      return FOCUS_ERR_HW_NOTREADY;
    }

  return FOCUS_OK;
}

/****************************************************************************
 * Name: camera_capture_frame
 *
 * 采一帧 JPEG 到 buf。阻塞上限 500ms。
 ****************************************************************************/
int camera_capture_frame(uint8_t *buf, size_t *size)
{
  struct v4l2_buffer vbuf;
  int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  if (g_camera_fd < 0 || buf == NULL || size == NULL)
    {
      return FOCUS_ERR_HW_NOTREADY;
    }

  /* 入队缓冲区 */
  memset(&vbuf, 0, sizeof(vbuf));
  vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  vbuf.memory = V4L2_MEMORY_USERPTR;
  vbuf.index = 0;
  vbuf.m.userptr = (unsigned long)buf;
  vbuf.length = 50 * 1024;   /* QVGA JPEG 足够 */
  if (ioctl(g_camera_fd, VIDIOC_QBUF, (unsigned long)&vbuf) < 0)
    {
      return FOCUS_ERR_IO;
    }

  /* 开启流 (若已开启会返回 EBUSY, 忽略) */
  ioctl(g_camera_fd, VIDIOC_STREAMON, (unsigned long)&type);

  /* 等待一帧 */
  memset(&vbuf, 0, sizeof(vbuf));
  vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  vbuf.memory = V4L2_MEMORY_USERPTR;
  if (ioctl(g_camera_fd, VIDIOC_DQBUF, (unsigned long)&vbuf) < 0)
    {
      return FOCUS_ERR_TIMEOUT;
    }

  *size = vbuf.bytesused;
  camera_frame_buffer = buf;
  camera_frame_size   = vbuf.bytesused;

  return FOCUS_OK;
}

/****************************************************************************
 * Name: camera_deinit
 ****************************************************************************/
void camera_deinit(void)
{
  if (g_camera_fd >= 0)
    {
      close(g_camera_fd);
      g_camera_fd = -1;
    }
  camera_frame_buffer = NULL;
  camera_frame_size   = 0;
}

/****************************************************************************
 * Name: 独立测试 (编译时 -DTEST_CAMERA)
 ****************************************************************************/
#ifdef TEST_CAMERA
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
  uint8_t *buf = malloc(50 * 1024);
  size_t size = 0;
  int i;

  if (camera_init() != FOCUS_OK)
    {
      printf("Camera init FAILED\n");
      return 1;
    }

  for (i = 0; i < 5; i++)
    {
      if (camera_capture_frame(buf, &size) == FOCUS_OK)
        {
          printf("Frame %d: %u bytes\n", i, (unsigned)size);
        }
      else
        {
          printf("Frame %d: FAILED\n", i);
        }
      sleep(2);
    }

  camera_deinit();
  free(buf);
  return 0;
}
#endif
