# FOCUS AIoT 学习状态监测终端

## 一、作品简介

FOCUS AIoT 是面向 openvela 开发板的学习状态可视化终端。它接收状态机与行为识别模块推送的会话数据，在 240×240 RGB565 屏幕上展示待机、模式选择、实时监测、提醒/鼓励和学习报告，并在学习结束后调用 MiMo 生成个性化建议。项目支持“严格监督”和“温暖陪伴”两套视觉反馈，后者不使用红色警告，以减少使用者的焦虑感。

本仓库负责 UI 与 MiMo 建议适配层，不轮询业务状态，也不直接访问摄像头或 SPI；上板时通过弱符号接口与 ST7789V framebuffer 驱动及网络模块集成。

## 二、选题方向

AI 硬件产品创新。作品把端侧行为识别结果转化为即时、可理解的视觉反馈，并结合大模型给出学习复盘建议，形成“感知—反馈—总结”的完整闭环。

## 三、已实现功能

- 4 个设备页面：待机、模式选择、实时监测、学习报告。
- 严格/鼓励双模式主题，覆盖 5 种学习状态，共 10 种图标样式。
- 总时长、有效时长、分心次数、专注度和鼓励模式里程碑展示。
- 提醒消息红色 500ms 闪烁、鼓励消息绿色渐变，3 秒后自动恢复主画面。
- `lcd_show_message()` 仅复制消息并唤醒 UI 线程，不等待动画结束。
- MiMo JSON 请求、5 秒超时参数和网络失败本地降级；建议不会为空。
- 115.2KB RGB565 framebuffer；图标采用实时矢量绘制，避免额外占用 128KB 静态图标内存。
- PC 端单元测试，覆盖双模式颜色、异步消息恢复、报告页和 MiMo 降级。

## 四、目录结构

```text
app/focus_aiot/
├─ include/
│  ├─ focus_types.h       # 状态机、会话和行为数据契约
│  ├─ lcd.h               # LCD 对外接口与可选板级钩子
│  └─ mimo.h              # MiMo 建议接口与 HTTP 适配钩子
├─ src/
│  ├─ lcd.c               # 页面渲染、缓存与异步消息任务
│  ├─ lcd_icons.c         # 双模式状态图标
│  ├─ ui_draw.c           # RGB565 基础图元和轻量字体
│  └─ mimo.c              # JSON 请求与本地降级建议
├─ tests/                 # 可独立运行的 PC 端测试
├─ CMakeLists.txt         # openvela CMake 构建入口
├─ Makefile / Make.defs   # openvela Make 构建入口
└─ Kconfig                # CONFIG_CONTEST2026_087_FOCUS_AIOT
logs/                     # AI Coding 对话日志提交目录
开发规范与目标.md          # 组内接口契约与验收目标
```

## 五、集成接口

状态机调用：

```c
int lcd_init(void);
int lcd_show_status(device_status_t status, session_stats_t *stats,
                    study_state_t *study);
int lcd_show_report(session_report_t *report, const char *advice);
int lcd_show_message(const char *message, action_t type);
int mimo_get_advice(session_stats_t *stats, uint8_t by_type[4],
                    char *output, size_t output_size);
```

板级驱动可提供以下强符号；未提供时会使用内置 framebuffer，便于独立测试：

```c
int st7789v_init(void);
uint16_t *st7789v_get_framebuffer(void);
int st7789v_present(const uint16_t *pixels, int width, int height);
```

网络模块可提供：

```c
int mimo_http_post(const char *url, const char *json,
                   char *response, size_t response_size,
                   unsigned int timeout_ms);
```

真实 MiMo 地址通过编译宏配置，例如 `-DMIMO_API_URL=\"https://example/api\"`。仓库不保存令牌或密钥。

## 六、拉取、编译与运行

按大赛 manifest 拉取完整 openvela 工作区：

```bash
repo init -u https://github.com/open-vela/contest2026_087_gaiduimingyizhanyongdui \
  -b dev-ai-contest-2026 -m contest2026_087_gaiduimingyizhanyongdui.xml
repo sync -c -j8
```

项目会映射到 `packages/demos/contest2026_087_focus_aiot`。在 menuconfig 中启用：

```text
CONFIG_CONTEST2026_087_FOCUS_AIOT=y
```

随后按所用开发板配置从 openvela 工作区根目录构建：

```bash
./build.sh <board-config-path> -j8
```

在支持 CMake 与 pthread 的电脑上可单独验证 UI 逻辑：

```bash
cmake -S app/focus_aiot/tests -B build/focus-aiot-tests
cmake --build build/focus-aiot-tests
ctest --test-dir build/focus-aiot-tests --output-on-failure
```

## 七、联调说明

1. LCD 驱动提供三个 `st7789v_*` 接口，确认 framebuffer 像素格式为 RGB565、尺寸为 240×240。
2. 状态机以 1Hz 调用 `lcd_show_status()`；行为模块把原始提醒文本放入 `study_state_t.message`。
3. 消息动画由 UI 线程管理，状态机无需延时或再次确认。
4. 学习结束时可先用空建议显示报告，再异步调用 `mimo_get_advice()`；结果返回后再次调用 `lcd_show_report()` 更新页面。
5. 接入真实 MiMo 服务时，由网络模块实现 `mimo_http_post()` 并在其内部处理鉴权。

## 八、AI Coding 使用说明

本项目借助 AI 完成需求拆解、模块边界设计、RGB565 绘图实现、异步消息状态管理、降级策略、测试用例与文档整理。开发过程中以组内《开发规范与目标》为约束逐项实现，并通过本地编译、警告检查和单元测试验证。完整对话日志需按大赛要求导出到 `logs/<GitHub账号>/<日期>/` 后提交。

## 九、当前限制

- 内置轻量字体完整支持 ASCII；中文消息目前以占位字形显示。上板联调时可在绘图层接入团队统一的中文字库。
- 仓库未包含真实 MiMo 地址、鉴权信息和具体开发板配置，这三项需由网络模块、密钥配置和板级工程提供。
