#define _POSIX_C_SOURCE 200809L

#include "behavior.h"
#include "perception.h"
#include "behavior_config.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define HISTORY_CAPACITY 60

static observation_t g_history[HISTORY_CAPACITY];
static int g_history_count;

int perception_get_history(observation_t *buf, int n)
{
    int count;

    if (buf == NULL || n <= 0) return 0;
    count = n < g_history_count ? n : g_history_count;
    memcpy(buf, g_history, (size_t)count * sizeof(*buf));
    return count;
}

static observation_t focused_obs(uint32_t timestamp_ms)
{
    observation_t obs;

    memset(&obs, 0, sizeof(obs));
    obs.person_present = true;
    obs.confidence = 1.0f;
    obs.timestamp_ms = timestamp_ms;
    return obs;
}

static observation_t phone_obs(uint32_t timestamp_ms, float motion)
{
    observation_t obs = focused_obs(timestamp_ms);

    obs.phone_detected = true;
    obs.phone_near_hand = true;
    obs.head_pitch = -20.0f;
    obs.hand_motion_score = motion;
    return obs;
}

static observation_t away_obs(uint32_t timestamp_ms)
{
    observation_t obs;

    memset(&obs, 0, sizeof(obs));
    obs.confidence = 1.0f;
    obs.timestamp_ms = timestamp_ms;
    return obs;
}

static observation_t drowsy_obs(uint32_t timestamp_ms)
{
    observation_t obs = focused_obs(timestamp_ms);

    obs.head_pitch = 60.0f;
    obs.hand_motion_score = 0.05f;
    return obs;
}

static void reset_history(void)
{
    memset(g_history, 0, sizeof(g_history));
    g_history_count = 0;
}

static void append_observation(observation_t obs)
{
    assert(g_history_count < HISTORY_CAPACITY);
    g_history[g_history_count++] = obs;
}

static study_state_t analyze_history(void)
{
    return behavior_analyze();
}

static void test_empty_history(void)
{
    study_state_t state;

    reset_history();
    behavior_init(MODE_STRICT);
    state = analyze_history();
    assert(state.status == FOCUSED);
    assert(state.action == NONE);
}

static void test_strict_phone_and_cooldown(void)
{
    study_state_t state;

    reset_history();
    behavior_init(MODE_STRICT);
    append_observation(focused_obs(0));
    append_observation(phone_obs(4000, 0.8f));
    append_observation(phone_obs(8000, 0.8f));
    append_observation(phone_obs(12000, 0.8f));

    state = analyze_history();
    assert(state.status == PLAYING_PHONE);
    assert(state.action == REMIND);
    assert(strcmp(state.message, "请放下手机!") == 0);
    assert(state.focus_score_delta == -15);

    /* The same latest frame is observed again on the faster state-machine
     * tick; the transient reminder must not be emitted twice. */
    state = analyze_history();
    assert(state.status == PLAYING_PHONE);
    assert(state.action == NONE);
    assert(state.message[0] == '\0');

    append_observation(phone_obs(20000, 0.8f));
    state = analyze_history();
    assert(state.status == PLAYING_PHONE);
    assert(state.action == NONE); /* 30-second cooldown is active. */

    append_observation(phone_obs(43000, 0.8f));
    state = analyze_history();
    assert(state.status == PLAYING_PHONE);
    assert(state.action == REMIND);
    assert(state.focus_score_delta == -15);
}

static void test_glancing_phone(void)
{
    study_state_t state;

    reset_history();
    behavior_init(MODE_STRICT);
    append_observation(focused_obs(0));
    append_observation(phone_obs(4000, 0.0f));
    append_observation(phone_obs(12000, 0.0f));

    state = analyze_history();
    assert(state.status == GLANCING_PHONE);
    assert(state.action == NONE);
    assert(state.focus_score_delta == 0);
}

static void test_away_and_drowsy(void)
{
    study_state_t state;

    reset_history();
    behavior_init(MODE_STRICT);
    append_observation(focused_obs(0));
    append_observation(away_obs(5000));
    append_observation(away_obs(10000));
    append_observation(away_obs(15000));

    state = analyze_history();
    assert(state.status == AWAY);
    assert(state.action == REMIND);
    assert(strcmp(state.message, "请回到座位!") == 0);
    assert(state.focus_score_delta == -10);

    reset_history();
    behavior_init(MODE_STRICT);
    append_observation(focused_obs(0));
    append_observation(drowsy_obs(4000));
    append_observation(drowsy_obs(8000));
    append_observation(drowsy_obs(12000));

    state = analyze_history();
    assert(state.status == DROWSY);
    assert(state.action == REMIND);
    assert(strcmp(state.message, "请注意坐姿!") == 0);
    assert(state.focus_score_delta == -20);
}

static void test_gentle_threshold_and_accumulated_reminders(void)
{
    study_state_t state;

    reset_history();
    behavior_init(MODE_GENTLE);
    append_observation(focused_obs(0));
    append_observation(phone_obs(5000, 0.8f));
    append_observation(phone_obs(10000, 0.8f));
    append_observation(phone_obs(15000, 0.8f));
    state = analyze_history();
    assert(state.status == FOCUSED); /* gentle phone threshold is 15s. */

    append_observation(phone_obs(20000, 0.8f));
    state = analyze_history();
    assert(state.status == PLAYING_PHONE);
    assert(state.action == NONE); /* first of three gentle reminders */

    append_observation(phone_obs(25000, 0.8f));
    state = analyze_history();
    assert(state.status == PLAYING_PHONE);
    assert(state.action == NONE);

    append_observation(phone_obs(30000, 0.8f));
    state = analyze_history();
    assert(state.status == PLAYING_PHONE);
    assert(state.action == REMIND);
    assert(strcmp(state.message, "休息好了就继续吧~") == 0);
    assert(state.focus_score_delta == -5);
}

static void test_gentle_milestones_are_not_repeated(void)
{
    study_state_t state;

    reset_history();
    behavior_init(MODE_GENTLE);
    append_observation(focused_obs(0));
    append_observation(focused_obs(900000));
    append_observation(focused_obs(1800000));

    state = analyze_history();
    assert(state.status == FOCUSED);
    assert(state.action == ENCOURAGE);
    assert(state.milestone_reached);
    assert(state.milestone_minutes == 30);
    assert(state.focus_score_delta == 5);

    state = analyze_history();
    assert(state.action == NONE);
    assert(!state.milestone_reached);

    append_observation(focused_obs(1860000));
    state = analyze_history();
    assert(state.action == NONE); /* still within the 30-minute block */

    append_observation(focused_obs(3600000));
    state = analyze_history();
    assert(state.action == ENCOURAGE);
    assert(state.milestone_minutes == 60);
}

int main(void)
{
    test_empty_history();
    test_strict_phone_and_cooldown();
    test_glancing_phone();
    test_away_and_drowsy();
    test_gentle_threshold_and_accumulated_reminders();
    test_gentle_milestones_are_not_repeated();
    puts("focus_aiot behavior tests passed");
    return 0;
}
