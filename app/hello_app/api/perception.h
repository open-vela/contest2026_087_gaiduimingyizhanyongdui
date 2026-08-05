/****************************************************************************
 * FOCUS AIoT - 视觉感知模块接口 (api/perception.h)
 *
 * 负责人: 周礼航
 * 职责: 从 JPEG 图片中提取统一 Observation (单帧空间分析)。
 *       云端 API 调用、去抖稳定、历史管理都在本模块内完成。
 *       不区分严格/鼓励模式，不判断行为。
 *
 * 冻结: observation_t 字段、3 个函数签名。
 *       后续只能追加 observation_t 字段，不得删除或重命名已有字段。
 ****************************************************************************/

#ifndef FOCUS_AIOT_API_PERCEPTION_H
#define FOCUS_AIOT_API_PERCEPTION_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- observation_t: 单帧视觉感知结果 ---- */

typedef struct {
    /* 人体检测 */
    bool     person_present;       /* 画面中是否有人 */
    float    person_bbox[4];       /* 人体边界框 (x,y,w,h) 归一化 [0-1] */

    /* 手机检测 */
    bool     phone_detected;       /* 是否检测到手机 */
    bool     phone_near_hand;      /* 手机是否在手部区域 */

    /* 头部姿态 (度, 负=低头 正=仰头) */
    float    head_pitch;
    float    head_yaw;

    /* 手部运动量 [0-1] */
    float    hand_motion_score;

    /* 整体置信度 [0-1] */
    float    confidence;

    /* 时间戳 (毫秒, 自系统启动计) */
    uint32_t timestamp_ms;

    /* === 保留扩展字段 (>= v2 追加) === */
    uint8_t  _reserved[16];
} observation_t;

/* ---- 接口函数 ---- */

/* 初始化: 配置云端 API URL/Key。
 * 返回: 0=成功 */
int  perception_init(const char *api_url, const char *api_key);

/* 核心: 拿一张 JPEG → 调云端 API → 去抖 → 输出一个 observation_t。
 * jpeg / jpeg_len: 输入图像数据 (可来自真实摄像头或 mock 缓冲区)
 * out:             输出的 Observation
 * 超时 ≤6s，失败返回错误码 (不崩)。
 * 返回: 0=成功, FOCUS_ERR_PERCEP_TIMEOUT / FOCUS_ERR_PERCEP_JSON */
int  perception_process(uint8_t *jpeg, size_t jpeg_len, observation_t *out);

/* 获取最近 N 个 Observation (从旧到新排列)。
 * buf: 输出缓冲区 (调用方分配, 至少 n * sizeof(observation_t))
 * n:   最多返回条数
 * 返回: 实际返回条数 (≤ n) */
int  perception_get_history(observation_t *buf, int n);

#ifdef __cplusplus
}
#endif

#endif /* FOCUS_AIOT_API_PERCEPTION_H */
