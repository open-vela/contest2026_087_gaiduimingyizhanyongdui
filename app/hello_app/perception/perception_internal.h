/****************************************************************************
 * FOCUS AIoT - 视觉感知模块内部声明 (perception/perception_internal.h)
 *
 * 负责人: 周礼航
 * 说明:   对应任务书里的「perception/perception.h (内部声明, 可选)」。
 *         为避免与公开接口 api/perception.h 混淆, 命名为 *_internal.h。
 *         这里暴露的 cloud_result_t 与工具函数仅供 perception.c 实现与
 *         单测 (tests/test_perception.c) 使用，不对其他模块开放。
 ****************************************************************************/

#ifndef PERCEPTION_INTERNAL_H
#define PERCEPTION_INTERNAL_H

#include "../api/perception.h"

/* 云端原始检测结果 (内部使用, 不对外暴露) */
typedef struct {
    bool  person_detected;
    float person_bbox[4];
    bool  phone_detected;
    bool  phone_in_hand;
    float head_pitch, head_yaw;
    float hand_motion;
    float confidence;
} cloud_result_t;

/* ---- 内部工具函数 (实现 + 单测共用) ---- */

uint32_t perception_monotonic_ms(void);

size_t perception_base64_encoded_len(size_t n);
size_t perception_base64_encode(const uint8_t *src, size_t len,
                                char *dst, size_t dst_cap);

bool  perception_majority3(bool a, bool b, bool c);
float perception_median3(float a, float b, float c);
float perception_max3(float a, float b, float c);

void  perception_fill_observation(const cloud_result_t *raw,
                                  observation_t *out, uint32_t ts);
void  perception_debounce_push(const cloud_result_t *raw);
void  perception_debounce_compute(observation_t *out, uint32_t ts);

/* 仅在非 mock (真实云端) 构建中定义, 依赖 cJSON */
#ifndef PERCEPTION_MOCK
int   perception_parse_cloud_response(const char *resp, cloud_result_t *out);
#endif

#endif /* PERCEPTION_INTERNAL_H */
