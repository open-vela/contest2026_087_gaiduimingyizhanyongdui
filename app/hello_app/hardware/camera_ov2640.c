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
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/videoio.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CAMERA_DEV      "/dev/video0"
#define CAMERA_WIDTH    320
#define CAMERA_HEIGHT   240
#define CAMERA_BUFCOUNT 2
#define CAMERA_BUFSIZE  (CAMERA_WIDTH * CAMERA_HEIGHT * 2)  /* RGB565 全尺寸 */
#define CAMERA_FRAME_TIMEOUT_MS 500   /* 单帧采集超时上限 */

/* ⚠️ 真机核验 (2026-08-19): esp32s3_cam 驱动实际配置 QVGA RGB565
 * (即使 S_FMT 请求 JPEG 也回显 JPEG 但传感器输出 RGB565)。
 * 采集必须用 RGB565 + V4L2_BUF_MODE_RING (设备验证成功的模式)。
 * JPEG 输出留给感知层用 STILL_CAPTURE/TAKEPICT 路径。 */

/* 最近一帧 (camera_task 采集后更新, perception 读取) */
uint8_t *camera_frame_buffer = NULL;
size_t   camera_frame_size   = 0;

static int g_camera_fd = -1;
static uint8_t *g_cam_buf2 = NULL;  /* 第 2 个 RING 缓冲 (内部分配) */

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

  /* 设置 QVGA RGB565 输出 (匹配驱动实际, RING 模式可采帧) */
  memset(&fmt, 0, sizeof(fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = CAMERA_WIDTH;
  fmt.fmt.pix.height = CAMERA_HEIGHT;
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
  fmt.fmt.pix.field = V4L2_FIELD_NONE;
  if (ioctl(g_camera_fd, VIDIOC_S_FMT, (unsigned long)&fmt) < 0)
    {
      printf("[camera] S_FMT FAILED errno=%d\n", errno);
      close(g_camera_fd);
      g_camera_fd = -1;
      return FOCUS_ERR_HW_NOTREADY;
    }

  /* 请求用户指针缓冲区, RING 模式 (驱动循环填充, 设备验证成功) */
  memset(&req, 0, sizeof(req));
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_USERPTR;
  req.count = CAMERA_BUFCOUNT;
  req.mode  = V4L2_BUF_MODE_RING;
  if (ioctl(g_camera_fd, VIDIOC_REQBUFS, (unsigned long)&req) < 0)
    {
      printf("[camera] REQBUFS FAILED errno=%d\n", errno);
      close(g_camera_fd);
      g_camera_fd = -1;
      return FOCUS_ERR_HW_NOTREADY;
    }

  /* RING 模式需全部缓冲入队, 分配第 2 缓冲 */
  g_cam_buf2 = malloc(CAMERA_BUFSIZE);
  if (g_cam_buf2 == NULL)
    {
      printf("[camera] 第 2 缓冲分配失败\n");
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
  struct pollfd pfd;
  int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  int pr;

  if (g_camera_fd < 0 || buf == NULL || size == NULL)
    {
      return FOCUS_ERR_HW_NOTREADY;
    }

  /* 入队全部缓冲 (RING 模式需环完整, 否则 DQBUF 阻塞) */
  {
    int i;
    for (i = 0; i < CAMERA_BUFCOUNT; i++)
      {
        memset(&vbuf, 0, sizeof(vbuf));
        vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        vbuf.memory = V4L2_MEMORY_USERPTR;
        vbuf.index = i;
        vbuf.m.userptr = (unsigned long)((i == 0) ? buf : g_cam_buf2);
        vbuf.length = CAMERA_BUFSIZE;
        if (ioctl(g_camera_fd, VIDIOC_QBUF, (unsigned long)&vbuf) < 0)
          {
            return FOCUS_ERR_IO;
          }
      }
  }

  /* 开启流 (若已开启会返回 EBUSY, 忽略) */
  ioctl(g_camera_fd, VIDIOC_STREAMON, (unsigned long)&type);

  /* poll 等待帧可用, 防止驱动异常时 DQBUF 永久阻塞 */
  pfd.fd = g_camera_fd;
  pfd.events = POLLIN;
  pr = poll(&pfd, 1, CAMERA_FRAME_TIMEOUT_MS);
  if (pr <= 0)
    {
      ioctl(g_camera_fd, VIDIOC_STREAMOFF, (unsigned long)&type);
      return FOCUS_ERR_TIMEOUT;
    }

  /* 取一帧 */
  memset(&vbuf, 0, sizeof(vbuf));
  vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  vbuf.memory = V4L2_MEMORY_USERPTR;
  if (ioctl(g_camera_fd, VIDIOC_DQBUF, (unsigned long)&vbuf) < 0)
    {
      printf("[camera] DQBUF FAILED errno=%d\n", errno);
      ioctl(g_camera_fd, VIDIOC_STREAMOFF, (unsigned long)&type);
      return FOCUS_ERR_TIMEOUT;
    }

  /* 停流复位, 供下一次采集重新 STREAMON (驱动为单帧模式) */
  ioctl(g_camera_fd, VIDIOC_STREAMOFF, (unsigned long)&type);

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
  if (g_cam_buf2 != NULL)
    {
      free(g_cam_buf2);
      g_cam_buf2 = NULL;
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
