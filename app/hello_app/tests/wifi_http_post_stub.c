/****************************************************************************
 * FOCUS AIoT - wifi_http_post 联调 stub (tests/wifi_http_post_stub.c)
 *
 * 用途:   宿主机联调时替换张沐泽的真实 wifi_http_post，让 perception 模块
 *         在 Linux 上无需硬件/网络即可跑通「base64 → 构造请求 → POST →
 *         解析云端 VLM 响应 → 去抖」整条真实链路。
 *
 * 说明:   与真机代码一起链接时【不要】编译本文件（会和张沐泽的强符号冲突）。
 *         仅用于 host 单测 / 联调。
 ****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stddef.h>

/* ---- 联调控制项 (测试代码可改) ---- */
int         stub_cloud_call_count = 0;    /* 累计被调用次数 */
const char *stub_cloud_response  = NULL;  /* 自定义响应, NULL 用默认 */
int         stub_cloud_fail_first_n = 0;  /* 前 N 次返回失败 (测重试) */

/* 默认返回一个「玩手机」场景的云端 VLM 响应 (字段最全, 便于验证解析)。
 * 外层是 OpenAI 兼容 chat completions envelope, content 里嵌 observation JSON。 */
static const char DEFAULT_RESPONSE[] =
    "{"
    "\"choices\":[{"
    "\"message\":{"
    "\"content\":\""
        "{\\\"person\\\":{\\\"detected\\\":true,\\\"bbox\\\":[0.15,0.10,0.70,0.85]},"
        "\\\"phone\\\":{\\\"detected\\\":true,\\\"near_hand\\\":true},"
        "\\\"head_pose\\\":{\\\"pitch\\\":-25.3,\\\"yaw\\\":2.1},"
        "\\\"hand_motion_score\\\":0.35,"
        "\\\"confidence\\\":0.92}"
    "\"}}"
    "]"
    "}";

int wifi_http_post(const char *url, const char *body, char *resp, size_t maxlen)
{
    (void)url;   /* 联调 stub: 不真正发请求, 忽略 url/body */
    (void)body;
    stub_cloud_call_count++;

    /* 模拟前 N 次失败 (perception 内部会重试 1 次) */
    if (stub_cloud_fail_first_n > 0) {
        stub_cloud_fail_first_n--;
        return -1;
    }

    const char *r = stub_cloud_response ? stub_cloud_response : DEFAULT_RESPONSE;
    if (resp && maxlen > 0) {
        snprintf(resp, maxlen, "%s", r);
    }
    return 0;
}
