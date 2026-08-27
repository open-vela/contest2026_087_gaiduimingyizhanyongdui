/****************************************************************************
 * FOCUS AIoT - 视觉感知模块 (perception/perception.c)
 *
 * 负责人: 周礼航
 * 职责:   从一张 JPEG 里看出「有什么」——只做空间维度的单帧静态分析，
 *         不做时间维度的行为推理 (那是赵思涵 behavior 模块的事)。
 *
 * 输入:   JPEG 图像 (来自张沐泽 camera_frame_buffer / camera_capture_frame)
 * 输出:   observation_t (每帧一个, 经 MiMo 识图接口 + 3 帧去抖)
 *
 * MiMo 调用时机: 每帧都调 (perception_process 每次收到一帧 JPEG 都调一次
 *                mimo_detect(), 失败重试 1 次, 总阻塞 ≤6s)
 *
 * MiMo 契约 (已冻结):
 *   Request:  {"image":"<base64>","width":320,"height":240}
 *   Response: {"code":0,"data":{person/phone/head_pose/hand_motion_score/confidence}}
 *
 * 三个对外接口 (冻结于 api/perception.h):
 *   perception_init()         初始化 MiMo endpoint/key + 清空历史
 *   perception_process()      一帧 JPEG → (调 MiMo) → observation_t
 *   perception_get_history()  取最近 N 条 observation (供 behavior 时序分析)
 *
 * 编译开关:
 *   -D PERCEPTION_MOCK  走 mock 路径 (不调 MiMo), 默认 mock 便于联调/回退
 *                       不定义则走真实 MiMo 链路 (需要 cJSON + wifi_http_post)
 ****************************************************************************/

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "../api/perception.h"
#include "perception_internal.h"

/* ======================================================================
 * 错误码统一使用 api/error.h (FOCUS_ERR_PERCEP_* = -30/-31/-32)
 * ====================================================================== */
#include "../api/error.h"

/* 参数错误: error.h 统一用 FOCUS_ERR_PARAM (-2) */
#define FOCUS_ERR_PERCEP_PARAM   FOCUS_ERR_PARAM

#ifndef PERCEPTION_MOCK
#include <netutils/cJSON.h>
/* 张沐泽提供, 见 api/wifi.h (前向声明, 避免独立编译依赖) */
int wifi_http_post(const char *url, const char *body, char *resp, size_t maxlen);
void wifi_set_http_auth(const char *api_key);
#endif

/* ------------------------------------------------------------------ */
/* 内部配置                                                            */
/* ------------------------------------------------------------------ */
#define HISTORY_MAX     60              /* 保留最近 60 条 observation */
#define FRAME_WIDTH     320
#define FRAME_HEIGHT    240
#define MAX_JPEG_SIZE   (50 * 1024)     /* 320x240 JPEG 上限 */
#define RESP_BUF_SIZE   4096            /* MiMo 响应缓冲 */

/* MiMo 识图 endpoint 与鉴权 key。
 * 优先级: perception_init() 传入 > 下面的默认值。
 * TODO: 把 endpoint 填成 MiMo 真实地址; key 按鉴权方式填 (契约未体现
 *       auth 字段, 若需 Authorization header 请与张沐泽确认 wifi_http_post
 *       是否支持自定义 header, g_api_key 暂作预留)。 */
#define MIMO_ENDPOINT_DEFAULT   "https://token-plan-cn.xiaomimimo.com/v1/chat/completions"
#define MIMO_API_KEY_DEFAULT    "tp-cqhvcy96byytd23azumypy2925bpmilukor7fp3k2tu8h5ew"
#define MIMO_MODEL              "mimo-v2.5"

/* 提示 MiMo 输出结构化 JSON (学习专注场景分析) */
#define MIMO_PROMPT \
  "你是学习专注监测AI。分析图片中学习者的状态，只输出一个JSON对象，不要输出其他任何文本。JSON格式: " \
  "{\"person_present\":bool,\"person_bbox\":[x,y,w,h],\"phone_detected\":bool," \
  "\"phone_near_hand\":bool,\"head_pitch\":float,\"head_yaw\":float," \
  "\"hand_motion_score\":float,\"confidence\":float}. " \
  "含义: person_present=是否有人; phone_detected=是否检测到手机; " \
  "phone_near_hand=手机是否在手部附近; head_pitch=头部俯仰角(度,负=低头); " \
  "hand_motion_score=手部运动量(0-1); confidence=置信度(0-1)。"

/* ------------------------------------------------------------------ */
/* 模块状态 (单例; 该模块由 wifi_task 串行调用, 无重入)                */
/* ------------------------------------------------------------------ */
static char g_api_url[160];
static char g_api_key[80];

static observation_t g_history[HISTORY_MAX];
static int           g_history_count = 0;   /* 累计写入条数 (用于求 start) */

static cloud_result_t g_debounce[3];
static int            g_debounce_count = 0; /* 已累积帧数 0..3 */
static int            g_debounce_idx   = 0;

static int            g_mock_seq = 0;       /* mock 轮转序号 */

#ifndef PERCEPTION_MOCK
static char           g_resp[RESP_BUF_SIZE]; /* MiMo 响应缓冲 (仅真实链路) */
#endif

/* ------------------------------------------------------------------ */
/* 基础工具                                                            */
/* ------------------------------------------------------------------ */

uint32_t perception_monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u +
                      (uint64_t)ts.tv_nsec / 1000000u);
}

bool perception_majority3(bool a, bool b, bool c)
{
    return ((int)a + (int)b + (int)c) >= 2;
}

float perception_median3(float a, float b, float c)
{
    if (a > b) { float t = a; a = b; b = t; }
    if (b > c) { float t = b; b = c; c = t; }
    if (a > b) { float t = a; a = b; b = t; }
    return b;
}

float perception_max3(float a, float b, float c)
{
    float m = a;
    if (b > m) m = b;
    if (c > m) m = c;
    return m;
}

/* ------------------------------------------------------------------ */
/* Base64 编码 (RFC 4648)                                              */
/* ------------------------------------------------------------------ */

size_t perception_base64_encoded_len(size_t n)
{
    return 4 * ((n + 2) / 3);
}

size_t perception_base64_encode(const uint8_t *src, size_t len,
                                char *dst, size_t dst_cap)
{
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    size_t out_len = perception_base64_encoded_len(len);
    if (!src || !dst || dst_cap < out_len + 1) {
        return 0;
    }

    size_t i, j = 0;
    for (i = 0; i + 3 <= len; i += 3) {
        uint32_t v = ((uint32_t)src[i]     << 16) |
                     ((uint32_t)src[i + 1] << 8)  |
                      (uint32_t)src[i + 2];
        dst[j++] = tbl[(v >> 18) & 63];
        dst[j++] = tbl[(v >> 12) & 63];
        dst[j++] = tbl[(v >> 6)  & 63];
        dst[j++] = tbl[ v        & 63];
    }

    size_t rem = len - i;
    if (rem == 1) {
        uint32_t v = (uint32_t)src[i] << 16;
        dst[j++] = tbl[(v >> 18) & 63];
        dst[j++] = tbl[(v >> 12) & 63];
        dst[j++] = '=';
        dst[j++] = '=';
    } else if (rem == 2) {
        uint32_t v = ((uint32_t)src[i] << 16) | ((uint32_t)src[i + 1] << 8);
        dst[j++] = tbl[(v >> 18) & 63];
        dst[j++] = tbl[(v >> 12) & 63];
        dst[j++] = tbl[(v >> 6)  & 63];
        dst[j++] = '=';
    }

    dst[j] = '\0';
    return j;
}

/* ------------------------------------------------------------------ */
/* 去抖稳定 (3 帧滑动窗口)                                              */
/*   bool 字段: 多数投票                                                */
/*   float 角度: 中位数                                                 */
/*   hand_motion: 取最大 (宁可多报不遗漏)                                */
/*   confidence: 取均值                                                 */
/* ------------------------------------------------------------------ */

void perception_fill_observation(const cloud_result_t *raw,
                                 observation_t *out, uint32_t ts)
{
    out->person_present    = raw->person_detected;
    out->person_bbox[0]    = raw->person_bbox[0];
    out->person_bbox[1]    = raw->person_bbox[1];
    out->person_bbox[2]    = raw->person_bbox[2];
    out->person_bbox[3]    = raw->person_bbox[3];
    out->phone_detected    = raw->phone_detected;
    out->phone_near_hand   = raw->phone_in_hand;
    out->head_pitch        = raw->head_pitch;
    out->head_yaw          = raw->head_yaw;
    out->hand_motion_score = raw->hand_motion;
    out->confidence        = raw->confidence;
    out->timestamp_ms      = ts;
}

void perception_debounce_push(const cloud_result_t *raw)
{
    g_debounce[g_debounce_idx] = *raw;
    g_debounce_idx = (g_debounce_idx + 1) % 3;
    if (g_debounce_count < 3) {
        g_debounce_count++;
    }
}

void perception_debounce_compute(observation_t *out, uint32_t ts)
{
    /* 最新一帧所在槽位 */
    const cloud_result_t *latest =
        &g_debounce[(g_debounce_idx + 3 - 1) % 3];

    if (g_debounce_count < 3) {
        /* 历史不足 3 帧, 直接输出最新一帧 */
        perception_fill_observation(latest, out, ts);
        return;
    }

    const cloud_result_t *h = g_debounce;
    out->person_present  = perception_majority3(h[0].person_detected,
                                                h[1].person_detected,
                                                h[2].person_detected);
    out->phone_detected  = perception_majority3(h[0].phone_detected,
                                                h[1].phone_detected,
                                                h[2].phone_detected);
    out->phone_near_hand = perception_majority3(h[0].phone_in_hand,
                                                h[1].phone_in_hand,
                                                h[2].phone_in_hand);

    out->head_pitch = perception_median3(h[0].head_pitch,
                                         h[1].head_pitch,
                                         h[2].head_pitch);
    out->head_yaw   = perception_median3(h[0].head_yaw,
                                         h[1].head_yaw,
                                         h[2].head_yaw);

    out->hand_motion_score = perception_max3(h[0].hand_motion,
                                             h[1].hand_motion,
                                             h[2].hand_motion);

    out->confidence = (h[0].confidence + h[1].confidence + h[2].confidence) / 3.0f;

    /* bbox 取最新一帧 (无去抖意义) */
    out->person_bbox[0] = latest->person_bbox[0];
    out->person_bbox[1] = latest->person_bbox[1];
    out->person_bbox[2] = latest->person_bbox[2];
    out->person_bbox[3] = latest->person_bbox[3];

    out->timestamp_ms = ts;
}

/* ------------------------------------------------------------------ */
/* MiMo 识图 (每帧都调, 仅真实链路)                                     */
/* ------------------------------------------------------------------ */

#ifndef PERCEPTION_MOCK

static int mimo_detect(uint8_t *jpeg, size_t jpeg_len, cloud_result_t *raw)
{
    /* 1. Base64 编码 (b64 ≈ 4/3 原图大小, 用堆/PSRAM 避免挤爆小栈) */
    size_t b64_cap = perception_base64_encoded_len(jpeg_len) + 1;
    char *b64 = malloc(b64_cap);
    if (!b64) {
        return FOCUS_ERR_PERCEP_PARAM;
    }
    size_t b64_len = perception_base64_encode(jpeg, jpeg_len, b64, b64_cap);
    if (b64_len == 0) {
        free(b64);
        return FOCUS_ERR_PERCEP_PARAM;
    }

    /* 2. 构造 OpenAI chat/completions 请求体:
     *    model + messages(system + user[image_url base64, text prompt]) */
    size_t img_len = strlen("data:image/jpeg;base64,") + b64_len;
    char *img = malloc(img_len + 1);
    if (!img) {
        free(b64);
        return FOCUS_ERR_PERCEP_PARAM;
    }
    snprintf(img, img_len + 1, "data:image/jpeg;base64,%s", b64);
    free(b64);

    size_t req_cap = img_len + sizeof(MIMO_PROMPT) + 256;
    char *req = malloc(req_cap);
    if (!req) {
        free(img);
        return FOCUS_ERR_PERCEP_PARAM;
    }
    snprintf(req, req_cap,
             "{\"model\":\"%s\",\"max_completion_tokens\":800,"
             "\"messages\":["
             "{\"role\":\"system\",\"content\":\"You are MiMo, an AI assistant "
             "developed by Xiaomi.\"},"
             "{\"role\":\"user\",\"content\":["
             "{\"type\":\"image_url\",\"image_url\":{\"url\":\"%s\"}},"
             "{\"type\":\"text\",\"text\":\"%s\"}]}]}",
             MIMO_MODEL, img, MIMO_PROMPT);
    free(img);

    /* 3. HTTP POST: 单次超时由 wifi_http_post 保证 ≤3s;
     *    失败重试 1 次, 总阻塞 ≤6s (满足开发规范约束) */
    int ret = wifi_http_post(g_api_url, req, g_resp, sizeof(g_resp));
    if (ret < 0) {
        printf("[percep] MiMo HTTP 首发失败 ret=%d, 重试...\n", ret);
        ret = wifi_http_post(g_api_url, req, g_resp, sizeof(g_resp));
    }
    free(req);
    if (ret < 0) {
        printf("[percep] MiMo HTTP 失败 ret=%d\n", ret);
        return FOCUS_ERR_PERCEP_TIMEOUT;
    }

    /* HTTP 成功: 打印响应头一截, 便于区分"网络失败"与"解析失败" */
    printf("[percep] MiMo HTTP OK, resp=%.120s\n", g_resp);

    /* 4. 解析响应 → 云端原始结果 */
    return perception_parse_cloud_response(g_resp, raw);
}

/* MiMo 响应解析 (OpenAI chat/completions 格式):
 *   外层 {choices:[{message:{content:"<JSON文本>"}}]},
 *   content 为模型输出的 JSON 字符串 → cloud_result_t。 */
int perception_parse_cloud_response(const char *resp, cloud_result_t *out)
{
    const char *s;
    const char *start;
    const char *end;
    char buf[512];
    size_t len;
    cJSON *inner;
    cJSON *root;
    cJSON *choices;
    cJSON *msg;
    cJSON *content;

    if (!resp || !out) {
        return FOCUS_ERR_PERCEP_JSON;
    }

    root = cJSON_Parse(resp);
    if (!root) {
        return FOCUS_ERR_PERCEP_JSON;
    }

    /* 取 choices[0].message.content */
    choices = cJSON_GetObjectItem(root, "choices");
    if (!cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
        cJSON_Delete(root);
        return FOCUS_ERR_PERCEP_NODATA;
    }
    msg = cJSON_GetObjectItem(cJSON_GetArrayItem(choices, 0), "message");
    content = cJSON_GetObjectItem(msg, "content");
    if (!cJSON_IsString(content)) {
        cJSON_Delete(root);
        return FOCUS_ERR_PERCEP_NODATA;
    }

    /* content 可能是"解释+JSON"混排, 提取第一个 { 到最后一个 } 的 JSON 对象 */
    s = content->valuestring;
    start = strchr(s, '{');
    end = strrchr(s, '}');
    if (!start || !end || end <= start) {
        cJSON_Delete(root);
        return FOCUS_ERR_PERCEP_JSON;
    }
    len = (size_t)(end - start) + 1;
    if (len >= sizeof(buf)) {
        len = sizeof(buf) - 1;
    }
    memcpy(buf, start, len);
    buf[len] = '\0';

    inner = cJSON_Parse(buf);
    cJSON_Delete(root);
    if (!inner) {
        return FOCUS_ERR_PERCEP_JSON;
    }

    /* 填充 cloud_result_t (扁平字段, 见 MIMO_PROMPT) */
    out->person_detected = cJSON_IsTrue(cJSON_GetObjectItem(inner, "person_present"));

    cJSON *bbox = cJSON_GetObjectItem(inner, "person_bbox");
    if (cJSON_IsArray(bbox) && cJSON_GetArraySize(bbox) >= 4) {
        for (int i = 0; i < 4; i++) {
            cJSON *v = cJSON_GetArrayItem(bbox, i);
            out->person_bbox[i] = cJSON_IsNumber(v) ? (float)v->valuedouble : 0.0f;
        }
    }

    out->phone_detected = cJSON_IsTrue(cJSON_GetObjectItem(inner, "phone_detected"));
    out->phone_in_hand  = cJSON_IsTrue(cJSON_GetObjectItem(inner, "phone_near_hand"));

    cJSON *pitch = cJSON_GetObjectItem(inner, "head_pitch");
    if (cJSON_IsNumber(pitch)) out->head_pitch = (float)pitch->valuedouble;
    cJSON *yaw = cJSON_GetObjectItem(inner, "head_yaw");
    if (cJSON_IsNumber(yaw)) out->head_yaw = (float)yaw->valuedouble;

    cJSON *motion = cJSON_GetObjectItem(inner, "hand_motion_score");
    if (cJSON_IsNumber(motion)) out->hand_motion = (float)motion->valuedouble;

    cJSON *conf = cJSON_GetObjectItem(inner, "confidence");
    if (cJSON_IsNumber(conf)) out->confidence = (float)conf->valuedouble;

    cJSON_Delete(inner);
    return FOCUS_OK;
}

#endif /* !PERCEPTION_MOCK */

/* ------------------------------------------------------------------ */
/* Mock 路径 (阶段 1, 联调用)                                          */
/*   按预设序列轮流产出 FOCUSED/PLAYING_PHONE/AWAY/DROWSY 对应的        */
/*   observation_t, 不调 MiMo, 不走去抖 (mock 本身已是稳定结果)         */
/* ------------------------------------------------------------------ */

#ifdef PERCEPTION_MOCK

static observation_t mock_observation(int step)
{
    observation_t o;
    memset(&o, 0, sizeof(o));
    o.confidence   = 0.9f;
    o.timestamp_ms = perception_monotonic_ms();

    switch (step % 4) {
    case 0: /* FOCUSED */
        o.person_present = true;
        o.person_bbox[0] = 0.15f; o.person_bbox[1] = 0.10f;
        o.person_bbox[2] = 0.70f; o.person_bbox[3] = 0.85f;
        o.head_pitch = 0.0f;
        o.hand_motion_score = 0.10f;
        break;
    case 1: /* PLAYING_PHONE */
        o.person_present = true;
        o.person_bbox[0] = 0.15f; o.person_bbox[1] = 0.10f;
        o.person_bbox[2] = 0.70f; o.person_bbox[3] = 0.85f;
        o.phone_detected  = true;
        o.phone_near_hand = true;
        o.head_pitch = -25.0f;
        o.hand_motion_score = 0.60f;
        break;
    case 2: /* AWAY */
        o.person_present = false;
        break;
    case 3: /* DROWSY */
        o.person_present = true;
        o.person_bbox[0] = 0.15f; o.person_bbox[1] = 0.10f;
        o.person_bbox[2] = 0.70f; o.person_bbox[3] = 0.85f;
        o.head_pitch = 55.0f;
        o.hand_motion_score = 0.05f;
        break;
    }
    return o;
}

#endif /* PERCEPTION_MOCK */

/* ------------------------------------------------------------------ */
/* 历史管理 (环形缓冲区, 供赵思涵时序分析)                              */
/* ------------------------------------------------------------------ */

static void history_append(const observation_t *obs)
{
    g_history[g_history_count % HISTORY_MAX] = *obs;
    g_history_count++;
}

/* ------------------------------------------------------------------ */
/* 对外接口                                                            */
/* ------------------------------------------------------------------ */

int perception_init(const char *api_url, const char *api_key)
{
    if (api_url) {
        snprintf(g_api_url, sizeof(g_api_url), "%s", api_url);
    } else {
        snprintf(g_api_url, sizeof(g_api_url), "%s", MIMO_ENDPOINT_DEFAULT);
    }
    if (api_key) {
        snprintf(g_api_key, sizeof(g_api_key), "%s", api_key);
    } else {
        snprintf(g_api_key, sizeof(g_api_key), "%s", MIMO_API_KEY_DEFAULT);
    }

#ifndef PERCEPTION_MOCK
    /* 设置 HTTP 鉴权 (Authorization: Bearer), 供 MiMo API 使用 */
    wifi_set_http_auth(g_api_key);
#endif

    memset(g_history, 0, sizeof(g_history));
    g_history_count = 0;

    memset(g_debounce, 0, sizeof(g_debounce));
    g_debounce_count = 0;
    g_debounce_idx   = 0;

    g_mock_seq = 0;
    return FOCUS_OK;
}

int perception_process(uint8_t *jpeg, size_t jpeg_len, observation_t *out)
{
    if (!out || !jpeg || jpeg_len == 0) {
        return FOCUS_ERR_PERCEP_PARAM;
    }

#ifdef PERCEPTION_MOCK
    /* 阶段 1: mock —— 不调 MiMo, 按预设序列轮流产出 observation */
    *out = mock_observation(g_mock_seq++);
#else
    /* 阶段 2: 真实链路 —— 每帧都调 MiMo 识图, 再去抖稳定 */
    cloud_result_t raw;
    memset(&raw, 0, sizeof(raw));
    int ret = mimo_detect(jpeg, jpeg_len, &raw);
    if (ret != FOCUS_OK) {
        printf("[percep] MiMo 识图失败 ret=%d\n", ret);
        return ret;
    }

    /* 云端原始结果打点, 便于真机核对 MiMo 到底识别出了什么 */
    printf("[percep] MiMo person=%d phone=%d inhand=%d pitch=%.1f "
           "motion=%.2f conf=%.2f\n",
           (int)raw.person_detected, (int)raw.phone_detected,
           (int)raw.phone_in_hand, raw.head_pitch,
           raw.hand_motion, raw.confidence);

    perception_debounce_push(&raw);
    perception_debounce_compute(out, perception_monotonic_ms());
#endif

    /* 追加历史 (供赵思涵时序分析) */
    history_append(out);
    return FOCUS_OK;
}

int perception_get_history(observation_t *buf, int n)
{
    if (!buf || n <= 0) {
        return 0;
    }
    if (n > HISTORY_MAX) {
        n = HISTORY_MAX;
    }

    int available = (g_history_count < HISTORY_MAX)
                        ? g_history_count : HISTORY_MAX;
    if (n > available) {
        n = available;
    }

    /* 从旧到新拷贝 */
    int start = (g_history_count > HISTORY_MAX)
                    ? (g_history_count % HISTORY_MAX) : 0;
    for (int i = 0; i < n; i++) {
        buf[i] = g_history[(start + i) % HISTORY_MAX];
    }
    return n;
}
