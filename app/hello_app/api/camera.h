/****************************************************************************
 * FOCUS AIoT - 摄像头抽象接口 (api/camera.h)
 *
 * 负责人: 张沐泽
 * 职责: 封装 OV2640 DVP 采集，对上层提供统一的帧输出。
 *
 * 冻结: camera_capture_frame() 签名、输出格式、外部变量名。
 ****************************************************************************/

#ifndef FOCUS_AIOT_API_CAMERA_H
#define FOCUS_AIOT_API_CAMERA_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 公共接口 ---- */

/* 初始化 OV2640，配置 QVGA 输出。
 * 返回: 0=成功, 负值=错误码 (见 error.h) */
int  camera_init(void);

/* 采集一帧图像，存入 buf（调用方在 PSRAM 中分配，至少 50KB）。
 * size 输出实际字节数。
 * 阻塞上限 500ms，超时返回 FOCUS_ERR_TIMEOUT。
 * 返回: 0=成功 */
int  camera_capture_frame(uint8_t *buf, size_t *size);

/* 关闭摄像头，释放 DMA。 */
void camera_deinit(void);

/* 最近一帧 JPEG 数据指针和大小。
 * 由 camera_task 采集完成后更新，perception_task 读取。
 * 读取方不应写这两个变量。 */
extern uint8_t *camera_frame_buffer;
extern size_t   camera_frame_size;

#ifdef __cplusplus
}
#endif

#endif /* FOCUS_AIOT_API_CAMERA_H */
