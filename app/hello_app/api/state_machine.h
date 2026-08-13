/****************************************************************************
 * FOCUS AIoT - 状态机与会话统计接口 (api/state_machine.h)
 *
 * 负责人: 万思源
 * 职责: 4 状态 FSM、多任务调度、会话统计、报告生成。
 *
 * 冻结: device_status_t / session_stats_t / 函数签名。
 ****************************************************************************/

#ifndef FOCUS_AIOT_API_STATE_MACHINE_H
#define FOCUS_AIOT_API_STATE_MACHINE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 设备状态 (4 状态 FSM) ---- */

typedef enum device_status_e {
    DEVICE_IDLE,          /* 待机                    */
    DEVICE_MODE_SELECT,   /* 选择模式 (严格/鼓励)    */
    DEVICE_MONITORING,    /* 监测中                  */
    DEVICE_REPORT         /* 报告页                  */
} device_status_t;

/* ---- 会话统计数据 ---- */

typedef struct session_stats_s {
    uint32_t session_start;          /* 学习开始时间戳 (ms)     */
    uint32_t total_duration_sec;     /* 总时长 (秒)             */
    uint32_t effective_duration_sec; /* 有效学习时长 (秒)       */
    uint8_t  distraction_count;      /* 分心总次数              */
    uint8_t  focus_score;            /* 专注度评分 0-100        */
    uint8_t  current_mode;           /* 0=STRICT, 1=GENTLE     */
    /* --- v2 追加 --- */
    uint8_t  _reserved[32];
} session_stats_t;

/* ---- 接口函数 ---- */

/* 初始化状态机 (进入 IDLE)。 */
void state_machine_init(void);

/* 每个主循环 tick 调用一次 (约 100Hz)。
 * 内部: 轮询按键、消费 study_state_t、更新统计、驱动 UI 刷新。
 * 必须在 20ms 内返回。 */
void state_machine_tick(void);

/* 获取当前设备状态 (供 UI 查询)。 */
device_status_t state_machine_get_status(void);

/* 获取当前会话统计 (供 UI 查询)。 */
const session_stats_t *state_machine_get_stats(void);

/* 获取当前模式。 */
int state_machine_get_mode(void);

/* 获取是否暂停中。 */
bool state_machine_is_paused(void);

#ifdef __cplusplus
}
#endif

#endif /* FOCUS_AIOT_API_STATE_MACHINE_H */
