/****************************************************************************
 * FOCUS AIoT - Mock 桩实现 (core/mock.c)
 *
 * 职责: 为各模块提供假数据，使整条链路可脱离硬件和云端独立运行。
 *       每个 mock 模拟对应模块的输出格式。
 *
 * 后续: 每个 USE_REAL_* 开关打开时，对应 mock 函数被真实实现替换。
 ****************************************************************************/

#include "mock.h"
#include "../api/error.h"
#include <stdio.h>
#include <string.h>

/* ---- Mock 感知 ---- */

static int g_mock_obs_index = 0;
static const observation_t g_mock_obs_sequence[] = {
    {.person_present=true, .head_pitch=-5.0f, .hand_motion_score=0.1f,
     .confidence=0.95f, .timestamp_ms=1000},
    {.person_present=true, .phone_detected=true, .phone_near_hand=true,
     .head_pitch=-20.0f, .hand_motion_score=0.7f, .confidence=0.85f,
     .timestamp_ms=6000},
    {.person_present=false, .confidence=0.90f, .timestamp_ms=11000},
    {.person_present=true, .head_pitch=48.0f, .hand_motion_score=0.05f,
     .confidence=0.78f, .timestamp_ms=16000},
    {.person_present=true, .head_pitch=-3.0f, .hand_motion_score=0.1f,
     .confidence=0.96f, .timestamp_ms=21000},
};
#define MOCK_OBS_COUNT (sizeof(g_mock_obs_sequence) / sizeof(g_mock_obs_sequence[0]))

void mock_perception_init(void)
{
    g_mock_obs_index = 0;
}

int mock_perception_process(observation_t *out)
{
    if (!out) return FOCUS_ERR_PARAM;
    *out = g_mock_obs_sequence[g_mock_obs_index];
    g_mock_obs_index = (g_mock_obs_index + 1) % MOCK_OBS_COUNT;
    return FOCUS_OK;
}

/* ---- Mock 行为 ---- */

void mock_behavior_init(void)
{
    /* no-op */
}

/* 简单直接映射 (不做真实时序分析，仅验证链路) */
study_state_t mock_behavior_analyze(const observation_t *obs)
{
    study_state_t ss;
    memset(&ss, 0, sizeof(ss));

    if (obs == NULL)
    {
        ss.status  = FOCUSED;
        ss.action  = NONE;
        return ss;
    }

    if (!obs->person_present)
    {
        ss.status  = AWAY;
        ss.action  = REMIND;
        snprintf(ss.message, sizeof(ss.message), "%s", "请回到座位!");
        ss.focus_score_delta = -10;
    }
    else if (obs->phone_detected && obs->phone_near_hand
             && obs->hand_motion_score > 0.3)
    {
        ss.status  = PLAYING_PHONE;
        ss.action  = REMIND;
        snprintf(ss.message, sizeof(ss.message), "%s", "请放下手机!");
        ss.focus_score_delta = -15;
    }
    else if (obs->head_pitch > 40.0f)
    {
        ss.status  = DROWSY;
        ss.action  = REMIND;
        snprintf(ss.message, sizeof(ss.message), "%s", "请注意坐姿!");
        ss.focus_score_delta = -20;
    }
    else
    {
        ss.status  = FOCUSED;
        ss.action  = NONE;
    }

    return ss;
}

/* ---- Mock UI ---- */

void mock_ui_show(void)
{
    /* 真实 LCD 未接入时，状态由 state_machine_tick() 内 printf 替代 */
}

/* ---- Mock 音频 ---- */

void mock_audio_play(const char *msg)
{
    if (msg)
    {
        printf("[audio/mock] %s\n", msg);
    }
}

/* ---- Mock MiMo ---- */

int mock_mimo_get_advice(const session_stats_t *stats,
                         const uint8_t distraction_by_type[4],
                         char *advice_out, size_t max_len)
{
    if (!advice_out || max_len == 0) return FOCUS_ERR_PARAM;
    (void)distraction_by_type;

    if (stats->current_mode == MODE_GENTLE)
    {
        snprintf(advice_out, max_len,
                 "你今天有效学习了 %u 分钟, 已经很棒了! 继续加油!",
                 stats->effective_duration_sec / 60);
    }
    else
    {
        snprintf(advice_out, max_len,
                 "严格模式下你坚持了 %u 分钟, 下次减少分心次数会更好!",
                 stats->effective_duration_sec / 60);
    }

    return FOCUS_OK;
}

/* ---- Mock 状态机 ---- */

#include "../api/state_machine.h"

static device_status_t g_mock_device_status = DEVICE_IDLE;

void state_machine_init(void)
{
    g_mock_device_status = DEVICE_IDLE;
    printf("[state_machine] init -> IDLE\n");
}

device_status_t state_machine_get_status(void)
{
    return g_mock_device_status;
}

const session_stats_t *state_machine_get_stats(void)
{
    static session_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    return &stats;
}

int state_machine_get_mode(void)
{
    return MODE_STRICT;
}

bool state_machine_is_paused(void)
{
    return false;
}

void state_machine_tick(void)
{
    /* MVP mock: no-op */
}

/* ---- Mock 摄像头 ---- */

void mock_camera_tick(void)
{
    /* 由主循环驱动, 无额外操作 */
}
