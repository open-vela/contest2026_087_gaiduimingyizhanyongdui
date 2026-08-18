/****************************************************************************
 * FOCUS AIoT - 音频 mock 桩 (hardware/audio_stub.c)
 *
 * 负责人: 张沐泽
 * 说明: 弱符号提供 audio_init/audio_play_pcm/audio_play_tts/audio_play_buzzer，
 *       全部打印到串口。真实实现由 hardware/audio_i2s.c (强符号) 提供，
 *       Kconfig 二选一编译。
 ****************************************************************************/

#include "../api/audio.h"
#include "../api/error.h"

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__GNUC__) && !defined(_WIN32)
#  define FOCUS_WEAK __attribute__((weak))
#else
#  define FOCUS_WEAK
#endif

FOCUS_WEAK void audio_init(void)
{
  /* no-op */
}

FOCUS_WEAK int audio_play_pcm(const uint8_t *data, size_t len)
{
  (void)data;
  (void)len;
  return FOCUS_OK;
}

FOCUS_WEAK int audio_play_tts(const char *text)
{
  printf("[audio/mock/TTS] %s\n", text ? text : "");
  return FOCUS_OK;
}

FOCUS_WEAK void audio_play_buzzer(int pattern)
{
  printf("[audio/mock/buzzer] pattern=%d\n", pattern);
}
