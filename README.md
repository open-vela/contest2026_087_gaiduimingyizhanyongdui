# FOCUS AIoT 学习状态监测终端

## 作品简介

FOCUS AIoT 在 ESP32-S3-EYE 上采集学习状态，并通过 240×240 LCD 展示实时反馈。设备支持严格模式和鼓励模式，显示专注、看手机、玩手机、离座、瞌睡五种状态。学习结束后，设备显示时长、分心次数、专注度和 MiMo 学习建议。

本次提交完成 LCD 页面、双模式图标、中文提醒、异步消息覆盖层和 MiMo 降级建议，并接入团队已经合并的 `api/*.h` 接口与 mock 主链路。

## 已实现功能

- 待机、模式选择、实时监测和学习报告四类页面。
- 严格模式和鼓励模式各五种状态图标。
- 总时长、有效时长、分心次数、专注度和鼓励里程碑显示。
- 提醒消息以 500ms 周期闪烁，鼓励消息使用绿色渐变，三秒后自动恢复监测页面。
- 中文点阵字库支持项目内置提醒、页面标题和本地建议。未收录字符显示问号，生成脚本可按需要追加字符。
- MiMo 请求使用 `wifi_http_post()`；网络失败或响应为空时生成本地建议。
- 监测页右上角提供 112×84 的实时摄像头预览；320×240 RGB565 源帧采用
  最近邻缩放后写入 LCD framebuffer，并在 JPEG/云端识图之前刷新。
- LCD 和 Wi-Fi 模拟后端可通过 Kconfig 关闭。真实驱动合入后不需要修改 UI 页面代码。

监测页布局（LCD 实际分辨率 240×240）：

```text
┌────────────────────────┐
│ 严格/鼓励          计时 │
│ ┌────────┐ ┌──────────┐ │
│ │ 状态图标 │ │ 摄像头预览 │ │  ← 112×84，实时更新
│ └────────┘ └──────────┘ │
│ 有效时长   分心次数      │
│ 专注度       进度条       │
│ 里程碑（鼓励模式）       │
└────────────────────────┘
```

## 目录结构

```text
app/hello_app/
├─ api/                  # 团队冻结的跨模块接口
├─ config/               # 严格和鼓励模式配置
├─ core/                 # mock 主链路和状态机预留实现
├─ behavior/             # 赵思涵负责的时序行为分析与双模式策略
├─ hardware/             # 可关闭的 LCD、Wi-Fi 模拟后端
├─ tests/                # UI 与行为模块主机单元测试
└─ ui/
   ├─ lcd.c              # 页面渲染和异步消息线程
   ├─ lcd_icons.c        # 双模式状态图标
   ├─ mimo.c             # MiMo 请求与本地降级
   ├─ ui_draw.c          # RGB565 图元、ASCII 与 UTF-8 绘制
   └─ ui_cjk_font.c      # 项目使用的 14×14 中文点阵
board/contest_board/     # ESP32-S3-EYE 硬件验证配置
scripts/build_hwtest.sh  # 硬件验证镜像构建脚本
tools/                   # 中文点阵生成脚本
logs/                    # AI Coding 日志目录
```

## 跨模块接口

状态机和行为模块调用以下接口：

```c
int lcd_show_status(device_status_t status, session_stats_t *stats,
                    study_state_t *study_state);
int lcd_show_report(session_report_t *report, const char *advice);
int lcd_show_message(const char *message, action_t type);
int lcd_show_preview(const uint8_t *rgb565, int src_w, int src_h);
int mimo_get_advice(session_stats_t *stats, uint8_t by_type[4],
                    char *output, size_t output_size);
```

LCD 底层驱动提供：

```c
int lcd_init(void);
int lcd_flush(void);
uint16_t *lcd_get_framebuffer(void);
```

UI 只写 RGB565 framebuffer。刷新由 `lcd_flush()` 完成。网络模块通过 `wifi_http_post()` 接入 MiMo。仓库不保存 API 密钥。

## 构建与运行

按仓库 manifest 拉取完整 openvela 工作区：

```bash
repo init -u https://github.com/open-vela/contest2026_087_gaiduimingyizhanyongdui \
  -b dev-ai-contest-2026 -m contest2026_087_gaiduimingyizhanyongdui.xml
repo sync -c -j8
```

构建硬件验证镜像：

```bash
cd <openvela 工作区根目录>
bash contest2026_087_gaiduimingyizhanyongdui/scripts/build_hwtest.sh
```

产物是 `nuttx/nuttx.bin`，烧录地址为 `0x0`。烧录与硬件测试方法见 `board/contest_board/configs/hwtest/README.md`。

当前 Kconfig 默认状态：

```text
CONFIG_CONTEST2026_087_LCD_STUB=n    # 真实 LCD 驱动 (hardware/lcd_st7789.c, /dev/fb0 + mmap)
CONFIG_CONTEST2026_087_WIFI_STUB=y   # Wi-Fi 仍为离线模拟后端
```

接入真实 Wi-Fi 实现时关闭 `CONFIG_CONTEST2026_087_WIFI_STUB`；如需回到内存 LCD 后端（离线验证 UI）可临时打开 `CONFIG_CONTEST2026_087_LCD_STUB`。

## 主机测试

```bash
cmake -S app/hello_app/tests -B build/focus-aiot-tests
cmake --build build/focus-aiot-tests
ctest --test-dir build/focus-aiot-tests --output-on-failure
```

行为模块也可以脱离硬件单独测试：

```bash
cc -std=c99 -Wall -Wextra -Werror \
  -Iapp/hello_app/api -Iapp/hello_app/config \
  app/hello_app/tests/test_behavior.c \
  app/hello_app/behavior/behavior.c \
  app/hello_app/behavior/behavior_config.c \
  -o build/focus_aiot_behavior_tests
build/focus_aiot_behavior_tests
```

行为引擎只消费 `perception_get_history()` 提供的 Observation 历史，负责
持续时间判定、严格/鼓励阈值、提醒冷却、累计提醒次数和专注里程碑去重。
当前完整应用仍保留 mock 感知历史；真实 `perception.c` 接入后会覆盖该 mock
历史提供器。

测试覆盖公共头文件兼容、严格和鼓励两套主题、三秒消息恢复、报告页面、中文绘制和 MiMo 本地降级。开发提交使用 GCC C99 的 `-Wall -Wextra -Werror` 检查 UI 和完整应用。

## 中文字库更新

修改 `tools/generate_ui_cjk_font.py` 中的 `CHARS` 后运行：

```bash
python tools/generate_ui_cjk_font.py
```

脚本使用 Noto Sans SC 生成 `app/hello_app/ui/ui_cjk_font.c`。设备运行时不依赖字体文件。

## AI Coding 使用说明

AI 用于核对团队接口、迁移已有 UI、检查线程和内存设计、生成测试、定位编译错误，以及整理作品说明。提交前执行了公共头文件检查、凭据扫描、严格编译和主机单元测试。对话日志按组委会要求放入 `logs/<GitHub账号>/<日期>/`。

## 仍需联调的内容

- ~~张沐泽提供 ST7789V 实现后，关闭 `CONFIG_CONTEST2026_087_LCD_STUB`~~ 已接入真实 LCD 驱动 (`hardware/lcd_st7789.c`)，待上板验证 `lcd_flush()` 实际刷新效果与 ≤50ms 时延。
- 网络模块提供真实 MiMo 地址和鉴权配置后，关闭 `CONFIG_CONTEST2026_087_WIFI_STUB`。
- 周礼航接入真实 `perception_get_history()` 后，使用真实 Observation 历史联调行为引擎。
- 状态机完成会话统计后，用真实 `session_stats_t` 和 `session_report_t` 替换主循环中的演示数据。
