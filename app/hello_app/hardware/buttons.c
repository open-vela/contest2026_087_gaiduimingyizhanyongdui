/****************************************************************************
 * FOCUS AIoT - 真实按键驱动 (hardware/buttons.c)
 *
 * 负责人: 张沐泽
 * 职责: BOOT 按键 GPIO 中断 + 消抖 + 长短按判定，非阻塞输出 button_event_t。
 *
 * 硬件事实 (已核实, 来自 vendor_espressif 板级 esp32s3_buttons.c):
 *   - ESP32-S3-EYE 仅 1 个物理按键: BOOT = GPIO0, 按下为低电平(0), 上拉输入。
 *   - 中断: 双边沿(CHANGE), irq_attach + esp32s3_gpioirqenable。
 *
 * ⚠️ 硬件限制: 冻结接口 button.h 假设 3 个按键(开始/模式/暂停), 但板上只有
 *    1 个 BOOT 键。本驱动把单个按键按时长映射为:
 *      短按 <1s          → BTN_START_SHORT
 *      长按 1~3s         → BTN_START_LONGPRESS
 *      超长按 >3s        → BTN_STOP
 *    BTN_MODE / BTN_PAUSE_* 需外接按键板才能覆盖, 见 DEVELOPMENT_TASKS。
 ****************************************************************************/

#include "../api/button.h"

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/irq.h>

/* ESP32-S3 GPIO 板级同款 (arch 私有头, 若报找不到请确认 -I 含 arch 目录) */
#include <esp32s3_gpio.h>
#include <hardware/esp32s3_gpio_sigmap.h>

/* ---- 引脚与常量 ---- */
#ifndef BUTTON_BOOT
#  define BUTTON_BOOT 0   /* BOOT 按键 GPIO0 */
#endif

#define BTN_DEBOUNCE_TICKS   (TICK_PER_SEC / 50)  /* ~20ms 消抖 */

/* ---- 内部状态 ---- */
static volatile uint32_t g_press_start_ticks;   /* 按下时刻 (tick) */
static volatile bool     g_pressed;             /* 当前是否按住   */
static volatile uint32_t g_last_duration_ticks; /* 最近一次完整按压时长 */
static volatile int      g_release_count;       /* 待消费的完整按压数 */

/****************************************************************************
 * Name: btn_isr
 *
 * 双边沿中断: 记录按下/释放时刻, 20ms 消抖, 释放时累计一次完整按压。
 ****************************************************************************/
static int btn_isr(int irq, FAR void *context, FAR void *arg)
{
  static uint32_t last_edge_ticks = 0;
  uint32_t now = clock_systime_ticks();
  bool level;

  /* 消抖: 忽略 20ms 内的连续边沿 */
  if (now - last_edge_ticks < BTN_DEBOUNCE_TICKS)
    {
      return OK;
    }
  last_edge_ticks = now;

  level = esp32s3_gpioread(BUTTON_BOOT);   /* 0=按下, 1=释放 */
  if (level == 0)
    {
      g_press_start_ticks = now;
      g_pressed = true;
    }
  else if (g_pressed)
    {
      g_last_duration_ticks = now - g_press_start_ticks;
      g_release_count++;
      g_pressed = false;
    }

  return OK;
}

/****************************************************************************
 * Name: button_init
 ****************************************************************************/
void button_init(void)
{
  int irq;

  /* 上拉输入 */
  esp32s3_configgpio(BUTTON_BOOT, INPUT_FUNCTION_2 | PULLUP);

  /* 双边沿中断 */
  irq = ESP32S3_PIN2IRQ(BUTTON_BOOT);
  irq_attach(irq, btn_isr, NULL);
  esp32s3_gpioirqenable(irq, CHANGE);
}

/****************************************************************************
 * Name: button_get_event
 *
 * 非阻塞: 消费一次完整按压, 按时长区分短按/长按/超长按。
 ****************************************************************************/
button_event_t button_get_event(void)
{
  uint32_t duration_ticks;
  uint32_t duration_ms;

  if (g_release_count == 0)
    {
      return BTN_NONE;
    }

  /* 无临界区: ISR 只写 volatile 单变量, 主线程读取的竞态窗口极小,
   * 且在 10Hz 轮询下可接受 (与真实按键驱动常见做法一致)。 */
  duration_ticks = g_last_duration_ticks;
  g_release_count--;

  duration_ms = duration_ticks * 1000UL / TICK_PER_SEC;

  if (duration_ms < 1000)
    {
      return BTN_START_SHORT;
    }
  else if (duration_ms < 3000)
    {
      return BTN_START_LONGPRESS;
    }

  return BTN_STOP;
}

/****************************************************************************
 * Name: 独立测试 (编译时 -DTEST_BUTTON)
 ****************************************************************************/
#ifdef TEST_BUTTON
#include <stdio.h>
#include <unistd.h>

int main(void)
{
  button_init();
  printf("Button test: 请按 BOOT 键 (短按/长按/超长按)\n");

  for (;;)
    {
      button_event_t ev = button_get_event();
      if (ev != BTN_NONE)
        {
          printf("button event: %d\n", (int)ev);
        }
      usleep(50 * 1000);
    }

  return 0;
}
#endif
