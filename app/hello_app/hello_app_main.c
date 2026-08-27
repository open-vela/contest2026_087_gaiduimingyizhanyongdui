/****************************************************************************
 * FOCUS AIoT - 主控入口 (多任务事件循环骨架)
 *
 * 负责人: 万思源
 *
 * 架构: 单线程事件循环驱动 6 个逻辑 task。
 *   camera_task    → 采集 / mock 帧事件 (每 5s)
 *   perception_task → 消费帧 → observation_t
 *   behavior_task   → 消费 observation → study_state_t
 *   state_machine   → 消费 study_state + 按键 → FSM + 统计
 *   ui_task         → 1Hz 刷新 LCD / 串口日志
 *   audio_task      → 提醒 / 鼓励触发
 *
 * 当前阶段 (MVP): 全部 mock，只验证链路日志。
 * 后续逐步 #define USE_REAL_* 替换真实模块。
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "api/error.h"
#include "api/audio.h"
#include "api/button.h"
#include "api/lcd.h"
#include "api/perception.h"
#include "api/behavior.h"
#include "api/state_machine.h"
#include "api/session.h"
#include "core/mock.h"

/* ==================== 模块桩调用 (MVP mock) ==================== */

/* 当前阶段: 各模块用 mock 实现，state_machine 已真实化。
 * 后续各模块负责人提交真实 .c 后，mock 桩自动被替换。 */

static void init_modules(void)
{
    session_stats_t initial_stats;
    study_state_t initial_study;

    /* 真实 perception 接口 (PERCEPTION_MOCK 时内部走 mock, 真实 MiMo 配置后自动切换) */
    printf("[perception] init\n");
    perception_init(NULL, NULL);

    printf("[behavior]  init mode=STRICT\n");
    behavior_init(MODE_STRICT);

    /* 张沐泽硬件驱动初始化 (stub 模式为 no-op, 真实模式配置 GPIO/I2S/摄像头) */
    button_init();
    audio_init();
    camera_init();

    if (lcd_init() == FOCUS_OK)
    {
        memset(&initial_stats, 0, sizeof(initial_stats));
        memset(&initial_study, 0, sizeof(initial_study));
        initial_study.status = FOCUSED;
        if (lcd_show_status(DEVICE_IDLE, &initial_stats,
                            &initial_study) == FOCUS_OK)
        {
            printf("[ui]        LCD UI initialized\n");
        }
        else
        {
            printf("[ui]        LCD UI unavailable\n");
        }
    }
    else
    {
        printf("[ui]        LCD backend unavailable\n");
    }
}

/* ==================== 硬件真机验证子命令 ==================== */

#include "api/button.h"
#include "api/camera.h"
#include "api/wifi.h"
#include "api/audio.h"
#include "core/rgb565_jpeg.h"
#include <netutils/netlib.h>

/* `hello_app hwbutton`: 真实按键事件测试 (10s) */
static int hw_test_button(void)
{
    int i;
    button_init();  /* 子命令不走 init_modules, 需自行初始化 */
    printf("===== 硬件验证: 按键 (真实 GPIO) =====\n");
    printf("请按 BOOT 键: 短按(<1s)/长按(1-3s)/超长按(>3s), 10 秒...\n");
    for (i = 0; i < 100; i++)
    {
        button_event_t ev = button_get_event();
        if (ev != BTN_NONE)
        {
            printf("  按键事件: %d (%s)\n", (int)ev,
                   ev == BTN_START_SHORT ? "短按" :
                   ev == BTN_START_LONGPRESS ? "长按" :
                   ev == BTN_STOP ? "超长按" : "其他");
        }
        usleep(100 * 1000);
    }
    return 0;
}

/* `hello_app hwaudio`: 蜂鸣测试 (5 次) */
static int hw_test_audio(void)
{
    int i;
    audio_init();  /* 子命令不走 init_modules, 需自行初始化 */
    printf("===== 硬件验证: 音频 (LED 闪烁降级) =====\n");
    for (i = 0; i < 5; i++)
    {
        printf("  蜂鸣 %d/5...\n", i + 1);
        audio_play_tts("测试蜂鸣");
        usleep(500 * 1000);
    }
    return 0;
}

/* `hello_app hwcamera`: 摄像头采集 3 帧 */
static int hw_test_camera(void)
{
    uint8_t *buf;
    size_t size = 0;
    int i;
    int ret;

    printf("===== 硬件验证: 摄像头 (OV2640) =====\n");
    ret = camera_init();
    if (ret != FOCUS_OK)
    {
        printf("  [FAIL] camera_init: %d\n", ret);
        return 1;
    }

    buf = malloc(320 * 240 * 2);  /* RGB565 全尺寸 */
    if (buf == NULL)
    {
        printf("  [FAIL] 内存不足\n");
        camera_deinit();
        return 1;
    }

    printf("  采集 3 帧 (驱动单帧模式, 首帧成功即验证链路)...\n");
    for (i = 0; i < 3; i++)
    {
        ret = camera_capture_frame(buf, &size);
        if (ret == FOCUS_OK)
        {
            printf("  frame %d: %u bytes\n", i, (unsigned)size);
        }
        else
        {
            printf("  frame %d: FAILED (%d) — 单帧模式后续帧失败为预期\n", i, ret);
        }
        usleep(500 * 1000);
    }

    free(buf);
    camera_deinit();

    if (size > 0)
    {
        printf("  [PASS] 摄像头链路正常 (采到 %u 字节帧)\n", (unsigned)size);
        return 0;
    }

    printf("  [FAIL] 未采到有效帧\n");
    return 1;
}

/* `hello_app hwwifi <ssid> <password>`: WiFi 连接 + HTTP POST 测试 */
static int hw_test_wifi(int argc, char *argv[])
{
    char resp[1024];
    int ret;

    if (argc < 4)
    {
        printf("用法: hello_app hwwifi <ssid> <password>\n");
        return 1;
    }

    printf("===== 硬件验证: WiFi =====\n");
    /* argv: [hello_app, hwwifi, ssid, password] → ssid=argv[2], pass=argv[3] */
    ret = wifi_connect(argv[2], argv[3]);
    printf("  wifi_connect=%d connected=%d rssi=%d dBm\n",
           ret, wifi_is_connected(), wifi_get_rssi());

    /* DHCP 获取 IP + DNS (netlib_obtain_ipv4addr, 阻塞至完成) */
    printf("  获取 IP (DHCP)...\n");
    {
        int ipret = netlib_obtain_ipv4addr("wlan0");
        printf("  DHCP ret=%d\n", ipret);
    }

    ret = wifi_http_post("http://httpbin.org/post",
                         "{\"test\":1}", resp, sizeof(resp));
    printf("  HTTP POST=%d\n", ret);
    if (ret == FOCUS_OK)
    {
        printf("  resp: %.200s\n", resp);
    }

    return ret == FOCUS_OK ? 0 : 1;
}

/* ==================== 主入口 ==================== */

int main(int argc, char *argv[])
{
    if (argc > 1)
    {
        if (strcmp(argv[1], "hwbutton") == 0) return hw_test_button();
        if (strcmp(argv[1], "hwaudio") == 0)  return hw_test_audio();
        if (strcmp(argv[1], "hwcamera") == 0) return hw_test_camera();
        if (strcmp(argv[1], "hwwifi") == 0)   return hw_test_wifi(argc, argv);
        printf("未知命令: %s\n", argv[1]);
        printf("用法: hello_app [hwbutton|hwaudio|hwcamera|hwwifi <ssid> <pass>]\n");
        return 1;
    }

    printf("\n");
    printf("==================================================\n");
    printf("  FOCUS AIoT v0.3 (真实硬件驱动)\n");
    printf("  板卡: ESP32-S3-EYE / openvela\n");
    printf("==================================================\n");
    printf("  模块状态:\n");
    printf("    state_machine  - 真实 FSM (core/state_machine.c)\n");
    printf("    ui             - 郭黄亦昕 (已集成)\n");
    printf("    behavior       - 赵思涵时序引擎\n");
    printf("    button/audio/camera/wifi - 张沐泽真实驱动\n");
    printf("    perception     - 周礼航感知 (mock 或真实 MiMo)\n");
    printf("  子命令: hello_app hwbutton|hwaudio|hwcamera|hwwifi <ssid> <pass>\n");
    printf("==================================================\n\n");

    /* ---- 模块初始化 ---- */
    init_modules();
    state_machine_init();

    /* ---- 摄像头/感知帧缓冲 (PSRAM heap, 主循环复用) ---- */
    {
        uint8_t *frame = malloc(320 * 240 * 2);
        observation_t obs;
        int frame_tick = 0;

        memset(&obs, 0, sizeof(obs));
        if (frame == NULL)
        {
            printf("[core] 帧缓冲分配失败, 感知暂停\n");
        }

        printf("[core] 进入主循环: 状态机 @10Hz + 感知 @5s/帧...\n");
        printf("[core] 真实按键: 长按进模式选择, 短按开始, 超长按停止\n\n");

        /* ---- 主循环: state_machine + 感知驱动 ---- */
        for (;;)
        {
            state_machine_tick();   /* 内部 behavior_analyze 消费感知历史 */

            /* 每 50 tick (5s) 采一帧喂感知, 更新 history 供行为引擎 */
            if (++frame_tick >= 50)
            {
                frame_tick = 0;
#ifdef PERCEPTION_MOCK
                /* mock: 假帧驱动 (perception 内部走 mock_observation) */
                if (frame != NULL)
                {
                    memset(frame, 0, 64);
                    perception_process(frame, 64, &obs);
                }
#else
                /* 真实: 摄像头采集 RGB565 → 转 JPEG → MiMo 识图 */
                if (frame != NULL)
                {
                    static uint8_t jpeg_buf[320 * 240];
                    size_t fsize = 0;
                    size_t jpeg_size = sizeof(jpeg_buf);

                    if (camera_capture_frame(frame, &fsize) == FOCUS_OK &&
                        fsize > 0)
                    {
                        if (rgb565_to_jpeg(frame, 320, 240,
                                           jpeg_buf, &jpeg_size) == 0)
                        {
                            perception_process(jpeg_buf, jpeg_size, &obs);
                        }
                    }
                }
#endif
            }

            usleep(100 * 1000);  /* 100ms = 10Hz */
        }
    }

    return EXIT_SUCCESS;
}
