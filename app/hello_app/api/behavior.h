/****************************************************************************
 * FOCUS AIoT - 行为分析模块接口 (api/behavior.h)
 *
 * 负责人: 赵思涵
 * 职责: 从 Observation 时间序列中推理学习状态，严格/鼓励双模式引擎。
 *       pure function: 不访问网络、不操作硬件、10ms 内返回。
 *
 * 冻结: status_t / action_t / study_state_t / 3 个函数签名。
 *       mode_config_t 由行为模块内部管理 (见 config/behavior_config.h)。
 ****************************************************************************/

#ifndef FOCUS_AIOT_API_BEHAVIOR_H
#define FOCUS_AIOT_API_BEHAVIOR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 学习状态枚举 ---- */

typedef enum {
    FOCUSED,          /* 专注学习               */
    GLANCING_PHONE,   /* 短暂看手机 (不提醒)     */
    PLAYING_PHONE,    /* 玩手机 (提醒)           */
    AWAY,             /* 离座                    */
    DROWSY            /* 低头/瞌睡               */
} status_t;

/* ---- 行动类型 ---- */

typedef enum {
    NONE,             /* 不需要行动              */
    REMIND,           /* 触发提醒 (严格:警告 / 鼓励:关怀) */
    ENCOURAGE         /* 触发鼓励 (仅鼓励模式, 专注里程碑) */
} action_t;

/* ---- study_state_t: 行为分析输出 ---- */

typedef struct {
    status_t  status;              /* 当前学习状态            */
    action_t  action;              /* 应采取的行动            */
    char      message[128];        /* 提醒/鼓励文字
                                    * ★ 郭黄亦昕直接显示此文本，
                                    *   不自行生成文案       */
    bool      milestone_reached;   /* 是否达到专注里程碑
                                    * (仅鼓励模式)           */
    int       milestone_minutes;   /* 连续专注分钟数          */
    int       focus_score_delta;   /* 评分变化量
                                    * (正=加分, 负=扣分, 增量) */
} study_state_t;

/* ---- 接口函数 ---- */

/* 初始化，设置当前模式。
 * mode: 0=MODE_STRICT, 1=MODE_GENTLE */
void behavior_init(int mode);

/* 核心函数: 消费 Observation 历史 → 输出行为状态。
 * 纯计算，10ms 内返回。
 * 在 wifi_task 每收到新 Observation 后调用一次。 */
study_state_t behavior_analyze(void);

/* 切换模式 (运行时用户通过按键切换)。
 * 可在监测进行中被调用，不要求重启。 */
void behavior_set_mode(int mode);

/* ---- 模式常量 ---- */
#define MODE_STRICT  0
#define MODE_GENTLE  1

#ifdef __cplusplus
}
#endif

#endif /* FOCUS_AIOT_API_BEHAVIOR_H */
