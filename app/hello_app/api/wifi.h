/****************************************************************************
 * FOCUS AIoT - Wi-Fi 抽象接口 (api/wifi.h)
 *
 * 负责人: 张沐泽
 * 职责: STA 模式连接、HTTP POST、RSSI 查询。
 *
 * 冻结: 所有函数签名。超时策略内部实现，调用方只关心返回值。
 ****************************************************************************/

#ifndef FOCUS_AIOT_API_WIFI_H
#define FOCUS_AIOT_API_WIFI_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 连接管理 ---- */

/* 连接 Wi-Fi (STA 模式)。ssid/password 从 NVS 读取或首次配置。
 * 返回: 0=成功 */
int  wifi_connect(const char *ssid, const char *password);

/* 检查 Wi-Fi 是否已连接。 */
bool wifi_is_connected(void);

/* 获取当前信号强度 (dBm)。 */
int  wifi_get_rssi(void);

/* ---- HTTP 客户端 ---- */

/* HTTP POST 请求 (阻塞，超时 3s + 重试 1 次，总计 ≤6s)。
 * url:     请求地址
 * body:    JSON 请求体 (调用方构造)
 * resp:    响应缓冲区 (调用方分配)
 * maxlen:  响应最大长度
 * 返回: 0=成功, -1=连接失败, -2=超时, -3=HTTP 错误 */
int  wifi_http_post(const char *url, const char *body,
                    char *resp, size_t maxlen);

#ifdef __cplusplus
}
#endif

#endif /* FOCUS_AIOT_API_WIFI_H */
