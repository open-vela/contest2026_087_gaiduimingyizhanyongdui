/****************************************************************************
 * FOCUS AIoT - 行为分析配置表 (behavior/behavior_config.c)
 *
 * 负责人: 赵思涵
 *
 * 严格/鼓励两套策略只通过配置和话术区分，行为分析引擎不复制两份。
 ****************************************************************************/

#include "../config/behavior_config.h"

/* ---- 提醒话术 ---- */

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

/* ---- 两套模式配置 ---- */

mode_config_t g_mode_configs[2] = {
    [MODE_STRICT] = {
        .phone_glance_sec      = 8,
        .phone_playing_sec     = 8,
        .away_threshold_sec    = 10,
        .drowsy_threshold_sec  = 8,
        .head_pitch_max        = 45,
        .remind_cooldown_sec   = 30,
        .remind_after_n_times  = 1,
        .focus_milestone_min   = 0,
        .enable_milestone      = false,
        .score_penalty_phone   = -15,
        .score_penalty_away    = -10,
        .score_penalty_drowsy  = -20,
        .score_bonus_milestone = 0,
        .messages              = strict_messages,
    },
    [MODE_GENTLE] = {
        .phone_glance_sec      = 15,
        .phone_playing_sec     = 15,
        .away_threshold_sec    = 20,
        .drowsy_threshold_sec  = 12,
        .head_pitch_max        = 52,
        .remind_cooldown_sec   = 120,
        .remind_after_n_times  = 3,
        .focus_milestone_min   = 30,
        .enable_milestone      = true,
        .score_penalty_phone   = -5,
        .score_penalty_away    = -3,
        .score_penalty_drowsy  = -8,
        .score_bonus_milestone = 5,
        .messages              = gentle_messages,
    },
};
