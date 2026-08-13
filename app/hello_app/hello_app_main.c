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

/* 当前阶段 MVP: 所有模块用 mock 实现。
 * 后续各模块负责人提交真实 .c 后，这里的调用自动切到真实实现。
 * 过渡期: 在各自的 .c 中 #ifdef USE_REAL percept_init / #else mock 实现。 */

/* 便利包装: 从 observation 直接得到 study_state (mock 快速通道) */
static study_state_t behavior_analyze_from_obs(const observation_t *obs)
{
    printf("[perception] process (mock)\n");
    return mock_behavior_analyze(obs);
}

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

/* ==================== 主事件循环 ==================== */

/* 观察 mock 链路:
 *   camera → perception → behavior → state_machine → ui/audio
 *   每个环节打印一行日志，便于验证链路完整性。
 */
static void run_mock_event_loop(void)
{
    int tick = 0;
    static const observation_t fake_frames[] = {
        {.person_present=true, .head_pitch=-5.0f,
         .hand_motion_score=0.1f, .confidence=0.95f},
        {.person_present=true, .head_pitch=-8.0f,
         .hand_motion_score=0.1f, .confidence=0.93f},
        {.person_present=true, .phone_detected=true, .phone_near_hand=true,
         .head_pitch=-15.0f, .hand_motion_score=0.5f, .confidence=0.88f},
        {.person_present=true, .phone_detected=true, .phone_near_hand=true,
         .head_pitch=-20.0f, .hand_motion_score=0.7f, .confidence=0.85f},
        {.person_present=false, .confidence=0.90f},
        {.person_present=false, .confidence=0.92f},
        {.person_present=true, .head_pitch=35.0f,
         .hand_motion_score=0.05f, .confidence=0.80f},
        {.person_present=true, .head_pitch=52.0f,
         .hand_motion_score=0.05f, .confidence=0.78f},
        {.person_present=true, .head_pitch=-3.0f,
         .hand_motion_score=0.15f, .confidence=0.96f},
        {.person_present=true, .head_pitch=-5.0f,
         .hand_motion_score=0.10f, .confidence=0.94f},
    };
    const int FAKE_COUNT = sizeof(fake_frames) / sizeof(fake_frames[0]);

    printf("\n==================================================\n");
    printf("  FOCUS AIoT mock 链路测试\n");
    printf("  观测序列: %d 帧\n", FAKE_COUNT);
    printf("==================================================\n\n");

    int frame_interval = 5;  /* 每 5 秒一帧 */
    int mode_ticks     = 120; /* 跑 120 tick = 10 分钟 (每秒 1 tick) */

    for (tick = 0; tick < mode_ticks; tick++)
    {
        /* ---- camera_task: 每 frame_interval 秒产一帧 ---- */
        if (tick % frame_interval == 0)
        {
            int idx = (tick / frame_interval) % FAKE_COUNT;
            const observation_t *obs = &fake_frames[idx];

            printf("┌─ [tick %3d] camera_frame #%d\n", tick, idx);

            /* ---- perception_task: 直接使用预制 observation ---- */
            printf("│  perception: person=%d phone=%d near=%d pitch=%.1f "
                   "motion=%.2f conf=%.2f\n",
                   obs->person_present, obs->phone_detected,
                   obs->phone_near_hand, obs->head_pitch,
                   obs->hand_motion_score, obs->confidence);

            /* ---- behavior_task: 分析观察 → 行为状态 ---- */
            study_state_t ss = behavior_analyze_from_obs(obs);
            printf("│  behavior: status=%d action=%d delta=%+d "
                   "milestone=%d(%dmin) msg=\"%.40s\"\n",
                   (int)ss.status, (int)ss.action, ss.focus_score_delta,
                   ss.milestone_reached, ss.milestone_minutes,
                   ss.message);

            session_stats_t ui_stats;
            memset(&ui_stats, 0, sizeof(ui_stats));
            ui_stats.total_duration_sec = (uint32_t)tick;
            ui_stats.effective_duration_sec = (uint32_t)tick;
            ui_stats.distraction_count = ss.action == REMIND ? 1 : 0;
            ui_stats.focus_score = ss.action == REMIND ? 80 : 100;
            ui_stats.current_mode = (uint8_t)g_current_mode;
            (void)lcd_show_status(DEVICE_MONITORING, &ui_stats, &ss);
            if (ss.action != NONE && ss.message[0] != '\0')
            {
                (void)lcd_show_message(ss.message, ss.action);
            }

            /* ---- state_machine: 消费 study_state ---- */
            if (ss.action == REMIND)
            {
                printf("│  state_machine: REMIND! → \"%s\"\n", ss.message);
                /* audio_task 触发 */
                printf("│  audio: [BUZZER] %s\n", ss.message);
            }
            else if (ss.action == ENCOURAGE)
            {
                printf("│  state_machine: ENCOURAGE! → \"%s\"\n", ss.message);
                printf("│  audio: [TONE] %s\n", ss.message);
            }
            else
            {
                printf("│  state_machine: FOCUSED / no action\n");
            }

            printf("└─\n");
        }

        /* ---- ui_task: 每 1 秒更新一次 ---- */
        if (tick % 1 == 0)
        {
            /* mock_ui_show() 输出精简状态行 */
        }

        sleep(1);
    }

    printf("\n==================================================\n");
    printf("  mock 链路测试完成。\n");
    printf("  确认日志中出现:\n");
    printf("    camera_frame → perception(preson/phone/pitch) →\n");
    printf("    behavior(status/action/delta) → state_machine(REMIND/ENCOURAGE)\n");
    printf("  以上即表示 mock 主链路跑通。\n");
    printf("==================================================\n\n");
}

/* ==================== 主入口 ==================== */

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    printf("\n");
    printf("==================================================\n");
    printf("  FOCUS AIoT v0.1 (MVP 骨架)\n");
    printf("  板卡: ESP32-S3-EYE / openvela\n");
    printf("  当前: 全 mock 链路验证模式\n");
    printf("==================================================\n");
    printf("  模块目录:\n");
    printf("    api/       - 接口契约 (已冻结)\n");
    printf("    core/      - 主控/状态机/任务调度\n");
    printf("    hardware/  - 张沐泽 硬件驱动\n");
    printf("    perception/- 周礼航 视觉感知\n");
    printf("    behavior/  - 赵思涵 行为分析\n");
    printf("    ui/        - 郭黄亦昕 LCD/UI/MiMo\n");
    printf("    config/    - 模式配置表\n");
    printf("    tests/     - 模块级 mock 测试\n");
    printf("==================================================\n\n");

    /* ---- 模块初始化 ---- */
    init_modules();
    state_machine_init();

    printf("[core] 模块初始化完成，进入 mock 事件循环...\n");

    /* ---- 跑 mock 主链路 ---- */
    run_mock_event_loop();

    return EXIT_SUCCESS;
}
