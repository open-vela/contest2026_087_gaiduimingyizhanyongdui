/****************************************************************************
 * FOCUS AIoT - 学习报告接口 (api/session.h)
 *
 * 负责人: 万思源
 * 职责: 学习结束后生成的完整报告，供 UI 和 MiMo 消费。
 *
 * 冻结: session_report_t 字段。
 ****************************************************************************/

#ifndef FOCUS_AIOT_API_SESSION_H
#define FOCUS_AIOT_API_SESSION_H

#include <stdint.h>
#include "state_machine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 分心类型索引 ---- */
enum distraction_type {
    DIST_PLAYING_PHONE = 0,   /* 玩手机   */
    DIST_GLANCING_PHONE,      /* 看手机   */
    DIST_AWAY,                /* 离座     */
    DIST_DROWSY,              /* 瞌睡     */
    DIST_TYPE_COUNT           /* 类型总数  */
};

/* ---- session_report_t: 学习结束报告 ---- */

typedef struct {
    session_stats_t stats;                          /* 基础统计       */
    uint8_t  distraction_by_type[DIST_TYPE_COUNT];   /* 分心类型计数   */
    char     advice[512];                            /* MiMo 建议文本  */
    /* --- v2 追加 --- */
    uint8_t  _reserved[64];
} session_report_t;

#ifdef __cplusplus
}
#endif

#endif /* FOCUS_AIOT_API_SESSION_H */
