/****************************************************************************
 * FOCUS AIoT - LCD 抽象接口 (api/lcd.h)
 *
 * 负责人: 张沐泽 (底层驱动) + 郭黄亦昕 (UI 绘制)
 * 职责: 提供 framebuffer 访问和页面级显示函数。
 *
 * 冻结字段:
 *   - lcd_init / lcd_flush / lcd_get_framebuffer (张沐泽)
 *   - lcd_show_* (郭黄亦昕)
 *   - action_t 来自 behavior.h，UI 只显示 message，不自行生成文案
 ****************************************************************************/

#ifndef FOCUS_AIOT_API_LCD_H
#define FOCUS_AIOT_API_LCD_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "behavior.h"
#include "session.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 张沐泽 底层驱动接口 ---- */

/* 初始化 ST7789V LCD (SPI 40MHz)，分配 framebuffer (115,200 bytes RGB565)。
 * 返回: 0=成功 */
int  lcd_init(void);

/* 将 framebuffer 整体刷新到 LCD (SPI DMA 异步发送)。
 * 由 ui_task 以 1Hz 频率调用。50ms 内必须返回。
 * 返回: 0=成功 */
int  lcd_flush(void);

/* 获取 framebuffer 指针 (240×240×2 bytes RGB565)。
 * 郭黄亦昕直接在此缓冲区上绘制，再调 lcd_flush() 推到屏幕。 */
uint16_t *lcd_get_framebuffer(void);

/* ---- 郭黄亦昕 UI 页面级接口 ---- */

/* 更新监测主画面 (由状态机 1Hz 定时调用)。
 * st: 当前设备状态
 * s:  会话统计数据
 * ss: 赵思涵输出的 study_state (取最新一条)
 * 50ms 内必须返回。 */
int  lcd_show_status(device_status_t st, session_stats_t *s,
                     study_state_t *ss);

/* 显示学习报告 (学习结束调用一次)。
 * r:      报告数据
 * advice: MiMo 建议文本 (可为 NULL，表示未获取到)
 * 50ms 内必须返回。 */
int  lcd_show_report(session_report_t *r, const char *advice);

/* 显示全屏消息覆盖层 (提醒/鼓励时调用，持续 3 秒后自动消失)。
 * msg:  显示文本 (直接使用赵思涵输出的 study_state_t.message)
 * type: REMIND=警告样式, ENCOURAGE=鼓励样式
 * 100ms 内必须返回。 */
int  lcd_show_message(const char *msg, action_t type);

#ifdef __cplusplus
}
#endif

#endif /* FOCUS_AIOT_API_LCD_H */
