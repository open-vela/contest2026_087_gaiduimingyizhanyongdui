/****************************************************************************
 * FOCUS AIoT - 按键抽象接口 (api/button.h)
 *
 * 负责人: 张沐泽
 * 职责: 3 键 GPIO 事件消抖、长短按判定、非阻塞读取。
 *
 * 冻结: button_event_t 枚举值、button_get_event() 签名。
 ****************************************************************************/

#ifndef FOCUS_AIOT_API_BUTTON_H
#define FOCUS_AIOT_API_BUTTON_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 按键事件枚举 ---- */

typedef enum {
    BTN_NONE = 0,           /* 无事件 */
    BTN_START_SHORT,        /* 按键 1 (开始) 短按 <1s */
    BTN_START_LONGPRESS,    /* 按键 1 (开始) 长按 ≥1s */
    BTN_MODE,               /* 按键 2 (模式) 短按     */
    BTN_PAUSE_SHORT,        /* 按键 3 (暂停) 短按 <2s  */
    BTN_PAUSE_LONGPRESS,    /* 按键 3 (暂停) 长按 ≥2s  */
    BTN_STOP,               /* 按键 3 用作停止键 (>3s)  */
} button_event_t;

/* ---- 接口 ---- */

/* 初始化 GPIO 按键 (上拉输入 + 双边沿中断)。
 * 3 个按键引脚映射以原理图为准。 */
void button_init(void);

/* 非阻塞读取按键事件。返回 BTN_NONE 表示无事件。 */
button_event_t button_get_event(void);

#ifdef __cplusplus
}
#endif

#endif /* FOCUS_AIOT_API_BUTTON_H */
