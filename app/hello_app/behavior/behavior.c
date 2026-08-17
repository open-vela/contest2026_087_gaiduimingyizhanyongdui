/****************************************************************************
 * FOCUS AIoT - 行为分析时序引擎 (behavior/behavior.c)
 *
 * 负责人: 赵思涵
 * 职责: 消费 perception_get_history() 返回的 Observation 序列，输出
 *       严格/鼓励双模式下的 study_state_t。
 *
 * 约束:
 *   - 只做纯计算，不访问网络、不操作硬件。
 *   - 以 Observation.timestamp_ms 计算持续时间。
 *   - 提醒冷却、累计提醒次数和里程碑去重全部在本模块处理。
 ****************************************************************************/

#include "../api/behavior.h"
#include "../api/perception.h"
#include "../config/behavior_config.h"

#include <stdio.h>
#include <string.h>

#define BEHAVIOR_HISTORY_CAPACITY 60
#define STATUS_COUNT              5

static int g_current_mode = MODE_STRICT;
static study_state_t g_last_state;
static uint32_t g_last_observation_ms;
static bool g_have_observation;
static uint32_t g_last_remind_ms;
static bool g_have_remind_time;
static int g_distraction_since_remind;
static int g_last_celebrated_streak;

static int normalize_mode(int mode)
{
    return mode == MODE_GENTLE ? MODE_GENTLE : MODE_STRICT;
}

const mode_config_t *behavior_get_config(int mode)
{
    return &g_mode_configs[normalize_mode(mode)];
}

const mode_config_t *behavior_get_current_config(void)
{
    return behavior_get_config(g_current_mode);
}

static void clear_transient_fields(study_state_t *state)
{
    state->action = NONE;
    state->message[0] = '\0';
    state->milestone_reached = false;
    state->milestone_minutes = 0;
    state->focus_score_delta = 0;
}

static study_state_t state_with_status(status_t status)
{
    study_state_t state;

    memset(&state, 0, sizeof(state));
    state.status = status;
    state.action = NONE;
    return state;
}

static uint32_t elapsed_ms(uint32_t newer, uint32_t older)
{
    /* uint32_t subtraction also behaves correctly across the normal timer
     * wrap-around as long as a history window is shorter than 2^31 ms. */
    return (uint32_t)(newer - older);
}

static bool is_phone_observation(const observation_t *obs,
                                 const mode_config_t *config)
{
    (void)config;
    return obs->person_present && obs->phone_detected && obs->phone_near_hand;
}

static bool is_away_observation(const observation_t *obs,
                                const mode_config_t *config)
{
    (void)config;
    return !obs->person_present;
}

static bool is_drowsy_observation(const observation_t *obs,
                                  const mode_config_t *config)
{
    return obs->person_present && obs->head_pitch > config->head_pitch_max;
}

static bool is_focus_observation(const observation_t *obs,
                                 const mode_config_t *config)
{
    return obs->person_present && !obs->phone_detected
           && obs->head_pitch <= config->head_pitch_max;
}

typedef bool (*observation_predicate_t)(const observation_t *obs,
                                        const mode_config_t *config);

static uint32_t continuous_duration(const observation_t *history, int count,
                                    const mode_config_t *config,
                                    observation_predicate_t predicate,
                                    int *first_index)
{
    int first;

    if (count <= 0 || !predicate(&history[count - 1], config))
    {
        if (first_index != NULL) *first_index = count;
        return 0;
    }

    first = count - 1;
    while (first > 0 && predicate(&history[first - 1], config))
    {
        first--;
    }

    if (first_index != NULL) *first_index = first;
    return elapsed_ms(history[count - 1].timestamp_ms,
                      history[first].timestamp_ms);
}

static int count_away_in_recent_window(const observation_t *history, int count,
                                       const mode_config_t *config,
                                       uint32_t window_ms)
{
    int index;
    int frames = 0;
    uint32_t newest;

    if (count <= 0) return 0;
    newest = history[count - 1].timestamp_ms;

    for (index = count - 1; index >= 0; index--)
    {
        if (elapsed_ms(newest, history[index].timestamp_ms) > window_ms)
        {
            break;
        }
        if (is_away_observation(&history[index], config)) frames++;
    }
    return frames;
}

static float average_phone_motion(const observation_t *history, int count,
                                  int first_phone_index)
{
    float total = 0.0f;
    int frames = 0;
    int index;

    for (index = first_phone_index; index < count; index++)
    {
        if (!is_phone_observation(&history[index], NULL)) break;
        total += history[index].hand_motion_score;
        frames++;
    }

    return frames > 0 ? total / (float)frames : 0.0f;
}

static int focus_streak_minutes(const observation_t *history, int count,
                                const mode_config_t *config)
{
    uint32_t duration;
    int first_index;

    duration = continuous_duration(history, count, config,
                                   is_focus_observation, &first_index);
    (void)first_index;
    return (int)(duration / 60000U);
}

static int score_penalty(status_t status, const mode_config_t *config)
{
    switch (status)
    {
        case PLAYING_PHONE:
            return config->score_penalty_phone;
        case AWAY:
            return config->score_penalty_away;
        case DROWSY:
            return config->score_penalty_drowsy;
        default:
            return 0;
    }
}

static const char *message_for(status_t status, const mode_config_t *config)
{
    if (status < 0 || status >= STATUS_COUNT || config->messages == NULL)
    {
        return "";
    }
    return config->messages[status] != NULL ? config->messages[status] : "";
}

static study_state_t build_remind_state(status_t status,
                                        const mode_config_t *config,
                                        uint32_t now_ms)
{
    study_state_t state = state_with_status(status);
    bool cooldown_active = false;
    int required_count = config->remind_after_n_times;

    if (required_count < 1) required_count = 1;

    if (g_have_remind_time
        && elapsed_ms(now_ms, g_last_remind_ms)
               < (uint32_t)config->remind_cooldown_sec * 1000U)
    {
        cooldown_active = true;
    }

    if (g_distraction_since_remind < required_count)
    {
        g_distraction_since_remind++;
    }

    if (!cooldown_active && g_distraction_since_remind >= required_count)
    {
        const char *message = message_for(status, config);

        state.action = REMIND;
        snprintf(state.message, sizeof(state.message), "%s", message);
        state.focus_score_delta = score_penalty(status, config);
        g_last_remind_ms = now_ms;
        g_have_remind_time = true;
        g_distraction_since_remind = 0;
    }

    return state;
}

static study_state_t build_milestone_state(const mode_config_t *config,
                                           int milestone_minutes)
{
    study_state_t state = state_with_status(FOCUSED);

    state.action = ENCOURAGE;
    state.milestone_reached = true;
    state.milestone_minutes = milestone_minutes;
    state.focus_score_delta = config->score_bonus_milestone;
    snprintf(state.message, sizeof(state.message),
             "已专注学习 %d 分钟, 继续保持!", milestone_minutes);
    return state;
}

void behavior_init(int mode)
{
    g_current_mode = normalize_mode(mode);
    memset(&g_last_state, 0, sizeof(g_last_state));
    g_last_state.status = FOCUSED;
    g_last_state.action = NONE;
    g_last_observation_ms = 0;
    g_have_observation = false;
    g_last_remind_ms = 0;
    g_have_remind_time = false;
    g_distraction_since_remind = 0;
    g_last_celebrated_streak = 0;
}

void behavior_set_mode(int mode)
{
    g_current_mode = normalize_mode(mode);

    /* A mode switch starts a fresh reminder/milestone policy while retaining
     * the latest observation timestamp and status for the running session. */
    g_have_remind_time = false;
    g_last_remind_ms = 0;
    g_distraction_since_remind = 0;
    g_last_celebrated_streak = 0;
    clear_transient_fields(&g_last_state);
}

study_state_t behavior_analyze(void)
{
    observation_t history[BEHAVIOR_HISTORY_CAPACITY];
    const mode_config_t *config = behavior_get_current_config();
    study_state_t state;
    observation_t *latest;
    uint32_t now_ms;
    uint32_t phone_duration;
    uint32_t away_duration;
    uint32_t drowsy_duration;
    int first_phone_index;
    int away_frames;
    float phone_motion;
    int streak_minutes;

    memset(history, 0, sizeof(history));
    {
        int count = perception_get_history(history, BEHAVIOR_HISTORY_CAPACITY);
        if (count <= 0)
        {
            return g_last_state;
        }
        if (count > BEHAVIOR_HISTORY_CAPACITY)
        {
            count = BEHAVIOR_HISTORY_CAPACITY;
        }

        latest = &history[count - 1];
        now_ms = latest->timestamp_ms;

        /* The state machine can tick faster than perception. Do not emit the
         * same reminder or milestone again for an unchanged latest frame. */
        if (g_have_observation && now_ms == g_last_observation_ms)
        {
            return g_last_state;
        }

        if (g_have_observation
            && elapsed_ms(now_ms, g_last_observation_ms) == 0)
        {
            return g_last_state;
        }

        g_have_observation = true;
        g_last_observation_ms = now_ms;

        /* Away has priority over all other interpretations. */
        away_frames = count_away_in_recent_window(history, count, config,
                                                   15000U);
        away_duration = continuous_duration(history, count, config,
                                             is_away_observation, NULL);
        if (away_frames >= 2
            && away_duration >= (uint32_t)config->away_threshold_sec * 1000U)
        {
            state = build_remind_state(AWAY, config, now_ms);
            g_last_state = state;
            clear_transient_fields(&g_last_state);
            return state;
        }

        /* Phone duration and motion must both support the stronger
         * PLAYING_PHONE interpretation. */
        phone_duration = continuous_duration(history, count, config,
                                              is_phone_observation,
                                              &first_phone_index);
        phone_motion = average_phone_motion(history, count, first_phone_index);
        if (is_phone_observation(latest, config))
        {
            if (phone_duration >= (uint32_t)config->phone_playing_sec * 1000U
                && phone_motion > 0.3f)
            {
                state = build_remind_state(PLAYING_PHONE, config, now_ms);
                g_last_state = state;
                clear_transient_fields(&g_last_state);
                return state;
            }

            if (phone_duration >= (uint32_t)config->phone_glance_sec * 1000U)
            {
                state = state_with_status(GLANCING_PHONE);
                g_last_state = state;
                return state;
            }
        }

        drowsy_duration = continuous_duration(history, count, config,
                                               is_drowsy_observation, NULL);
        if (is_drowsy_observation(latest, config)
            && drowsy_duration
                   >= (uint32_t)config->drowsy_threshold_sec * 1000U)
        {
            state = build_remind_state(DROWSY, config, now_ms);
            g_last_state = state;
            clear_transient_fields(&g_last_state);
            return state;
        }

        if (config->enable_milestone && config->focus_milestone_min > 0
            && is_focus_observation(latest, config))
        {
            streak_minutes = focus_streak_minutes(history, count, config);
            if (streak_minutes >= config->focus_milestone_min)
            {
                int milestone = (streak_minutes / config->focus_milestone_min)
                                * config->focus_milestone_min;
                if (milestone > g_last_celebrated_streak)
                {
                    g_last_celebrated_streak = milestone;
                    state = build_milestone_state(config, milestone);
                    g_last_state = state;
                    clear_transient_fields(&g_last_state);
                    return state;
                }
            }
        }

        state = state_with_status(FOCUSED);
        g_last_state = state;
        return state;
    }
}
