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
#include "core/mock.h"

/* ==================== 模块桩调用 (MVP mock) ==================== */

/* 当前阶段: 各模块用 mock 实现，state_machine 已真实化。
 * 后续各模块负责人提交真实 .c 后，mock 桩自动被替换。 */

static void init_modules(void)
{
    session_stats_t initial_stats;
    study_state_t initial_study;

    printf("[perception] init (mock mode)\n");
    mock_perception_init();

    printf("[behavior]  init mode=STRICT\n");
    behavior_init(MODE_STRICT);

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
    printf("    button/audio          - mock 占位 (待张沐泽)\n");
    printf("    behavior              - 赵思涵时序引擎\n");
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
