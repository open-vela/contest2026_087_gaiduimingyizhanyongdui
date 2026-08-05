/****************************************************************************
 * FOCUS AIoT - 行为分析配置表 (config/behavior_config.h)
 *
 * 负责人: 赵思涵
 * 职责: 严格/鼓励模式参数表 + 话术表。
 *       所有阈值、冷却、扣分/加分、话术统一在这里管理。
 *
 * 冻结: mode_config_t 结构。参数可以调，字段可以加。
 ****************************************************************************/

#ifndef FOCUS_AIOT_CONFIG_BEHAVIOR_CONFIG_H
#define FOCUS_AIOT_CONFIG_BEHAVIOR_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "../api/behavior.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 模式配置结构体 ---- */

typedef struct {
    /* 判定阈值 */
    int  phone_glance_sec;       /* 看手机判定阈值 (秒)    */
    int  phone_playing_sec;      /* 玩手机判定阈值 (秒)    */
    int  away_threshold_sec;     /* 离座判定阈值 (秒)      */
    int  drowsy_threshold_sec;   /* 瞌睡判定阈值 (秒)      */
    int  head_pitch_max;         /* 正常头部俯角上限 (度)   */

    /* 提醒策略 */
    int  remind_cooldown_sec;    /* 两次提醒最小间隔 (秒)   */
    int  remind_after_n_times;   /* 累计 N 次分心才提醒     */

    /* 鼓励模式专属 */
    int  focus_milestone_min;    /* 专注里程碑间隔 (分钟, 0=禁用) */
    bool enable_milestone;       /* 是否启用里程碑鼓励       */

    /* 评分 */
    int  score_penalty_phone;    /* 玩手机扣分              */
    int  score_penalty_away;     /* 离座扣分                */
    int  score_penalty_drowsy;   /* 瞌睡扣分                */
    int  score_bonus_milestone;  /* 专注里程碑加分          */

    /* 话术 */
    const char **messages;       /* 提醒话术表 (status_t 索引) */
} mode_config_t;

/* ---- 话术表 ---- */

extern const char *strict_messages[5];
extern const char *gentle_messages[5];

/* ---- 两套配置 ---- */

extern mode_config_t g_mode_configs[2];  /* [MODE_STRICT] [MODE_GENTLE] */

/* ---- 快捷访问 ---- */

/* 获取当前模式的配置表 (由行为模块维护) */
const mode_config_t *behavior_get_current_config(void);

/* 获取指定模式的配置表 */
const mode_config_t *behavior_get_config(int mode);

#ifdef __cplusplus
}
#endif

#endif /* FOCUS_AIOT_CONFIG_BEHAVIOR_CONFIG_H */
