/****************************************************************************
 * FOCUS AIoT - 跨模块统一错误码 (api/error.h)
 *
 * 所有模块的返回值统一使用以下错误码。
 * 0=成功, 负值=错误。
 * 每个模块的 .c 不应再自定义 errno 语义。
 *
 * 冻结字段: 全部。后续只能追加新码，不得修改已有值。
 ****************************************************************************/

#ifndef FOCUS_AIOT_API_ERROR_H
#define FOCUS_AIOT_API_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 通用 ---- */
#define FOCUS_OK             0    /* 成功 */
#define FOCUS_ERR_UNKNOWN   -1    /* 未知错误 */
#define FOCUS_ERR_PARAM     -2    /* 参数错误 */
#define FOCUS_ERR_NOMEM     -3    /* 内存不足 */
#define FOCUS_ERR_TIMEOUT   -4    /* 操作超时 */
#define FOCUS_ERR_BUSY      -5    /* 设备忙 */
#define FOCUS_ERR_IO        -6    /* IO 错误 */

/* ---- 硬件 ---- */
#define FOCUS_ERR_HW_FAULT -10    /* 硬件故障 */
#define FOCUS_ERR_HW_NOTFOUND -11 /* 硬件未检测到 */
#define FOCUS_ERR_HW_NOTREADY -12 /* 硬件未就绪 */

/* ---- 网络 ---- */
#define FOCUS_ERR_NET_DISCONN  -20 /* 网络未连接 */
#define FOCUS_ERR_NET_DNS      -21 /* DNS 解析失败 */
#define FOCUS_ERR_NET_CONNREF  -22 /* 连接被拒绝 */
#define FOCUS_ERR_NET_HTTP     -23 /* HTTP 错误 */
#define FOCUS_ERR_NET_TLS      -24 /* TLS 握手失败 */

/* ---- 感知 ---- */
#define FOCUS_ERR_PERCEP_TIMEOUT -30 /* 云端 API 超时 */
#define FOCUS_ERR_PERCEP_JSON    -31 /* JSON 解析失败 */
#define FOCUS_ERR_PERCEP_NODATA  -32 /* 无可用 Observation */

/* ---- 行为分析 ---- */
#define FOCUS_ERR_BEHAV_NOHIST  -40 /* Observation 历史不足 */

#ifdef __cplusplus
}
#endif

#endif /* FOCUS_AIOT_API_ERROR_H */
