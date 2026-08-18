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
#include <nuttx/arch.h>

#include <esp32s3_gpio.h>

#include <stddef.h>
#include <stdint.h>

/* TODO(上板核验): 按原理图确认蜂鸣器/扬声器 PWM 引脚, 默认占位 GPIO46 */
#ifndef AUDIO_BUZZER_GPIO
#  define AUDIO_BUZZER_GPIO 46
#endif

static void buzzer_beep(unsigned int freq_hz, unsigned int duration_ms)
{
  unsigned int half_period_ms;
  unsigned int elapsed;
  unsigned int toggle;

  if (freq_hz == 0)
    {
      return;
    }

  half_period_ms = 1000U / freq_hz / 2U;
  if (half_period_ms == 0)
    {
      half_period_ms = 1;
    }

  for (elapsed = 0, toggle = 0; elapsed < duration_ms; elapsed += half_period_ms)
    {
      esp32s3_gpiowrite(AUDIO_BUZZER_GPIO, toggle & 1);
      toggle++;
      up_mdelay(half_period_ms);
    }

  esp32s3_gpiowrite(AUDIO_BUZZER_GPIO, 0);
}

/****************************************************************************
 * Name: audio_init
 ****************************************************************************/
void audio_init(void)
{
  /* 蜂鸣器 GPIO 输出低电平 */
  esp32s3_configgpio(AUDIO_BUZZER_GPIO, OUTPUT_FUNCTION_1);
  esp32s3_gpiowrite(AUDIO_BUZZER_GPIO, 0);
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
      buzzer_beep(2500, 500);   /* 一声较长柔和音 */
    }
  else
    {
      /* 三声短促警告音 */
      buzzer_beep(3000, 150);
      up_mdelay(100);
      buzzer_beep(3000, 150);
      up_mdelay(100);
      buzzer_beep(3000, 150);
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
