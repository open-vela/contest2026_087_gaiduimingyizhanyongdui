/****************************************************************************
 * FOCUS AIoT - 视觉感知模块纯 C 单测 (tests/test_perception.c)
 *
 * 负责人: 周礼航
 * 说明:   宿主机运行, 不依赖硬件/网络/cJSON (用 mock 路径)。
 *         参考赵思涵 tests/behavior_test.c 的模式。
 *
 * 编译 (在 focus_perception 目录下):
 *   gcc -Wall -Wextra -Werror -DPERCEPTION_MOCK \
 *       perception/perception.c tests/test_perception.c -o test_perception
 *   ./test_perception
 ****************************************************************************/

#include <stdio.h>
#include <string.h>

#include "../api/perception.h"
#include "../perception/perception_internal.h"

/* ======================================================================
 * 返回码定义（临时，后续应移到公共头文件）
 * ====================================================================== */
#ifndef FOCUS_OK
#define FOCUS_OK                            0
#endif

#ifndef FOCUS_ERR_PERCEP_PARAM
#define FOCUS_ERR_PERCEP_PARAM             -1
#endif

#ifndef FOCUS_ERR_PERCEP_TIMEOUT
#define FOCUS_ERR_PERCEP_TIMEOUT           -2
#endif

#ifndef FOCUS_ERR_PERCEP_JSON
#define FOCUS_ERR_PERCEP_JSON              -3
#endif

#ifndef FOCUS_ERR_PERCEP_NODATA
#define FOCUS_ERR_PERCEP_NODATA            -4
#endif
/* ====================================================================== */

static int g_checks = 0;
static int g_fail   = 0;

#define CHECK(cond) do {                                   \
    g_checks++;                                            \
    if (!(cond)) {                                         \
        g_fail++;                                          \
        printf("  [FAIL] %s (line %d)\n", #cond, __LINE__);\
    }                                                      \
} while (0)

/* RFC 4648 标准测试向量 */
static void test_base64(void)
{
    printf("== test_base64 ==\n");
    const char *cases[][2] = {
        {"",       ""},
        {"M",      "TQ=="},
        {"Ma",     "TWE="},
        {"Man",    "TWFu"},
        {"plea",   "cGxlYQ=="},
        {"pleas",  "cGxlYXM="},
        {"please", "cGxlYXNl"},
    };
    char out[64];

    for (int i = 0; i < 7; i++) {
        size_t n = perception_base64_encode((const uint8_t *)cases[i][0],
                                            strlen(cases[i][0]),
                                            out, sizeof(out));
        CHECK(n == strlen(cases[i][1]));
        CHECK(strcmp(out, cases[i][1]) == 0);
    }
}

static void test_math_helpers(void)
{
    printf("== test_math_helpers ==\n");

    CHECK(perception_majority3(true, true, false)  == true);
    CHECK(perception_majority3(true, false, false) == false);
    CHECK(perception_majority3(false, false, false) == false);
    CHECK(perception_majority3(true, true, true)   == true);

    CHECK(perception_median3(1.0f, 2.0f, 3.0f) == 2.0f);
    CHECK(perception_median3(3.0f, 1.0f, 2.0f) == 2.0f);
    CHECK(perception_median3(2.0f, 3.0f, 1.0f) == 2.0f);
    CHECK(perception_median3(5.0f, 5.0f, 5.0f) == 5.0f);

    CHECK(perception_max3(1.0f, 2.0f, 3.0f) == 3.0f);
    CHECK(perception_max3(3.0f, 2.0f, 1.0f) == 3.0f);
}

static void test_debounce(void)
{
    printf("== test_debounce ==\n");
    observation_t out;

    /* 布尔多数投票: 2 帧有人 + 1 帧无人 → 有人 */
    cloud_result_t r1 = {0}, r2 = {0}, r3 = {0};
    r1.person_detected = true;  r1.confidence = 0.9f;
    r2.person_detected = true;  r2.confidence = 0.9f;
    r3.person_detected = false; r3.confidence = 0.9f;

    perception_init("url", "key");       /* 重置去抖状态 */
    perception_debounce_push(&r1);
    perception_debounce_compute(&out, 1);       /* 不足 3 帧 → 输出最新帧 */
    CHECK(out.person_present == true);

    perception_debounce_push(&r2);
    perception_debounce_compute(&out, 2);
    CHECK(out.person_present == true);

    perception_debounce_push(&r3);
    perception_debounce_compute(&out, 3);       /* 满 3 帧 → 多数投票 */
    CHECK(out.person_present == true);

    /* 浮点中位数: 10/20/90 → 20 */
    cloud_result_t p1 = {0}, p2 = {0}, p3 = {0};
    p1.head_pitch = 10.0f; p1.confidence = 0.9f;
    p2.head_pitch = 20.0f; p2.confidence = 0.9f;
    p3.head_pitch = 90.0f; p3.confidence = 0.9f;

    perception_init("url", "key");
    perception_debounce_push(&p1);
    perception_debounce_push(&p2);
    perception_debounce_push(&p3);
    perception_debounce_compute(&out, 4);
    CHECK(out.head_pitch == 20.0f);
}

static void test_process_and_history(void)
{
    printf("== test_process_and_history ==\n");
    uint8_t dummy[16] = {0};
    observation_t obs;
    observation_t hist[10];

    CHECK(perception_init("http://api", "key123") == FOCUS_OK);

    /* 空历史 */
    CHECK(perception_get_history(hist, 10) == 0);
    CHECK(perception_get_history(NULL, 10) == 0);

    /* 连续 8 帧, 时间戳非递减 */
    uint32_t last_ts = 0;
    for (int i = 0; i < 8; i++) {
        CHECK(perception_process(dummy, sizeof(dummy), &obs) == FOCUS_OK);
        CHECK(obs.timestamp_ms >= last_ts);
        last_ts = obs.timestamp_ms;
    }

    /* 历史条数: 取全部 / 取最近 3 条 */
    CHECK(perception_get_history(hist, 10) == 8);
    CHECK(perception_get_history(hist, 3)  == 3);

    /* mock 轮转应覆盖 person=false 与 phone=true 两种情况 */
    bool saw_away = false, saw_phone = false;
    for (int i = 0; i < 4; i++) {
        CHECK(perception_process(dummy, sizeof(dummy), &obs) == FOCUS_OK);
        if (!obs.person_present) saw_away = true;
        if (obs.phone_detected)  saw_phone = true;
    }
    CHECK(saw_away && saw_phone);

    /* 参数非法 */
    CHECK(perception_process(NULL, 10, &obs) != FOCUS_OK);
    CHECK(perception_process(dummy, 0, &obs)  != FOCUS_OK);
    CHECK(perception_process(dummy, 10, NULL) != FOCUS_OK);
}

int main(void)
{
    test_base64();
    test_math_helpers();
    test_debounce();
    test_process_and_history();

    printf("\n结果: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
