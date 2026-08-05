/****************************************************************************
 * FOCUS AIoT - Mock 数据提供者 (core/mock.h)
 *
 * 职责: 在真实硬件/云端未就绪时，为各模块提供假数据。
 *       每个 mock 函数模拟对应模块的真实输出，使整条链路可独立调试。
 *
 * 使用: #define USE_REAL_* 可用 CMake / Kconfig 控制，默认全部用 mock。
 ****************************************************************************/

#ifndef FOCUS_AIOT_CORE_MOCK_H
#define FOCUS_AIOT_CORE_MOCK_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../api/perception.h"
#include "../api/behavior.h"
#include "../api/session.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Mock 摄像头 ==================== */

/* 每 5 秒产生一条假帧事件 (实际无图像数据，只发信号) */
void mock_camera_tick(void);

/* ==================== Mock 感知 ==================== */

/* 初始化 mock 感知 (设置 observation 循环序列) */
void mock_perception_init(void);

/* 输出一个假 observation_t (轮流: FOCUSED/PLAYING_PHONE/AWAY/DROWSY) */
int  mock_perception_process(observation_t *out);

/* ==================== Mock 行为 ==================== */

/* 初始化 mock 行为分析 */
void mock_behavior_init(void);

/* 基于 observation 产生 study_state (直接映射，不做真实时序) */
study_state_t mock_behavior_analyze(const observation_t *obs);

/* ==================== Mock UI ==================== */

/* 打印状态到串口 (替代 LCD 显示) */
void mock_ui_show(void);

/* ==================== Mock 音频 ==================== */

/* 打印提醒文字到串口 (替代真实播放) */
void mock_audio_play(const char *msg);

/* ==================== Mock MiMo ==================== */

/* 用本地模板生成建议 */
int  mock_mimo_get_advice(const session_stats_t *stats,
                          const uint8_t distraction_by_type[4],
                          char *advice_out, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* FOCUS_AIOT_CORE_MOCK_H */
