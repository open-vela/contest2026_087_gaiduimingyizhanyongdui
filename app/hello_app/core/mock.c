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

/* ---- Mock 按键 (张沐泽接口的占位实现) ---- */

#include "../api/button.h"

static int g_mock_btn_tick = 0;

button_event_t button_get_event(void)
{
    /* 模拟按键序列: 让 FSM 走完一次完整学习流程。
     * 真实实现由张沐泽提供 (GPIO 中断 + 长短按判定)。 */
    button_event_t ev = BTN_NONE;
    g_mock_btn_tick++;

    switch (g_mock_btn_tick)
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

/* ---- Mock 行为分析 (赵思涵接口的占位实现) ---- */

void behavior_set_mode(int mode)
{
    (void)mode;
    printf("[behavior/mock] set_mode -> %s\n",
           mode == MODE_GENTLE ? "GENTLE" : "STRICT");
}

/* behavior_analyze(): 每 tick 调用一次, 轮转输出 study_state。
 * 真实实现由赵思涵提供 (消费 perception_get_history 做时序分析)。
 *
 * mock 模拟真实行为的冷却逻辑: 每个状态持续 ~1 秒 (10 tick),
 * 但 REMIND 只在进入该状态的第一 tick 触发, 后续 tick 返回 FOCUSED,
 * 避免同一分心被重复计数。真实冷却由赵思涵的 remind_cooldown_sec 保证。 */
study_state_t behavior_analyze(void)
{
    static int call         = 0;
    static int idx          = 0;
    static int tick_in_state = 0;
    static const study_state_t seq[] = {
        {FOCUSED,       NONE,     "",                false, 0, 0},
        {PLAYING_PHONE, REMIND,   "请放下手机!",     false, 0, -15},
        {FOCUSED,       NONE,     "",                false, 0, 0},
        {AWAY,          REMIND,   "请回到座位!",     false, 0, -10},
        {FOCUSED,       NONE,     "",                false, 0, 0},
        {DROWSY,        REMIND,   "请注意坐姿!",     false, 0, -20},
        {FOCUSED,       NONE,     "",                false, 0, 0},
    };
    const int SEQ_N = (int)(sizeof(seq) / sizeof(seq[0]));
    study_state_t s;

    call++;
    tick_in_state++;

    /* 每 10 tick (约 1 秒) 轮转到下一个状态 */
    if ((call % 10) == 0)
      {
        idx = (idx + 1) % SEQ_N;
        tick_in_state = 0;
      }

    s = seq[idx];

    /* 冷却模拟: REMIND 只在进入状态的第一 tick 返回, 其余返回 FOCUSED */
    if (s.action == REMIND && tick_in_state > 0)
      {
        s.status           = FOCUSED;
        s.action           = NONE;
        s.message[0]       = '\0';
        s.focus_score_delta = 0;
      }

    return s;
}

/* ---- Mock 音频 (张沐泽接口的占位实现) ---- */

#include "../api/audio.h"

int audio_play_tts(const char *text)
{
    printf("[audio/mock/TTS] %s\n", text ? text : "");
    return FOCUS_OK;
}

void audio_play_buzzer(int pattern)
{
    printf("[audio/mock/buzzer] pattern=%d\n", pattern);
}

int audio_play_pcm(const uint8_t *data, size_t len)
{
    (void)data;
    (void)len;
    return FOCUS_OK;
}

/* ---- Mock 摄像头 ---- */

void mock_camera_tick(void)
{
    /* 由主循环驱动, 无额外操作 */
}
