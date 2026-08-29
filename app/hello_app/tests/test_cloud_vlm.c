/****************************************************************************
 * FOCUS AIoT - 真实云端 VLM 链路单测 (tests/test_cloud_vlm.c)
 *
 * 负责人: 周礼航
 * 用途:   用 wifi_http_post_stub.c 替换真实网络, 验证 perception 真实路径:
 *         每帧都调云端 VLM、请求构造、响应解析、JSON 围栏容错、3 帧去抖、
 *         重试、错误码。
 *
 * 依赖:   cJSON (perception.c 真实路径需要)。
 * 编译 (在 focus_perception 目录下):
 *   gcc -Wall -Wextra -Werror \
 *       perception/perception.c tests/wifi_http_post_stub.c tests/test_cloud_vlm.c \
 *       cJSON.c -I/path/to/cjson -o test_cloud_vlm
 *   ./test_cloud_vlm
 *
 *   (cJSON 是单文件库; openvela 已自带。host 上可 apt install libcjson-dev
 *    或直接拷一份 cJSON.c/cJSON.h 进仓库。)
 ****************************************************************************/

#include <stdio.h>
#include <string.h>

#include "../api/perception.h"

/* 来自 wifi_http_post_stub.c */
extern int         stub_cloud_call_count;
extern const char *stub_cloud_response;
extern int         stub_cloud_fail_first_n;

static int g_checks = 0;
static int g_fail   = 0;

#define CHECK(cond) do {                                   \
    g_checks++;                                            \
    if (!(cond)) {                                         \
        g_fail++;                                          \
        printf("  [FAIL] %s (line %d)\n", #cond, __LINE__);\
    }                                                      \
} while (0)

static int feq(float a, float b)
{
    float d = a - b;
    if (d < 0) d = -d;
    return d < 1e-4f;
}

/* 每帧都调云端 VLM + 响应解析 */
static void test_cloud_parse_and_every_frame(void)
{
    printf("== test_cloud_parse_and_every_frame ==\n");
    stub_cloud_response = NULL;      /* 用默认「玩手机」响应 */
    stub_cloud_fail_first_n = 0;

    CHECK(perception_init("http://localhost:8000/v1/chat/completions", "key")
          == FOCUS_OK);

    uint8_t jpeg[64] = {0};
    observation_t obs;
    int before = stub_cloud_call_count;

    /* 连续 5 帧 → 云端 VLM 应被调用 5 次 (每帧都调) */
    for (int i = 0; i < 5; i++) {
        CHECK(perception_process(jpeg, sizeof(jpeg), &obs) == FOCUS_OK);
    }
    CHECK(stub_cloud_call_count - before == 5);

    /* 单帧解析结果 (去抖窗口未满 3 帧时直接输出) */
    CHECK(obs.person_present  == true);
    CHECK(obs.phone_detected  == true);
    CHECK(obs.phone_near_hand == true);
    CHECK(feq(obs.person_bbox[0], 0.15f) && feq(obs.person_bbox[1], 0.10f));
    CHECK(feq(obs.person_bbox[2], 0.70f) && feq(obs.person_bbox[3], 0.85f));
    CHECK(feq(obs.head_pitch, -25.3f));
    CHECK(feq(obs.head_yaw, 2.1f));
    CHECK(feq(obs.hand_motion_score, 0.35f));
    CHECK(feq(obs.confidence, 0.92f));
}

/* content 被 ```json 围栏 + 前后说明文字包裹, 验证容错提取 */
static void test_cloud_fenced_content(void)
{
    printf("== test_cloud_fenced_content ==\n");
    perception_init("u", "k");
    stub_cloud_fail_first_n = 0;

    stub_cloud_response =
        "{\"choices\":[{\"message\":{\"content\":\""
        "Here: ```json {\\\"person\\\":{\\\"detected\\\":false,\\\"bbox\\\":[0,0,0,0]},"
        "\\\"phone\\\":{\\\"detected\\\":false,\\\"near_hand\\\":false},"
        "\\\"head_pose\\\":{\\\"pitch\\\":0,\\\"yaw\\\":0},"
        "\\\"hand_motion_score\\\":0.0,"
        "\\\"confidence\\\":0.5} ``` done\""
        "}}]}";

    uint8_t jpeg[64] = {0};
    observation_t obs;
    CHECK(perception_process(jpeg, sizeof(jpeg), &obs) == FOCUS_OK);
    CHECK(obs.person_present == false);
    CHECK(obs.phone_detected == false);
    CHECK(feq(obs.confidence, 0.5f));
}

/* 失败重试: 前 1 次失败 → 重试成功, 调用次数 +2 */
static void test_cloud_retry(void)
{
    printf("== test_cloud_retry ==\n");
    perception_init("u", "k");

    stub_cloud_fail_first_n = 1;
    int before = stub_cloud_call_count;

    uint8_t jpeg[64] = {0};
    observation_t obs;
    CHECK(perception_process(jpeg, sizeof(jpeg), &obs) == FOCUS_OK);
    CHECK(stub_cloud_call_count - before == 2);   /* 1 失败 + 1 重试成功 */
}

/* 错误码: 无 choices / content 无 JSON / envelope 非法 / 连续失败超时 */
static void test_cloud_error_codes(void)
{
    printf("== test_cloud_error_codes ==\n");
    uint8_t jpeg[64] = {0};
    observation_t obs;

    /* 无 choices → NODATA */
    perception_init("u", "k");
    stub_cloud_fail_first_n = 0;
    stub_cloud_response = "{\"choices\":[]}";
    CHECK(perception_process(jpeg, sizeof(jpeg), &obs) == FOCUS_ERR_PERCEP_NODATA);

    /* content 里没有 JSON 对象 → JSON */
    stub_cloud_response = "{\"choices\":[{\"message\":{\"content\":\"no json here\"}}]}";
    CHECK(perception_process(jpeg, sizeof(jpeg), &obs) == FOCUS_ERR_PERCEP_JSON);

    /* envelope 非法 JSON → JSON */
    stub_cloud_response = "not-a-json";
    CHECK(perception_process(jpeg, sizeof(jpeg), &obs) == FOCUS_ERR_PERCEP_JSON);

    /* 连续 2 次都失败 (原请求 + 重试) → TIMEOUT */
    stub_cloud_response = NULL;
    stub_cloud_fail_first_n = 2;
    CHECK(perception_process(jpeg, sizeof(jpeg), &obs) == FOCUS_ERR_PERCEP_TIMEOUT);
}

int main(void)
{
    test_cloud_parse_and_every_frame();
    test_cloud_fenced_content();
    test_cloud_retry();
    test_cloud_error_codes();

    printf("\n结果: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
