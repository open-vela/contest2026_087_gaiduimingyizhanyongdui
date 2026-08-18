/****************************************************************************
 * FOCUS AIoT - 按键 mock 桩 (hardware/button_stub.c)
 *
 * 负责人: 张沐泽
 * 说明: 弱符号提供 button_init/button_get_event，返回 mock 按键序列以驱动 FSM。
 *       真实实现由 hardware/buttons.c (强符号) 提供，Kconfig 二选一编译。
 ****************************************************************************/

#include "../api/button.h"

#if defined(__GNUC__) && !defined(_WIN32)
#  define FOCUS_WEAK __attribute__((weak))
#else
#  define FOCUS_WEAK
#endif

static int g_stub_btn_tick = 0;

FOCUS_WEAK void button_init(void)
{
  /* no-op */
}

FOCUS_WEAK button_event_t button_get_event(void)
{
  /* 模拟按键序列: 让 FSM 走完一次完整学习流程。
   * 真实实现由张沐泽提供 (GPIO 中断 + 长短按判定)。 */
  button_event_t ev = BTN_NONE;
  g_stub_btn_tick++;

  switch (g_stub_btn_tick)
    {
      case 5:   ev = BTN_START_LONGPRESS;  break;  /* 进入模式选择 */
      case 10:  ev = BTN_START_SHORT;      break;  /* 确认，开始学习 */
      case 50:  ev = BTN_PAUSE_SHORT;      break;  /* 暂停 */
      case 60:  ev = BTN_PAUSE_SHORT;      break;  /* 恢复 */
      case 100: ev = BTN_PAUSE_LONGPRESS;  break;  /* 停止 → 报告 */
      case 110: ev = BTN_START_SHORT;      break;  /* 返回 IDLE */
      default:  ev = BTN_NONE;             break;
    }

  return ev;
}
