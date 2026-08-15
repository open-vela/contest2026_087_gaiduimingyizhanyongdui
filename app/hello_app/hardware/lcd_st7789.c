/****************************************************************************
 * FOCUS AIoT - 真实 LCD 驱动 (hardware/lcd_st7789.c)
 *
 * 负责人: 张沐泽
 * 职责: 打开内核 framebuffer 设备 /dev/fb0 并 mmap，向 UI 模块暴露
 *       真实屏幕的 framebuffer 指针。ST7789V 的 SPI 初始化/复位序列由
 *       内核 board_lcd_initialize() 完成，本文件不操作底层 SPI。
 *
 * 覆盖关系: hardware/lcd_stub.c 的三个函数是 __attribute__((weak))，
 *       本文件的强定义链接后自动覆盖 stub，无需修改任何调用方。
 *
 * 屏幕参数 (由内核驱动上报, 与 hwtest 实测一致):
 *   - 尺寸    : 240 x 240
 *   - 像素格式: RGB565 (FB_FMT_RGB16_565), bpp=16
 *   - stride  : 480 bytes/行
 *   - fblen   : 115200 bytes
 ****************************************************************************/

#include "../api/error.h"
#include "../api/lcd.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <nuttx/video/fb.h>

#include <stdint.h>

static int                    g_lcd_fd = -1;   /* /dev/fb0 文件描述符 */
static uint16_t              *g_lcd_fb = NULL; /* mmap 后的真实 framebuffer */
static struct fb_videoinfo_s  g_vinfo;
static struct fb_planeinfo_s  g_pinfo;

/****************************************************************************
 * Name: lcd_init
 *
 * 打开 /dev/fb0 → 读取视频/平面信息 → mmap framebuffer。
 * 返回: FOCUS_OK(0)=成功, FOCUS_ERR_HW_NOTREADY(-12)=硬件未就绪
 ****************************************************************************/
int lcd_init(void)
{
  g_lcd_fd = open("/dev/fb0", O_RDWR);
  if (g_lcd_fd < 0)
    {
      return FOCUS_ERR_HW_NOTREADY;
    }

  if (ioctl(g_lcd_fd, FBIOGET_VIDEOINFO, (unsigned long)&g_vinfo) < 0)
    {
      close(g_lcd_fd);
      g_lcd_fd = -1;
      return FOCUS_ERR_HW_NOTREADY;
    }

  if (ioctl(g_lcd_fd, FBIOGET_PLANEINFO, (unsigned long)&g_pinfo) < 0)
    {
      close(g_lcd_fd);
      g_lcd_fd = -1;
      return FOCUS_ERR_HW_NOTREADY;
    }

  /* 240x240 RGB565: fblen=115200, stride=480 */
  g_lcd_fb = mmap(NULL, g_pinfo.fblen,
                  PROT_READ | PROT_WRITE,
                  MAP_SHARED | MAP_FILE, g_lcd_fd, 0);
  if (g_lcd_fb == MAP_FAILED)
    {
      close(g_lcd_fd);
      g_lcd_fd = -1;
      g_lcd_fb = NULL;
      return FOCUS_ERR_HW_NOTREADY;
    }

  return FOCUS_OK;
}

/****************************************************************************
 * Name: lcd_get_framebuffer
 *
 * 返回指向真实屏幕的 framebuffer 指针 (240x240 RGB565)。
 * UI 模块 (郭黄亦昕) 在此缓冲区绘制后调 lcd_flush() 即可上屏。
 ****************************************************************************/
uint16_t *lcd_get_framebuffer(void)
{
  return g_lcd_fb;
}

/****************************************************************************
 * Name: lcd_flush
 *
 * 将 framebuffer 整体刷新到 LCD (FBIO_UPDATE)。
 * 由 ui_task 以 1Hz 调用, 单次应在 50ms 内返回。
 * 返回: FOCUS_OK(0)=成功, FOCUS_ERR_IO(-6)=IO 错误, FOCUS_ERR_HW_NOTREADY(-12)=未就绪
 ****************************************************************************/
int lcd_flush(void)
{
  struct fb_area_s area;

  if (g_lcd_fd < 0)
    {
      return FOCUS_ERR_HW_NOTREADY;
    }

  area.x = 0;
  area.y = 0;
  area.w = g_vinfo.xres;   /* 240 */
  area.h = g_vinfo.yres;   /* 240 */

  if (ioctl(g_lcd_fd, FBIO_UPDATE, (unsigned long)&area) < 0)
    {
      return FOCUS_ERR_IO;
    }

  return FOCUS_OK;
}

/****************************************************************************
 * Name: 独立测试 (编译时 -DTEST_LCD 单独验证驱动)
 ****************************************************************************/
#ifdef TEST_LCD
#include <stdio.h>

int main(void)
{
  uint16_t *fb;
  int i;

  if (lcd_init() != FOCUS_OK)
    {
      printf("LCD test FAILED: lcd_init()\n");
      return 1;
    }

  fb = lcd_get_framebuffer();
  if (fb == NULL)
    {
      printf("LCD test FAILED: framebuffer is NULL\n");
      return 1;
    }

  /* 5 条水平色带: 红/绿/蓝/白/黑 (按内核上报的分辨率) */
  {
    int width  = g_vinfo.xres;
    int height = g_vinfo.yres;
    for (i = 0; i < width * height; i++)
      {
        int row = i / width;
        if (row < height / 5)          fb[i] = 0xf800;  /* 红 */
        else if (row < height * 2 / 5) fb[i] = 0x07e0;  /* 绿 */
        else if (row < height * 3 / 5) fb[i] = 0x001f;  /* 蓝 */
        else if (row < height * 4 / 5) fb[i] = 0xffff;  /* 白 */
        else                           fb[i] = 0x0000;  /* 黑 */
      }
  }

  if (lcd_flush() == FOCUS_OK)
    {
      printf("LCD test PASSED (240x240 RGB565)\n");
      return 0;
    }

  printf("LCD test FAILED: lcd_flush()\n");
  return 1;
}
#endif
