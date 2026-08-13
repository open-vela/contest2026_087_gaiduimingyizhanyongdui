/****************************************************************************
 * FOCUS AIoT - 主控入口 (多任务事件循环骨架)
 *
 * 负责人: 万思源
 *
 * 架构: 单线程事件循环驱动 6 个逻辑 task。
 *   camera_task    → 采集 / mock 帧事件 (每 5s)
 *   perception_task → 消费帧 → observation_t
 *   behavior_task   → 消费 observation → study_state_t
 *   state_machine   → 消费 study_state + 按键 → FSM + 统计
 *   ui_task         → 1Hz 刷新 LCD / 串口日志
 *   audio_task      → 提醒 / 鼓励触发
 *
 * 当前阶段 (MVP): 全部 mock，只验证链路日志。
 * 后续逐步 #define USE_REAL_* 替换真实模块。
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "api/error.h"
#include "api/lcd.h"
#include "api/perception.h"
#include "api/behavior.h"
#include "api/state_machine.h"
#include "api/session.h"
#include "config/behavior_config.h"
#include "core/mock.h"

/* ==================== 模式话术表实现 ==================== */

const char *strict_messages[5] = {
    [PLAYING_PHONE] = "请放下手机!",
    [AWAY]          = "请回到座位!",
    [DROWSY]        = "请注意坐姿!",
};

const char *gentle_messages[5] = {
    [PLAYING_PHONE] = "休息好了就继续吧~",
    [AWAY]          = "等你回来哦~",
    [DROWSY]        = "要不要起来活动一下?",
};

/* ==================== 模式配置表实现 ==================== */

mode_config_t g_mode_configs[2] = {
    [MODE_STRICT] = {
        .phone_glance_sec     = 8,
        .phone_playing_sec    = 8,
        .away_threshold_sec   = 10,
        .drowsy_threshold_sec = 8,
        .head_pitch_max       = 45,
        .remind_cooldown_sec  = 30,
        .remind_after_n_times = 1,
        .focus_milestone_min  = 0,
        .enable_milestone     = false,
        .score_penalty_phone  = -15,
        .score_penalty_away   = -10,
        .score_penalty_drowsy = -20,
        .score_bonus_milestone = 0,
        .messages             = strict_messages,
    },
    [MODE_GENTLE] = {
        .phone_glance_sec     = 15,
        .phone_playing_sec    = 15,
        .away_threshold_sec   = 20,
        .drowsy_threshold_sec = 12,
        .head_pitch_max       = 52,
        .remind_cooldown_sec  = 120,
        .remind_after_n_times = 3,
        .focus_milestone_min  = 30,
        .enable_milestone     = true,
        .score_penalty_phone  = -5,
        .score_penalty_away   = -3,
        .score_penalty_drowsy = -8,
        .score_bonus_milestone = 5,
        .messages             = gentle_messages,
    },
};

/* 当前模式 (行为模块持有，这里导出引用) */
static int g_current_mode = MODE_STRICT;
static const mode_config_t *g_cfg = &g_mode_configs[MODE_STRICT];

const mode_config_t *behavior_get_current_config(void)
{
    return g_cfg;
}

const mode_config_t *behavior_get_config(int mode)
{
    if (mode == MODE_GENTLE) return &g_mode_configs[MODE_GENTLE];
    return &g_mode_configs[MODE_STRICT];
}

/* ==================== 模块桩调用 (MVP mock) ==================== */

/* 当前阶段: 各模块用 mock 实现，state_machine 已真实化。
 * 后续各模块负责人提交真实 .c 后，mock 桩自动被替换。 */

static void init_modules(void)
{
    session_stats_t initial_stats;
    study_state_t initial_study;

    printf("[perception] init (mock mode)\n");
    mock_perception_init();

    g_current_mode = MODE_STRICT;
    g_cfg          = &g_mode_configs[MODE_STRICT];
    printf("[behavior]  init mode=STRICT\n");
    mock_behavior_init();

    if (lcd_init() == FOCUS_OK)
    {
        memset(&initial_stats, 0, sizeof(initial_stats));
        memset(&initial_study, 0, sizeof(initial_study));
        initial_study.status = FOCUSED;
        if (lcd_show_status(DEVICE_IDLE, &initial_stats,
                            &initial_study) == FOCUS_OK)
        {
            printf("[ui]        LCD UI initialized\n");
        }
        else
        {
            printf("[ui]        LCD UI unavailable\n");
        }
    }
    else
    {
        printf("[ui]        LCD backend unavailable\n");
    }
}

/* ==================== 主入口 ==================== */

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    printf("\n");
    printf("==================================================\n");
    printf("  FOCUS AIoT v0.2 (状态机 + 会话统计)\n");
    printf("  板卡: ESP32-S3-EYE / openvela\n");
    printf("  负责人: 万思源 (state_machine)\n");
    printf("==================================================\n");
    printf("  模块状态:\n");
    printf("    state_machine  - 真实 FSM (core/state_machine.c)\n");
    printf("    ui             - 郭黄亦昕 (已集成)\n");
    printf("    button/behavior/audio - mock 占位 (待张沐泽/赵思涵)\n");
    printf("    perception     - mock 占位 (待周礼航)\n");
    printf("==================================================\n\n");

    /* ---- 模块初始化 ---- */
    init_modules();
    state_machine_init();

    printf("[core] 进入主循环 (state_machine_tick @ 10Hz)...\n");
    printf("[core] mock 按键序列将自动驱动一次完整学习流程:\n");
    printf("[core]   IDLE -> MODE_SELECT -> MONITORING(暂停/恢复) -> REPORT -> IDLE\n\n");

    /* ---- 主循环: state_machine_tick 驱动 ---- */
    for (;;)
    {
        state_machine_tick();
        usleep(100 * 1000);  /* 100ms = 10Hz */
    }

    return EXIT_SUCCESS;
}
