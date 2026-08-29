/****************************************************************************
 * FOCUS AIoT - 视觉感知模块 (perception/perception.c)
 *
 * 负责人: 周礼航
 * 职责:   从一张 JPEG 里看出「有什么」——只做空间维度的单帧静态分析，
 *         不做时间维度的行为推理 (那是赵思涵 behavior 模块的事)。
 *
 * 输入:   JPEG 图像 (来自张沐泽 camera_frame_buffer / camera_capture_frame)
 * 输出:   observation_t (每帧一个, 经云端 VLM 识图接口 + 3 帧去抖)
 *
 * 云端 VLM 调用时机: 每帧都调 (perception_process 每次收到一帧 JPEG 都调一次
 *                    cloud_detect(), 失败重试 1 次, 总阻塞 ≤6s)
 *
 * 云端 VLM 契约 (OpenAI 兼容 /v1/chat/completions, 模型默认 Qwen2.5-VL-3B):
 *   Request:  {"model":"Qwen/Qwen2.5-VL-3B-Instruct",
 *              "messages":[{"role":"user","content":[
 *                {"type":"image_url","image_url":{"url":"data:image/jpeg;base64,<b64>"}},
 *                {"type":"text","text":"<PROMPT>"}]}],
 *              "temperature":0,"max_tokens":512}
 *   Response: {"choices":[{"message":{"content":"<observation JSON>"}}]}
 *             content 里的 JSON 字段: person/phone/head_pose/hand_motion_score/confidence
 *
 * 三个对外接口 (冻结于 api/perception.h):
 *   perception_init()         初始化 VLM endpoint/key + 清空历史
 *   perception_process()      一帧 JPEG → (调云端 VLM) → observation_t
 *   perception_get_history()  取最近 N 条 observation (供 behavior 时序分析)
 *
 * 编译开关:
 *   -D PERCEPTION_MOCK  走 mock 路径 (不调云端 VLM), 默认 mock 便于联调/回退
 *                       不定义则走真实云端 VLM 链路 (需要 cJSON + wifi_http_post)
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

#ifndef PERCEPTION_MOCK
#include <cJSON.h>
/* 张沐泽提供, 见 api/wifi.h (前向声明, 避免独立编译依赖) */
int wifi_http_post(const char *url, const char *body, char *resp, size_t maxlen);
#endif

/* ------------------------------------------------------------------ */
/* 内部配置                                                            */
/* ------------------------------------------------------------------ */
#define HISTORY_MAX     60              /* 保留最近 60 条 observation */
#define FRAME_WIDTH     320
#define FRAME_HEIGHT    240
#define MAX_JPEG_SIZE   (50 * 1024)     /* 320x240 JPEG 上限 */
#define RESP_BUF_SIZE   8192            /* 云端 VLM 响应缓冲 (含 envelope 冗余) */

/* 云端 VLM endpoint、鉴权 key 与模型名。
 * 优先级: perception_init() 传入 > 下面的默认值 (endpoint/key)。
 * 模型名是编译期宏 (签名冻结, 运行时不变); 若服务端用 --served-model-name
 * 改了别名, 把 VLM_MODEL_DEFAULT 改成那个别名即可。
 * TODO: 把 endpoint 填成自托管 /v1/chat/completions 真实地址; 鉴权按实际情况填。
 *       wifi_http_post(url, body, resp, maxlen) 带不了 header, 自托管 vLLM 默认
 *       无鉴权跑局域网即可; 若网关要求 Authorization, key 走 URL query 传入,
 *       g_api_key 暂作预留 (需与张沐泽确认)。 */
#define VLM_MODEL_DEFAULT      "Qwen/Qwen2.5-VL-3B-Instruct"
#define VLM_ENDPOINT_DEFAULT   ""
#define VLM_API_KEY_DEFAULT    ""

/* 发给 VLM 的固定指令: 只输出一个 JSON 对象, 不带说明/围栏。
 * 注意保持单行、不含双引号/反斜杠/换行 (会被原样嵌入 JSON text 字段)。 */
#define VLM_PROMPT \
    "You are a vision detector for a study-space camera. " \
    "Look at the image and return exactly one JSON object, nothing else " \
    "(no markdown, no explanation). Required keys: " \
    "person.detected (true/false), " \
    "person.bbox (array of 4 numbers x,y,w,h normalized 0 to 1, all zero if no person), " \
    "phone.detected (true/false), " \
    "phone.near_hand (true/false), " \
    "head_pose.pitch (degrees, negative means looking down), " \
    "head_pose.yaw (degrees), " \
    "hand_motion_score (0 to 1), " \
    "confidence (0 to 1)."

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
static char           g_resp[RESP_BUF_SIZE]; /* 云端 VLM 响应缓冲 (仅真实链路) */
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
/* 云端 VLM 识图 (每帧都调, 仅真实链路)                                 */
/* ------------------------------------------------------------------ */

#ifndef PERCEPTION_MOCK

/* 从 VLM 输出文本里定位并裁剪出最外层 JSON 对象。
 * content 常被 VLM 套 ```json 围栏或前后加说明文字, cJSON_Parse 要求
 * 结尾只允许空白, 所以这里按大括号深度找到匹配的 '}' 并把其后截断。
 * 注: 我们只输出布尔/数字/数组, JSON 里不会出现带 '{'/'}' 的字符串, 故
 *     不处理字符串内嵌括号的边界情况。返回首个 '{' 指针, 失败返回 NULL。 */
static char *extract_json_object(char *s)
{
    char *start = NULL;
    int depth = 0;

    for (char *p = s; *p; p++) {
        if (*p == '{') {
            if (depth == 0) start = p;
            depth++;
        } else if (*p == '}') {
            if (depth > 0 && --depth == 0) {
                p[1] = '\0';   /* 截断围栏/说明文字 */
                return start;
            }
        }
    }
    return NULL;
}

/* 解析模型输出的 observation JSON (无 code/data 包裹, 字段直接平铺) */
static int parse_observation_json(const char *json, cloud_result_t *out)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return FOCUS_ERR_PERCEP_JSON;
    }

    /* person */
    cJSON *person = cJSON_GetObjectItem(root, "person");
    if (cJSON_IsObject(person)) {
        out->person_detected = cJSON_IsTrue(cJSON_GetObjectItem(person, "detected"));

        cJSON *bbox = cJSON_GetObjectItem(person, "bbox");
        if (cJSON_IsArray(bbox) && cJSON_GetArraySize(bbox) >= 4) {
            for (int i = 0; i < 4; i++) {
                cJSON *v = cJSON_GetArrayItem(bbox, i);
                out->person_bbox[i] = cJSON_IsNumber(v) ? (float)v->valuedouble : 0.0f;
            }
        }
    }

    /* phone */
    cJSON *phone = cJSON_GetObjectItem(root, "phone");
    if (cJSON_IsObject(phone)) {
        out->phone_detected = cJSON_IsTrue(cJSON_GetObjectItem(phone, "detected"));
        out->phone_in_hand  = cJSON_IsTrue(cJSON_GetObjectItem(phone, "near_hand"));
    }

    /* head_pose */
    cJSON *pose = cJSON_GetObjectItem(root, "head_pose");
    if (cJSON_IsObject(pose)) {
        cJSON *pitch = cJSON_GetObjectItem(pose, "pitch");
        if (cJSON_IsNumber(pitch)) out->head_pitch = (float)pitch->valuedouble;
        cJSON *yaw = cJSON_GetObjectItem(pose, "yaw");
        if (cJSON_IsNumber(yaw)) out->head_yaw = (float)yaw->valuedouble;
    }

    cJSON *motion = cJSON_GetObjectItem(root, "hand_motion_score");
    if (cJSON_IsNumber(motion)) out->hand_motion = (float)motion->valuedouble;

    cJSON *conf = cJSON_GetObjectItem(root, "confidence");
    if (cJSON_IsNumber(conf)) out->confidence = (float)conf->valuedouble;

    cJSON_Delete(root);
    return FOCUS_OK;
}

/* 解析 OpenAI 兼容 chat completions 响应 → cloud_result_t */
int perception_parse_cloud_response(const char *resp, cloud_result_t *out)
{
    if (!resp || !out) {
        return FOCUS_ERR_PERCEP_JSON;
    }

    cJSON *root = cJSON_Parse(resp);
    if (!root) {
        return FOCUS_ERR_PERCEP_JSON;
    }

    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (!cJSON_IsArray(choices) || cJSON_GetArraySize(choices) < 1) {
        cJSON_Delete(root);
        return FOCUS_ERR_PERCEP_NODATA;   /* 无 choices: 无数据/服务端错误 */
    }

    cJSON *message = cJSON_GetObjectItem(cJSON_GetArrayItem(choices, 0), "message");
    cJSON *content = cJSON_IsObject(message)
                         ? cJSON_GetObjectItem(message, "content") : NULL;
    if (!cJSON_IsString(content)) {
        cJSON_Delete(root);
        return FOCUS_ERR_PERCEP_NODATA;
    }

    /* content 是 VLM 生成的文本, 提取其中嵌套的 observation JSON 再解析 */
    char *json = extract_json_object(content->valuestring);
    if (!json) {
        cJSON_Delete(root);
        return FOCUS_ERR_PERCEP_JSON;
    }

    int ret = parse_observation_json(json, out);
    cJSON_Delete(root);
    return ret;
}

/* 每帧调用云端 VLM 识图 */
static int cloud_detect(uint8_t *jpeg, size_t jpeg_len, cloud_result_t *raw)
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

    /* 2. 构造 OpenAI 兼容请求体 (+1024 容纳 prompt/data URI/model/围栏) */
    size_t req_cap = b64_len + 1024;
    char *req = malloc(req_cap);
    if (!req) {
        free(b64);
        return FOCUS_ERR_PERCEP_PARAM;
    }
    int n = snprintf(req, req_cap,
        "{\"model\":\"%s\","
        "\"messages\":[{\"role\":\"user\",\"content\":["
        "{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/jpeg;base64,%s\"}},"
        "{\"type\":\"text\",\"text\":\"%s\"}"
        "]}]"
        ",\"temperature\":0,\"max_tokens\":512}",
        VLM_MODEL_DEFAULT, b64, VLM_PROMPT);
    free(b64);
    if (n < 0 || (size_t)n >= req_cap) {
        free(req);
        return FOCUS_ERR_PERCEP_PARAM;
    }

    /* 3. HTTP POST: 单次超时由 wifi_http_post 保证 ≤3s;
     *    失败重试 1 次, 总阻塞 ≤6s (满足开发规范约束) */
    int ret = wifi_http_post(g_api_url, req, g_resp, sizeof(g_resp));
    if (ret < 0) {
        ret = wifi_http_post(g_api_url, req, g_resp, sizeof(g_resp));
    }
    free(req);
    if (ret < 0) {
        return FOCUS_ERR_PERCEP_TIMEOUT;
    }

    /* 4. 解析响应 → 云端原始结果 */
    return perception_parse_cloud_response(g_resp, raw);
}

#endif /* !PERCEPTION_MOCK */

/* ------------------------------------------------------------------ */
/* Mock 路径 (阶段 1, 联调用)                                          */
/*   按预设序列轮流产出 FOCUSED/PLAYING_PHONE/AWAY/DROWSY 对应的        */
/*   observation_t, 不调云端 VLM, 不走去抖 (mock 本身已是稳定结果)       */
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
        snprintf(g_api_url, sizeof(g_api_url), "%s", VLM_ENDPOINT_DEFAULT);
    }
    if (api_key) {
        snprintf(g_api_key, sizeof(g_api_key), "%s", api_key);
    } else {
        snprintf(g_api_key, sizeof(g_api_key), "%s", VLM_API_KEY_DEFAULT);
    }

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
    /* 阶段 1: mock —— 不调云端 VLM, 按预设序列轮流产出 observation */
    *out = mock_observation(g_mock_seq++);
#else
    /* 阶段 2: 真实链路 —— 每帧都调云端 VLM 识图, 再去抖稳定 */
    cloud_result_t raw;
    memset(&raw, 0, sizeof(raw));
    int ret = cloud_detect(jpeg, jpeg_len, &raw);
    if (ret != FOCUS_OK) {
        return ret;
    }

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
