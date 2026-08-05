/****************************************************************************
 * FOCUS AIoT - 音频抽象接口 (api/audio.h)
 *
 * 负责人: 张沐泽
 * 职责: I2S 音频输出、预录 PCM 播放、TTS 合成播放。
 *
 * 冻结: 所有函数签名。降级策略由实现决定 (TTS 失败→蜂鸣器)。
 ****************************************************************************/

#ifndef FOCUS_AIOT_API_AUDIO_H
#define FOCUS_AIOT_API_AUDIO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 接口 ---- */

/* 初始化 I2S 音频输出 (16kHz / 16bit / 单声道)。 */
void audio_init(void);

/* 播放预录音频 (从 Flash 读取 PCM 数据，DMA 发送)。
 * 返回: 0=成功 */
int  audio_play_pcm(const uint8_t *data, size_t len);

/* 播放 TTS 合成的语音 (调用云端 TTS API)。
 * text: 要播放的文字
 * 返回: 0=成功, -1=TTS 失败则自动降级为蜂鸣器 */
int  audio_play_tts(const char *text);

/* 蜂鸣器降级提示音 (不同频率/节奏区分警告/鼓励)。 */
void audio_play_buzzer(int pattern);

/* 预定义蜂鸣器 pattern */
#define AUDIO_BUZZER_WARN     1   /* 短促警告 */
#define AUDIO_BUZZER_ENCOURAGE 2  /* 柔和鼓励 */

#ifdef __cplusplus
}
#endif

#endif /* FOCUS_AIOT_API_AUDIO_H */
