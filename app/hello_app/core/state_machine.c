/****************************************************************************
 * FOCUS AIoT - 主控状态机与会话统计 (core/state_machine.c)
 *
 * 负责人: 万思源
 * 职责: 4 状态 FSM (IDLE → MODE_SELECT → MONITORING → REPORT)
 *       消费按键事件 + 行为分析结果，驱动计时/评分/提醒/报告。
 *
 * 依赖 (通过 api 头文件):
 *   - button_get_event()  张沐泽
 *   - behavior_analyze()  赵思涵  ★ 每个 tick 只调用一次 (有状态)
 *   - behavior_set_mode() 赵思涵
 *   - lcd_show_*()        郭黄亦昕
 *   - audio_play_*()      张沐泽
 *
 * 设计要点:
 *   - state_machine_tick() 由主循环以 ~10Hz 调用。
 *   - 有效学习时长: 仅当 behavior 返回 FOCUSED 时累计 elapsed。
 *   - 评分只累加 behavior 输出的 focus_score_delta, 不自己算分。
 *   - 暂停期间不加有效时长、不触发提醒。
 *   - behavior_analyze() 每 tick 仅调用一次, 结果缓存供 UI 复用。
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../api/error.h"
#include "../api/button.h"
#include "../api/behavior.h"
#include "../api/state_machine.h"
#include "../api/session.h"
#include "../api/lcd.h"
#include "../api/audio.h"

/* ---- 常量 ---- */
#define UI_REFRESH_INTERVAL_MS 1000U  /* UI 刷新间隔 */
#define SCORE_INITIAL         100
#define SCORE_MIN             0
#define SCORE_MAX             100

/* ---- 内部状态 ---- */
static device_status_t g_state      = DEVICE_IDLE;
static int             g_mode       = MODE_STRICT;
static bool            g_paused     = false;

static session_stats_t g_stats;
static uint8_t         g_distraction_by_type[DIST_TYPE_COUNT];
static int             g_score = SCORE_INITIAL;
static session_report_t g_report;

/* 最新一次 behavior 分析结果 (缓存供 UI 复用) */
static study_state_t   g_latest_study;

/* 时间基准 (单调时钟, 毫秒) */
static uint64_t g_last_tick_ms   = 0;   /* 上次 tick 时间 */
static uint64_t g_last_ui_ms     = 0;   /* 上次 UI 刷新时间 */
static uint64_t g_accumulated_ms = 0;   /* 未折算成秒的累计毫秒 */

/* ---- 前向声明 ---- */
static uint64_t monotonic_ms(void);
static void     transition_to(device_status_t next);
static void     start_session(void);
static void     end_session(void);
static void     reset_session(void);
static void     toggle_mode(void);
static void     process_study_state(const study_state_t *ss);
static void     trigger_remind(const study_state_t *ss);
static void     trigger_encourage(const study_state_t *ss);
static void     refresh_ui(void);

/* ==================== 单调时钟 ==================== */

static uint64_t monotonic_ms(void)
{
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
      return 0;
    }
  return (uint64_t)ts.tv_sec * 1000U + (uint64_t)ts.tv_nsec / 1000000U;
}

/* ==================== 状态转移 ==================== */

static void transition_to(device_status_t next)
{
  static const char *const names[] =
    { "IDLE", "MODE_SELECT", "MONITORING", "REPORT" };
  const char *from = (g_state >= DEVICE_IDLE && g_state <= DEVICE_REPORT)
                     ? names[g_state] : "?";
  const char *to   = (next   >= DEVICE_IDLE && next   <= DEVICE_REPORT)
                     ? names[next]   : "?";

  g_state = next;
  /* 强制 UI 立即刷新: 否则 1Hz 节流会让模式选择/报告界面滞后一拍。
   * (start_session 也会置 0, 此处统一覆盖所有转移, 长按/短按/停止即时上屏) */
  g_last_ui_ms = 0;
  printf("[state_machine] %s -> %s\n", from, to);
}

/* ==================== 会话生命周期 ==================== */

static void start_session(void)
{
  uint64_t now = monotonic_ms();
  g_last_tick_ms   = now;
  g_last_ui_ms     = 0;  /* 立即刷新一次 */
  g_accumulated_ms = 0;

  memset(&g_stats, 0, sizeof(g_stats));
  memset(g_distraction_by_type, 0, sizeof(g_distraction_by_type));

  g_stats.session_start = (uint32_t)(now / 1000U);
  g_stats.current_mode  = (uint8_t)g_mode;
  g_stats.focus_score   = SCORE_INITIAL;
  g_score               = SCORE_INITIAL;
  g_paused              = false;

  printf("[state_machine] 会话开始 mode=%s\n",
         g_mode == MODE_STRICT ? "STRICT" : "GENTLE");
}

static void end_session(void)
{
  g_stats.focus_score = (uint8_t)g_score;

  memset(&g_report, 0, sizeof(g_report));
  g_report.stats = g_stats;
  memcpy(g_report.distraction_by_type, g_distraction_by_type,
         sizeof(g_distraction_by_type));
  /* advice 由 UI 层调 mimo_get_advice() 填充 */

  printf("[state_machine] 会话结束: 总%us 有效%us 分心%u次 评分%u\n",
         g_stats.total_duration_sec,
         g_stats.effective_duration_sec,
         g_stats.distraction_count,
         g_stats.focus_score);
}

static void reset_session(void)
{
  g_paused = false;
  g_score  = SCORE_INITIAL;
  memset(&g_stats, 0, sizeof(g_stats));
  memset(g_distraction_by_type, 0, sizeof(g_distraction_by_type));
  memset(&g_report, 0, sizeof(g_report));
  printf("[state_machine] 会话已复位\n");
}

static void toggle_mode(void)
{
  g_mode = (g_mode == MODE_STRICT) ? MODE_GENTLE : MODE_STRICT;
  behavior_set_mode(g_mode);
  g_stats.current_mode = (uint8_t)g_mode;
  printf("[state_machine] 切换模式 -> %s\n",
         g_mode == MODE_STRICT ? "STRICT" : "GENTLE");
}

/* ==================== 评分 ==================== */

static void add_score_delta(int delta)
{
  g_score += delta;
  if (g_score < SCORE_MIN) g_score = SCORE_MIN;
  if (g_score > SCORE_MAX) g_score = SCORE_MAX;
  g_stats.focus_score = (uint8_t)g_score;
}

/* ==================== 行为结果消费 ==================== */

static void process_study_state(const study_state_t *ss)
{
  /* 评分增量 */
  if (ss->focus_score_delta != 0)
    {
      add_score_delta(ss->focus_score_delta);
    }

  /* 分心计数与类型 + 提醒 */
  if (ss->action == REMIND)
    {
      g_stats.distraction_count++;
      switch (ss->status)
        {
          case PLAYING_PHONE:
            g_distraction_by_type[DIST_PLAYING_PHONE]++;
            break;
          case GLANCING_PHONE:
            g_distraction_by_type[DIST_GLANCING_PHONE]++;
            break;
          case AWAY:
            g_distraction_by_type[DIST_AWAY]++;
            break;
          case DROWSY:
            g_distraction_by_type[DIST_DROWSY]++;
            break;
          default:
            break;
        }
      trigger_remind(ss);
    }
  else if (ss->action == ENCOURAGE)
    {
      trigger_encourage(ss);
    }
}

/* ==================== 提醒/鼓励触发 ==================== */

static void trigger_remind(const study_state_t *ss)
{
  printf("[state_machine] REMIND -> \"%s\" (score %u)\n",
         ss->message, g_stats.focus_score);

  if (ss->message[0] != '\0')
    {
      (void)lcd_show_message(ss->message, REMIND);
      (void)audio_play_tts(ss->message);
    }
}

static void trigger_encourage(const study_state_t *ss)
{
  printf("[state_machine] ENCOURAGE -> \"%s\" (milestone %dmin)\n",
         ss->message, ss->milestone_minutes);

  if (ss->message[0] != '\0')
    {
      (void)lcd_show_message(ss->message, ENCOURAGE);
      (void)audio_play_buzzer(AUDIO_BUZZER_ENCOURAGE);
    }
}

/* ==================== UI 刷新 ==================== */

static void refresh_ui(void)
{
  switch (g_state)
    {
      case DEVICE_MONITORING:
        (void)lcd_show_status(g_state, &g_stats, &g_latest_study);
        break;

      case DEVICE_REPORT:
        /* advice 由 UI 层通过 mimo_get_advice 补充 */
        (void)lcd_show_report((session_report_t *)&g_report, NULL);
        break;

      case DEVICE_MODE_SELECT:
      case DEVICE_IDLE:
      default:
        (void)lcd_show_status(g_state, &g_stats, &g_latest_study);
        break;
    }
}

/* ==================== 公开接口 ==================== */

void state_machine_init(void)
{
  g_state  = DEVICE_IDLE;
  g_mode   = MODE_STRICT;
  g_paused = false;
  g_score  = SCORE_INITIAL;

  memset(&g_stats, 0, sizeof(g_stats));
  memset(g_distraction_by_type, 0, sizeof(g_distraction_by_type));
  memset(&g_report, 0, sizeof(g_report));
  memset(&g_latest_study, 0, sizeof(g_latest_study));
  g_latest_study.status = FOCUSED;

  g_last_tick_ms = 0;
  g_last_ui_ms   = 0;

  printf("[state_machine] init -> IDLE\n");
}

void state_machine_tick(void)
{
  uint64_t now = monotonic_ms();
  button_event_t btn = button_get_event();

  /* ---- 1. 累计本 tick 经过的毫秒 (避免整数除法丢失 <1s 间隔) ---- */
  if (g_last_tick_ms != 0)
    {
      g_accumulated_ms += (now - g_last_tick_ms);
    }
  g_last_tick_ms = now;

  /* ---- 2. 按键驱动的状态转移 ---- */
  switch (g_state)
    {
      case DEVICE_IDLE:
        if (btn == BTN_START_LONGPRESS)
          {
            transition_to(DEVICE_MODE_SELECT);
          }
        break;

      case DEVICE_MODE_SELECT:
        if (btn == BTN_MODE)
          {
            toggle_mode();
          }
        else if (btn == BTN_START_SHORT)
          {
            start_session();
            transition_to(DEVICE_MONITORING);
          }
        break;

      case DEVICE_MONITORING:
        if (btn == BTN_PAUSE_SHORT)
          {
            g_paused = !g_paused;
            printf("[state_machine] %s\n", g_paused ? "暂停" : "恢复");
          }
        else if (btn == BTN_PAUSE_LONGPRESS || btn == BTN_STOP)
          {
            end_session();
            transition_to(DEVICE_REPORT);
          }
        break;

      case DEVICE_REPORT:
        if (btn == BTN_START_SHORT)
          {
            reset_session();
            transition_to(DEVICE_IDLE);
          }
        break;

      default:
        break;
    }

  /* ---- 3. 计时 + 消费行为结果 (MONITORING 且未暂停) ---- */
  if (g_state == DEVICE_MONITORING && !g_paused)
    {
      /* 每 tick 只调用一次 behavior_analyze (有状态) */
      g_latest_study = behavior_analyze();

      /* 累计毫秒满 1 秒才折算成秒, 避免 <1s 的 tick 间隔被丢弃 */
      if (g_accumulated_ms >= 1000U)
        {
          uint32_t elapsed_sec = (uint32_t)(g_accumulated_ms / 1000U);
          g_accumulated_ms %= 1000U;

          g_stats.total_duration_sec += elapsed_sec;

          /* 有效学习时长: 仅 FOCUSED 状态累计 */
          if (g_latest_study.status == FOCUSED)
            {
              g_stats.effective_duration_sec += elapsed_sec;
            }
        }

      process_study_state(&g_latest_study);
    }

  /* ---- 4. UI 刷新 (节流到 1Hz) ---- */
  if (g_last_ui_ms == 0 || (now - g_last_ui_ms) >= UI_REFRESH_INTERVAL_MS)
    {
      g_last_ui_ms = now;
      refresh_ui();
    }
}

device_status_t state_machine_get_status(void)
{
  return g_state;
}

const session_stats_t *state_machine_get_stats(void)
{
  return &g_stats;
}

int state_machine_get_mode(void)
{
  return g_mode;
}

bool state_machine_is_paused(void)
{
  return g_paused;
}
