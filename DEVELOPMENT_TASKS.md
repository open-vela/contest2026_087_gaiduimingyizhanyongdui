# FOCUS AIoT - 成员任务安排

> 更新: 2026-08-27 | 负责人: 万思源
> 前提: 接口已冻结 (`api/*.h`), 状态机/LCD/UI/行为引擎/硬件全栈已完成。

## 当前完成状态

| 模块 | 负责人 | 状态 |
|---|---|---|
| api/*.h 接口冻结 | 万思源 | ✅ |
| core/state_machine.c | 万思源 | ✅ 已合并 |
| ui/ (LCD页面+MiMo+字体) | 郭黄亦昕 | ✅ 已合并 |
| hardware/lcd_st7789.c | 张沐泽 | ✅ 已合并, 真机验证通过 |
| behavior/ (时序引擎+配置表+单测) | 赵思涵 | ✅ 已合并, 单测 100% 通过 |
| hardware/ 全栈 (按键/WiFi/摄像头/音频) | 张沐泽 | ✅ **真机核验 100% 通过** (PR #10) |
| perception/ (MiMo 云端识图) | 周礼航 | ✅ **真机链路打通** (PR #14) |

**关键衔接**: `core/mock.c` 已提供 **weak `perception_get_history()`** 临时历史提供器,
真实感知模块接入后由强符号自动覆盖 (和 LCD stub 相同的替换机制)。行为引擎当前直接
消费 mock 历史即可运行, 无需等待真实感知。

**硬件驱动状态 (2026-08-19 真机核验完成)**:
- LCD ✅ 上屏 / 按键 ✅ 长短按 / 音频 ✅ LED 降级 (板载无蜂鸣器)
- 摄像头 ✅ RGB565 采帧 / WiFi ✅ 连接+DHCP+HTTP POST
- 真机核验修复: camera 格式/缓冲、wifi ioctl 流程、audio 降级等, 见 PR #10

**全链路状态 (2026-08-27 真机验证)**: 摄像头 → TinyJPEG → HTTP 中继 → MiMo 识图 → 行为/FSM
**已跑通**, 实测识别出人物并跟踪头部俯仰角 (`person=1 pitch=±(随动作变化)`)。见 PR #13/#14。

---

## 一、张沐泽 - 硬件驱动全栈 (`hardware/`)

**原则**: 每个驱动独立 `.c` + Kconfig 开关 + mock 可回退 (和 LCD 一样的模式)。

### 任务 1: 按键驱动 (优先)

- **文件**: `hardware/buttons.c`
- **核心函数**: `button_init()`, `button_get_event()`
- **效果**: 长按进入模式选择、短按开始/暂停——**用手按驱动 FSM** (替换当前 mock 自动序列), 串口看到真实按键事件。

### 任务 2: WiFi 驱动

- **文件**: `hardware/wifi_esp32.c`
- **核心函数**: `wifi_connect()`, `wifi_http_post()`, `wifi_get_rssi()`
- **效果**: 连上 WiFi, `wifi_http_post` 到测试地址返回成功 (替换 `wifi_stub.c`, 让 MiMo 走真实 API)。

### 任务 3: 摄像头驱动

- **文件**: `hardware/camera_ov2640.c`
- **核心函数**: `camera_init()`, `camera_capture_frame()`
- **效果**: 采到一帧 JPEG, 大小合理 (供周礼航取帧发云端)。

### 任务 4: 音频驱动

- **文件**: `hardware/audio_i2s.c`
- **核心函数**: `audio_play_tts()`, `audio_play_buzzer()`
- **效果**: REMIND 时发出警告音/蜂鸣 (替换 mock 打印)。

### ⚠️ 接线注意

当前 `core/mock.c` 定义了 `button_get_event`/`audio_play_tts` 等**普通函数**, 你接入真实驱动会冲突。
解决方案 (和 LCD 相同): **Kconfig 开关二选一编译**, 例如:
- `CONFIG_CONTEST2026_087_BUTTON_STUB=y` → 编译 mock
- 否则 → 编译你的真实驱动

`Makefile`/`CMakeLists.txt` 已有这个 if/else 模式 (参考 LCD_STUB)。

### 验收

- 按键: 长按进入模式选择, 短按开始/暂停, FSM 全程用手操作。
- WiFi: `wifi_http_post` 返回成功, MiMo 请求能发出去。
- 摄像头: `camera_capture_frame` 返回有效 JPEG 帧。
- 音频: REMIND 触发真实提示音。

---

## 二、周礼航 - 视觉感知 (`perception/`) ✅ 已实现

**任务**: 实现 `api/perception.h` 三个函数, 从图片输出 `observation_t`。已完成。

### 📁 文件

```
perception/perception.c          ← 主实现 (三函数, 真实 MiMo 链路)
perception/perception_internal.h ← 内部声明
```

### 真实云端链路 (MiMo, 经 HTTP 中继)

设备无 TLS 栈无法直连 https 的 MiMo, 走 **HTTP 中继**:
```
设备 --http--> 中继(106.15.192.201:8060, tools/mimo_relay.py) --https--> MiMo
```

`perception_process(jpeg, jpeg_len, out)` 真实路径:
```
base64_encode(jpeg, jpeg_len, b64)              // JPEG 60-160KB → b64
构造 OpenAI chat/completions 请求 (model=mimo-v2.5,
  image_url = "data:image/jpeg;base64,<b64>", max_completion_tokens=2048)
wifi_http_post(中继地址, req, resp, 16384)      // 60s 超时
跳过 HTTP 头 (\r\n\r\n) → cJSON 解析外层 choices[0].message.content
提取第一个 { 到最后一个 } → cJSON 填充 cloud_result_t
3 帧去抖 → observation_t
```

### 真机联调踩坑 (已修复, 见 PR #14)

- **prompt 含字面 JSON 引号** → 请求变非法 JSON, MiMo 400。改纯文字描述字段。
- **wifi_http_post 返回含 HTTP 头的完整响应** → 解析前剥离 `\r\n\r\n`。
- **响应缓冲 4096 太小** → MiMo 推理长时截断末尾 JSON, 加大到 16KB。
- **max_completion_tokens=800 不够** → 推理吃满时 content 空, 改 2048。
- **HTTP 超时 20s 不够** → 推理慢时设备先超时, 改 60s。
- **TinyJPEG 输出可超 76800** → JPEG 缓冲 3×, 溢出显式报错。

### ③ `perception_get_history(buf, n)`
- 环形缓冲区 `obs_history[60]`
- 从旧到新拷贝 (取最近的 n 条)
- 返回实际条数

### 云端 API 协议 (实际: MiMo OpenAI chat/completions)

```
POST http://<中继>:8060/v1/chat/completions   (中继转 https 到 token-plan-cn.xiaomimimo.com)
Request:  {"model":"mimo-v2.5","max_completion_tokens":2048,"messages":[
            {"role":"system","content":"You are MiMo, an AI assistant developed by Xiaomi."},
            {"role":"user","content":[
              {"type":"image_url","image_url":{"url":"data:image/jpeg;base64,<b64>"}},
              {"type":"text","text":"<prompt>"}]}]}
Response: {"choices":[{"message":{"content":"<JSON: person_present/phone_detected/...>"}}]}
```
```

### 去抖 (阶段 2)

3 帧滑动窗口:
- bool 字段 (person/phone/near_hand): 多数投票
- float 字段 (pitch/yaw): 中位数
- hand_motion: 取最大值 (宁可多报)
- confidence: 取均值

### 时间戳

- 用 `clock_gettime(CLOCK_MONOTONIC)` (参考 `core/state_machine.c` 的 monotonic_ms)
- 每帧写入 `out->timestamp_ms`

### 关键约束

- 摄像头逐帧触发: `camera_capture_frame()` (RGB565) + TinyJPEG 转 JPEG (见 `core/rgb565_jpeg.c`)。
- HTTP 超时 60s (推理慢), JSON 缺字段不崩 (cJSON 判空)。
- **不区分严格/鼓励模式** (那是赵思涵的事)。
- 失败返回: `FOCUS_ERR_PERCEP_TIMEOUT(-30)` / `FOCUS_ERR_PERCEP_JSON(-31)` / `FOCUS_ERR_PERCEP_NODATA(-32)`

### 中继部署 (tools/mimo_relay.py, 租用服务器)

```bash
RELAY_TOKEN=<设备端 Authorization 令牌> \
MIMO_API_KEY=<MiMo key> \
PORT=8060 \
nohup python3 /root/mimo_relay.py > /root/relay.log 2>&1 &
```

- 放行安全组 8060 入方向 (TCP / 0.0.0.0/0)。
- 必须用 `ThreadingHTTPServer` (单线程在 MiMo 慢请求时接收队列积压丢 SYN)。
- 设备端 endpoint/token 在 `perception.c` 的 `MIMO_ENDPOINT_DEFAULT` / `MIMO_API_KEY_DEFAULT`。

### 编译接入

- `Makefile`/`CMakeLists.txt` 的 `CSRCS` 加 `perception/perception.c`
- mock 路径保留: 用 `#ifdef PERCEPTION_MOCK` 或 Kconfig 开关, 默认 mock 可回退
- ⚠️ 提供强符号 `perception_get_history()` 即自动覆盖 `core/mock.c` 的 weak 桩

### 验收

- `perception_process()` 返回有效 observation (mock + 真实双路径)。
- `perception_get_history()` 返回历史, 时间戳递增。
- 云端失败返回错误码, 不阻塞 >60s。
- 真机: `hello_app` 主循环 behavior 消费真实 history 而非 mock (PR #14 已打通)。

---

## 三、赵思涵 - 行为分析 (`behavior/`)

**任务**: 实现 `api/behavior.h`, 消费 observation 序列输出 `study_state_t`。

### 任务 1: 迁移配置表 ✅ 已完成

- **文件**: `behavior/behavior_config.c`
- `g_mode_configs`/`strict_messages`/`gentle_messages` 已迁出 `hello_app_main.c`。
- 主入口已调用 `behavior_init(MODE_STRICT)`。

### 任务 2: 真实时序引擎 ✅ 已完成

- **文件**: `behavior/behavior.c` (401 行)
- `behavior_analyze()` 从 `perception_get_history()` 做时序判定:
  - 玩手机 8s/15s (严格/鼓励), 离座 10s/20s, 瞌睡 8s/12s
  - 优先级: AWAY > 玩手机 > 看手机 > 瞌睡 > 里程碑 > FOCUSED
  - 提醒冷却 `remind_cooldown_sec`, 累计提醒, 里程碑去重
  - 同帧防重 (`g_last_state` 缓存), uint32 时间回绕处理

### 任务 3: 纯 C 单测 ✅ 已完成

- **文件**: `tests/behavior_test.c` (250 行)
- 覆盖: 空历史/严格手机+冷却/看手机/离座/瞌睡/鼓励阈值+累计/里程碑
- 宿主机单测 **100% 通过** (带 `-Werror` 编译零警告)

### 审查结论 (2026-08-18)

- 行为引擎代码质量高, 时序判定/冷却/里程碑逻辑完整。
- 固件编译通过, `behavior_*` 强符号链接, 无 mock 残留。
- 与 mock 感知的弱 `perception_get_history` 衔接正确, 真实感知接入后自动覆盖。

---

## 协作关系

```
张沐泽 camera_capture_frame ──▶ 周礼航 perception_process ──▶ observation_t
                                    │
                                    ▼ 周礼航 perception_get_history
张沐泽 wifi_http_post ──▶ 周礼航 (云端请求)
                                    │
                                    ▼
                              赵思涵 behavior_analyze ──▶ study_state_t
                                    │
                                    ▼
                          (万思源 state_machine 已就绪, 消费它)
```

- 赵思涵依赖周礼航的 `perception_get_history`。
- 周礼航依赖张沐泽的 camera + wifi。
- **张沐泽先做摄像头+WiFi, 周礼航先 mock 后接真, 赵思涵先单测后联调。**

## 推进节奏 (更新 2026-08-19)

| 阶段 | 内容 | 状态 |
|---|---|---|
| 行为分析 | 配置表迁移 + 时序引擎 + 单测 | ✅ 赵思涵已完成 |
| 硬件全栈 | 按键/WiFi/摄像头/音频驱动 | ✅ 张沐泽真机核验 100% |
| 进行中 | 周礼航 perception 实现 (最后一条线) | 进行中 |
| 待 | 全链路联调 (真实 observation → behavior → state_machine → UI) | 待 |

## 当前焦点

1. **周礼航**: perception_process 实现, 替换 mock (先固定 observation, 再接云端), 提供强符号 `perception_get_history()` 接管历史。摄像头已就绪 (张沐泽, RGB565 逐帧采集)。
2. 全链路联调: 真实 observation → 行为引擎 (已就绪) → 状态机 → UI 上屏。
3. 硬件验证子命令: `hello_app hwbutton|hwaudio|hwcamera|hwwifi <ssid> <pass>` 保留供联调排查。
