# 视觉感知模块 (perception) —— 周礼航 · 成品

这是「FOCUS AIoT」里 **视觉感知层** 的完整实现。输入一张 JPEG，**每帧调用
云端 VLM 识图接口**，输出一个 `observation_t`，供赵思涵的 `behavior` 模块做时序分析。

> 依据文档：`周礼航开发规范.md`、`DEVELOPMENT_TASKS (2).md`（2026-08-19）、
> `产品方案与分工_v2.md`。只做「空间」上的单帧识别，**不区分严格/鼓励模式**，
> **不做行为判断**（那是赵思涵的事）。

## 文件清单

```
focus_perception/
├── api/perception.h             公开接口 (observation_t + 3 函数 + 错误码)
│                                ⚠️ 参考拷贝, 勿覆盖仓库冻结版
├── perception/
│   ├── perception.c             主实现 (云端 VLM 调用 + 去抖 + 历史 + base64 + cJSON)
│   └── perception_internal.h    内部声明 (cloud_result_t + 工具函数)
├── tests/
│   ├── test_perception.c        mock 单测 (不依赖硬件/网络/cJSON)
│   ├── wifi_http_post_stub.c    联调 stub (替换真实 wifi_http_post)
│   └── test_cloud_vlm.c         真实云端 VLM 链路单测 (需要 cJSON)
├── BUILD_INTEGRATION.md         Makefile/CMakeLists/Kconfig 接入 diff
└── README.md
```

## 云端 VLM 识图（每帧都调）

`perception_process()` 每次收到一帧 JPEG 都调一次 `cloud_detect()`（失败重试 1 次，
总阻塞 ≤6s），默认模型 **Qwen/Qwen2.5-VL-3B-Instruct**，走 OpenAI 兼容的
`POST /v1/chat/completions`（vLLM / Ollama / SGLang 均可托管）：

```
Request:  {"model":"Qwen/Qwen2.5-VL-3B-Instruct",
           "messages":[{"role":"user","content":[
             {"type":"image_url","image_url":{"url":"data:image/jpeg;base64,<b64>"}},
             {"type":"text","text":"<PROMPT>"}]}],
           "temperature":0,"max_tokens":512}

Response: {"choices":[{"message":{"content":"<observation JSON>"}}]}

          content 里的 observation JSON 字段:
          {"person":{"detected":true,"bbox":[0.15,0.10,0.70,0.85]},
           "phone":{"detected":true,"near_hand":true},
           "head_pose":{"pitch":-25.3,"yaw":2.1},
           "hand_motion_score":0.35,"confidence":0.92}
```

endpoint / key 通过 `perception_init(url, key)` 传入（优先级高于
`VLM_ENDPOINT_DEFAULT` / `VLM_API_KEY_DEFAULT` 两个占位宏）；模型名是编译期宏
`VLM_MODEL_DEFAULT`，想换 MiniCPM-V / InternVL 等只改这个宏即可。

> ⚠️ **待确认**：`wifi_http_post(url, body, resp, maxlen)` 带不了 header。
> 自托管 vLLM 默认**无鉴权**跑在局域网即可；若网关要求 `Authorization`，key 走 URL
> query 传入（`g_api_key` 已预留），具体鉴权方式需跟张沐泽/陈泽沛确认。

## 三个对外接口（冻结签名）

```c
int  perception_init(const char *api_url, const char *api_key);
int  perception_process(uint8_t *jpeg, size_t jpeg_len, observation_t *out);
int  perception_get_history(observation_t *buf, int n);
```

返回值（错误码，来自 `api/error.h`，此处兜底定义）：

| 常量 | 值 | 含义 |
|---|---|---|
| `FOCUS_OK` | 0 | 成功 |
| `FOCUS_ERR_PERCEP_TIMEOUT` | -30 | 云端 VLM 超时/请求失败 |
| `FOCUS_ERR_PERCEP_JSON` | -31 | JSON 解析失败 / 缺字段 |
| `FOCUS_ERR_PERCEP_NODATA` | -32 | 响应无 choices / 无数据 |
| `FOCUS_ERR_PERCEP_PARAM` | -33 | 参数非法（本地扩展，可请万思源并入 error.h） |

## 关键设计

- **每帧都调云端 VLM**：`perception_process()` → `cloud_detect()`（base64 → OpenAI
  兼容 JSON → `wifi_http_post` → cJSON 解析 envelope → 提取 content 里的 observation
  JSON）→ 3 帧去抖 → 追加历史。
- **mock / 真实双路径**：`-DPERCEPTION_MOCK` 走 mock（默认，不调云端 VLM）；
  不定义则走真实云端 VLM 链路。
- **JSON 围栏容错**：VLM 输出的 `content` 可能带 ```` ```json ```` 围栏或前后说明文字，
  `extract_json_object()` 按大括号深度定位并截断出最外层 JSON 再解析。
- **3 帧去抖**：bool 多数投票、角度中位数、hand_motion 取最大、confidence 取均值。
  mock 路径本身已是稳定结果，**绕过**去抖，避免把 AWAY 状态「投票投没」。
- **历史环形缓冲**：`obs_history[60]`，`perception_get_history()` 从旧到新返回。
  提供**强符号** `perception_get_history()`，自动覆盖 `core/mock.c` 里的 weak 桩。
- **超时**：HTTP 单次由张沐泽的 `wifi_http_post` 保证 ≤3s；失败重试 1 次，总阻塞 ≤6s。
- **内存**：base64 / 请求体用 `malloc`（ESP32 上建议映射 PSRAM），响应用固定 8KB 静态缓冲。
- **时间戳**：`clock_gettime(CLOCK_MONOTONIC)`（与 `core/state_machine.c` 的 monotonic_ms 一致）。

## 本地单测

```bash
cd focus_perception

# 1) mock 单测 (无 cJSON 依赖)
gcc -Wall -Wextra -Werror -DPERCEPTION_MOCK \
    perception/perception.c tests/test_perception.c -o test_perception
./test_perception

# 2) 真实云端 VLM 链路单测 (需要 cJSON.c/cJSON.h)
gcc -Wall -Wextra -Werror \
    perception/perception.c tests/wifi_http_post_stub.c tests/test_cloud_vlm.c \
    cJSON.c -I/path/to/cjson -o test_cloud_vlm
./test_cloud_vlm
```

预期都是 `0 failed`。mock 测试覆盖：base64（RFC4648 标准向量）、多数投票/中位数/最大值、
3 帧去抖、mock 轮转、历史环形缓冲、空历史/参数非法。VLM 链路测试覆盖：每帧调用次数、
响应解析、JSON 围栏容错、失败重试、三种错误码。

## 接入仓库（openvela 真机）

1. 把 `perception/perception.c`、`perception/perception_internal.h` 放进
   `app/hello_app/perception/`。
2. `api/perception.h` **不要覆盖**，用仓库冻结版（字段/签名必须一致）。
3. 按 `BUILD_INTEGRATION.md` 加 `CSRCS` + Kconfig 开关。
4. 联调/上线前关掉 mock 开关，并填好 VLM endpoint（自托管 `/v1/chat/completions`）。

## 联调顺序

先跟张沐泽联调（确认 `wifi_http_post` 能通 + VLM endpoint/鉴权方式）→ 再跟赵思涵联调
（确认 `observation_t` 字段对）→ 最后全链路：真实 observation → behavior →
state_machine → UI 上屏。
