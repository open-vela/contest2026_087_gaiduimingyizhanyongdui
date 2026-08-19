/****************************************************************************
 * FOCUS AIoT - 真实音频驱动 (hardware/audio_i2s.c)
 *
 * 负责人: 张沐泽
 * 职责: 提醒音/蜂鸣降级。REMIND 触发蜂鸣, TTS 不可用时自动降级为蜂鸣。
 *
 * 硬件事实 (已核实):
 *   - defconfig 里 I2S0 仅配 RX(麦克风输入, DIN=2/BCLK=41/WS=42),
 *     未配置扬声器输出(I2S0_TX 关闭), board.h 亦无 codec/扬声器引脚。
 *   - 因此本板当前无 I2S 扬声器通路, 音频输出统一降级为 PWM/GPIO 蜂鸣。
 *
 * ⚠️ 上板核验: 蜂鸣器 GPIO 需按 ESP32-S3-EYE 原理图确认 (见下方宏)。
 ****************************************************************************/

#include "../api/audio.h"
#include "../api/error.h"

#include <nuttx/config.h>
#include <nuttx/leds/userled.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <stddef.h>
#include <stdint.h>

/*
 * ⚠️ 2026-08-19 真机核验结论: ESP32-S3-EYE 板载无蜂鸣器/扬声器
 * (board.h 无相关引脚, I2S0 仅 RX 麦克风)。原 GPIO46 蜂鸣假设无效。
 * 音频输出统一降级为板载 Power LED 闪烁 (/dev/userleds, 已验证可用),
 * 作为提醒的视觉反馈。如需真实声音, 可外接蜂鸣器到任意 GPIO 并
 * 在 buzzer 分支改为 GPIO 翻转 (见下方 AUDIO_BUZZER_GPIO 占位)。
 */

static int g_led_fd = -1;

/* 闪烁 n 次: on_ms 亮 / off_ms 灭 */
static void led_blink(unsigned int times, unsigned int on_ms, unsigned int off_ms)
{
  unsigned int i;

  for (i = 0; i < times; i++)
    {
      if (g_led_fd >= 0)
        {
          ioctl(g_led_fd, ULEDIOC_SETALL, (unsigned long)1);  /* 亮 */
        }
      usleep(on_ms * 1000);

      if (g_led_fd >= 0)
        {
          ioctl(g_led_fd, ULEDIOC_SETALL, (unsigned long)0);  /* 灭 */
        }
      usleep(off_ms * 1000);
    }
}

/****************************************************************************
 * Name: audio_init
 ****************************************************************************/
void audio_init(void)
{
  /* 打开板载 LED, 用作提醒视觉反馈 */
  g_led_fd = open("/dev/userleds", O_WRONLY);
  if (g_led_fd < 0)
    {
      g_led_fd = -1;
    }
  else
    {
      ioctl(g_led_fd, ULEDIOC_SETALL, (unsigned long)0);
    }
}

/****************************************************************************
 * Name: audio_play_pcm
 *
 * 无 I2S 扬声器通路, 返回未就绪 (调用方降级蜂鸣)。
 ****************************************************************************/
int audio_play_pcm(const uint8_t *data, size_t len)
{
  (void)data;
  (void)len;
  return FOCUS_ERR_HW_NOTREADY;
}

/****************************************************************************
 * Name: audio_play_tts
 *
 * 无 TTS 能力, 直接降级为蜂鸣。返回 -1 表示已降级。
 ****************************************************************************/
int audio_play_tts(const char *text)
{
  (void)text;
  audio_play_buzzer(AUDIO_BUZZER_WARN);
  return -1;
}

/****************************************************************************
 * Name: audio_play_buzzer
 *
 * pattern: AUDIO_BUZZER_WARN(1)=短促警告, AUDIO_BUZZER_ENCOURAGE(2)=柔和鼓励
 ****************************************************************************/
void audio_play_buzzer(int pattern)
{
  if (pattern == AUDIO_BUZZER_ENCOURAGE)
    {
      /* 鼓励: 一次较长闪烁 */
      led_blink(1, 500, 200);
    }
  else
    {
      /* 警告: 三次短促闪烁 */
      led_blink(3, 150, 100);
    }
}

/****************************************************************************
 * Name: 独立测试 (编译时 -DTEST_AUDIO)
 ****************************************************************************/
#ifdef TEST_AUDIO
#include <stdio.h>

int main(void)
{
  audio_init();
  printf("Audio test: 警告音\n");
  audio_play_buzzer(AUDIO_BUZZER_WARN);
  printf("Audio test: 鼓励音\n");
  audio_play_buzzer(AUDIO_BUZZER_ENCOURAGE);
  return 0;
}
#endif
