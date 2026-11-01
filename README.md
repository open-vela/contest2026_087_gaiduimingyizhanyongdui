# FOCUS AIoT — 学习专注监测终端

> 队伍编号 087 ｜ 赛道：AI 硬件产品创新 ｜ 硬件：ESP32-S3-EYE + openvela

## 一、作品简介

FOCUS AIoT 是一台放在书桌上的**学习专注监测终端**。它用板载摄像头持续拍摄学习画面，
通过**视觉模型**判断学习者是否在玩手机、是否离座、是否瞌睡，实时在 240×240 LCD 上
给出状态与提醒；一次学习结束后，设备把统计上报服务器，由 **LLM 生成学习建议**，
完整报告在网页端查看。

**亮点**：

- **双模式**：严格模式（紧盯不放）与鼓励模式（温柔提醒），共用硬件，策略/文案/阈值全不同。
- **端云协同**：设备只负责采集与显示，视觉识别与建议生成放在**自托管模型 + 服务器**，
  设备无需 TLS 栈、无需大算力。
- **网页报告**：设备屏只有 240×240，长文本放不下 —— 完整学习报告（含 LLM 建议正文）
  在网页端呈现，设备屏只提示网址。
- **编码卸载**：设备端不做 JPEG 编码（会占用 8KB 栈帧 + 230KB 缓冲 + 密集浮点），
  只上传**全尺寸**原始 RGB565（不编码也不降采样，保住小目标细节），编码由服务器
  完成 —— 把算力留给采集与交互。
- **全链路可复现**：视觉模型、中转服务器、内网穿透、固件编译烧录，本文档给出从零步骤。

## 二、选题方向

**AI 硬件产品创新**。

理由：本作品是"硬件 + 端侧采集 + 云端 AI"的完整产品形态，重点在于把
**视觉理解模型**落到真实可用的硬件终端上，并解决"设备算力/网络受限时如何使用云端 AI"
这一工程问题（固件无 TLS 栈 → HTTP 中转；屏幕小 → 网页报告）。

## 三、系统架构

```
┌──────────────┐  ① RGB565 采集 (320×240)
│ ESP32-S3-EYE │──────────────┐
│   openvela   │              ▼
│              │       ② 全尺寸 RGB565 (320×240, 150KB) — 不编码不降采样
│ 摄像头/LCD/  │              ▼
│ 按键/LED     │       ③ HTTP POST base64 图 (~200KB, 复用长连接)
└──────┬───────┘              ▼
       │              ┌─────────────────────┐
       │              │  中转服务器(公网)    │
       │              │  tools/mimo_relay.py│
       │              │  :8060              │
       │              │  ★ RGB565 → JPEG    │  ← 编码在服务端
       │              └───┬─────────────┬───┘
       │  ⑤ LLM 建议       │             │ ④ 转发图片
       │                   ▼             ▼
       │            ┌───────────┐  ┌─────────────┐
       │            │ MiMo API  │  │ frp 内网穿透 │
       │            │ (建议生成) │  │ :7000       │
       │            └───────────┘  └──────┬──────┘
       │                                 ▼
       │                        ┌────────────────────┐
       └── ⑥ 网页 /report  ─────│ 本机视觉模型服务    │
              /preview          │ YOLOv8+手部分割:8000│
                                └────────────────────┘
```

**为什么这么设计**：

1. **编码放在服务端**：设备端做 JPEG 编码（TinyJPEG）会占用 8KB 栈帧、230KB 缓冲并做
   密集浮点运算，实测会触发 openvela 在 ESP32-S3 上的稳定性问题。改为设备只上传
   **原始 RGB565**，由服务器用 PIL 转 JPEG —— 设备端零编码负担。
   分辨率保持**全尺寸 320×240**：曾经试过 2× 降采样到 160×120 把传输量压到 1/4，
   但手机这类小目标细节丢失后识别率明显下降，于是改回全尺寸，传输压力由
   **复用长连接 + 加大 TCP/IOB 缓冲**解决（见第 3 点）。
2. **固件没有 TLS 栈**，无法直连 HTTPS 云端 API；视觉模型需要 GPU，跑在 PC 上。
   因此由中转服务器做协议转换与转发，并顺带提供网页报告。
3. **每帧复用同一条 TCP 连接**：感知约 5s 一帧，全尺寸帧体 base64 后约 200KB。
   若每帧都新建/关闭连接，建连速率远超 TIME_WAIT 的回收速率，约 10 帧后 nuttx 的
   TCP/IOB 池见底，`connect` 直接失败（`FOCUS_ERR_NET_DISCONN` = -20）。因此设备端
   保持 keep-alive 长连接、响应按 `Content-Length` 读满即返回；中继侧切到 HTTP/1.1
   配合。同时把 `CONFIG_NET_SEND_BUFSIZE` 调到 64KB、`CONFIG_IOB_NBUFFERS` 调到 512
   留足余量 —— 注意这是**加大缓冲**，不是靠减数据。

## 四、目录结构

```text
contest2026_087_gaiduimingyizhanyongdui/
├─ app/hello_app/            # 设备端应用（openvela 应用）
│  ├─ api/                   # 团队冻结的跨模块接口
│  ├─ core/                  # 状态机 FSM、会话统计、图像编码、串口链路
│  │  ├─ state_machine.c     #   4 状态：IDLE/MODE_SELECT/MONITORING/REPORT
│  │  ├─ rgb565_jpeg.c       #   TinyJPEG 编码（仅未开 RAW_RGB 时的备用路径）
│  │  └─ serial_link.c       #   USB 串口直传（备用链路，含校验重传）
│  ├─ perception/            # 视觉感知：JPEG → 识图服务 → observation_t（3 帧去抖）
│  ├─ behavior/              # 行为分析：observation 时序 → study_state_t（双模式阈值）
│  ├─ ui/                    # LCD 页面渲染、中文字库、图标
│  ├─ hardware/              # 真实驱动：OV2640(V4L2)、WiFi、按键、音频、ST7789
│  └─ tests/                 # 主机单元测试（UI/行为/感知）
├─ board/contest_board/      # 板级配置
│  └─ configs/hwtest/defconfig   # ← 本作品使用的 openvela 配置
├─ scripts/
│  └─ build_hwtest.sh        # 一键构建（工具链/HAL/兼容补丁/相机补丁全自动）
├─ patches/
│  └─ 0001-nuttx-esp32s3-cam-realign-dma-on-first-vsync.patch
│                            # ← 必需的内核补丁：相机 DMA 帧对齐（见排障第 5 条）
├─ skills/                   # 本项目踩过的坑沉淀成的可复用 skill（详见第八节）
│  ├─ openvela-build-recovery/SKILL.md      # 构建自毁/恢复、Kconfig 默认值、.built 归档
│  ├─ nuttx-camera-frame-debug/SKILL.md     # 画面卷动/颜色错乱的 DMA 相位根因
│  ├─ nuttx-http-client-stability/SKILL.md  # 大 body 上传的连接耗尽与 keep-alive
│  └─ dist/*.skill                          # 打包件，可直接安装使用
├─ tools/
│  ├─ mimo_relay.py          # 中转服务器：识图转发 + 学习报告 + 网页
│  ├─ serial_bridge.py       # 电脑端串口桥接（备用链路，替代 WiFi）
│  └─ generate_ui_cjk_font.py# 中文字库生成脚本
├─ logs/                     # AI Coding 对话日志
├─ 视觉模型README.md          # 视觉模型服务的完整说明（模型/阈值/接口/许可）
└─ README.md
```

## 五、从零搭建（评委复现步骤）

分五步：**① 拉取工程 → ② 编译烧录固件 → ③ 部署视觉模型 → ④ 部署中转服务器 + 内网穿透 → ⑤ 全链路验证**

---

### 步骤 1：拉取 openvela 全量工程

```bash
repo init -u https://github.com/open-vela/contest2026_087_gaiduimingyizhanyongdui \
  -b dev-ai-contest-2026 -m contest2026_087_gaiduimingyizhanyongdui.xml
repo sync -c -j8
```

同步后本仓位于工作区 `contest2026_087_gaiduimingyizhanyongdui/`，
openvela 源码在外层（`nuttx/`、`apps/`、`packages/`、`vendor/`）。

> `app/hello_app/` 会通过 manifest 的 `<linkfile>` 软链到
> `packages/demos/contest2026_087_hello_app`，**无需手动拷贝**。

**⚠️ 交叉编译工具链要单独拉。** manifest 里 `xtensa-esp32s3-elf` 带了
`groups="notdefault,platform-linux"`（见 `openvela.xml`），**上面那条
`repo sync -c -j8` 不会拉它** —— 直接编译会刷屏
`xtensa-esp32s3-elf-gcc: command not found`。补一步：

```bash
git clone --depth=1 \
  https://github.com/openvela-toolchain-external/prebuilts_gcc_linux-x86_64_xtensa-esp32s3-elf \
  prebuilts/gcc/linux-x86_64/xtensa-esp32s3-elf

export PATH=$PWD/prebuilts/gcc/linux-x86_64/xtensa-esp32s3-elf/bin:$PATH
xtensa-esp32s3-elf-gcc --version     # 应输出 12.2.0
```

---

### 步骤 2：编译并烧录设备固件

**环境**：Linux（推荐 Ubuntu 22.04）。**首次构建需要能访问 github.com**（拉 ESP HAL
及其子模块）。

**一条命令**（在 openvela 工作区根目录，即本仓的上一级执行）：

```bash
bash contest2026_087_gaiduimingyizhanyongdui/scripts/build_hwtest.sh
```

脚本会把评委手动复现时最容易踩的四个坑一并处理掉，每步都在输出里注明了原因：

| # | 坑 | 脚本的处理 |
|---|---|---|
| 1 | 交叉工具链不在 PATH | 它在 manifest 里是 `notdefault` 组，`repo sync -c -j8` **不会拉**；脚本检查，缺失时直接给出 clone 命令 |
| 2 | ESP HAL 编译不过 | 自动打两项兼容补丁（见下方排障第 2 条） |
| 3 | **画面卷动 / 颜色错乱** | 自动应用本仓 `patches/` 下的**相机 DMA 对齐补丁**（见排障第 5 条） |
| 4 | 仓库 defconfig 被改 | `build.sh` 结尾会 `savedefconfig` 覆盖回仓库，脚本构建后还原 |

首次约 15–40 分钟（取决于网络）。**之后重跑同一条命令即走增量编译**，不会再重新配置。

**关键配置**（已在 `hwtest/defconfig` 中设置，无需手动改）：

| Kconfig | 值 | 作用 |
|---|---|---|
| `CONFIG_CONTEST2026_087_PERCEPTION_RAW_RGB` | `y` | 设备端**不做 JPEG 编码**，上传原始 RGB565，由服务器转码 |
| `CONFIG_CONTEST2026_087_PERCEPTION_MOCK` | 未启用 | 走真实视觉模型 |
| `CONFIG_CONTEST2026_087_{WIFI,BUTTON,CAMERA,AUDIO}_STUB` | `is not set` | 走真实驱动而非桩 |
| `CONFIG_NET_SEND_BUFSIZE` | `65536` | 单帧约 200KB，需要足够的 TCP 发送缓冲 |
| `CONFIG_IOB_NBUFFERS` | `512` | 配合长连接，避免约 10 帧后耗尽 IOB 池 |

产物：`nuttx/nuttx.bin`

**增量编译**：重跑上面的脚本即可（它检测到 `nuttx/.config` 已存在就只跑 `make`）。
若想手动跑：

```bash
export PATH=$PWD/prebuilts/gcc/linux-x86_64/xtensa-esp32s3-elf/bin:$PATH
make -C nuttx -j2
```

> ⚠️ **首次之后不要再直接跑 `./build.sh`** —— 见排障第 1 条。

**烧录**（需 esptool）：

```bash
esptool -c esp32s3 -p COM6 -b 460800 \
  --before default-reset --after hard-reset \
  write_flash 0x0 nuttx.bin
```

> `-p` 换成你的串口（Windows `COM6`，Linux `/dev/ttyUSB0`）。
> 若报 `invalid header`，确认烧到地址 `0x0` 且带 `--after hard-reset`。

**运行**：

```
nsh> hello_app hwwifi <你的WiFi名> <密码>     # 连 WiFi（识图需要）
nsh> hello_app                                # 启动主程序
```

**按键操作**（BOOT 键）：

| 操作 | 效果 |
|---|---|
| **待机时长按** 1–3 秒 | 进入模式选择（严格/鼓励） |
| **模式选择时长按** | **切换模式**（严格 ⇄ 鼓励） |
| **模式选择时短按** | **确认进入监测** |
| 监测中短按 / 超长按 >3 秒 | 结束本次学习，进入报告页 |
| 报告页短按 | 返回待机 |

> 只有**进入监测状态后**才采集与识图（IDLE/模式选择/报告页不采集）。

#### 构建排障（复现必读）

以下五条是本作品开发中真实踩到并定位过的，评委复现时大概率会遇到。
`scripts/build_hwtest.sh` 已把第 1、2、5 条自动处理，这里记录成因供排查。

**1. `build.sh` 只能用于首次构建，之后一律用 `make -C nuttx -j2`。**
`build.sh` 内部调 `configure.sh -e`，配置一旦变化就执行 `make distclean`；而
`nuttx/arch/xtensa/src/esp32s3/Make.defs` 末尾有
`distclean:: $(call DELDIR,chip/esp-hal-3rdparty)` —— 会把已拉好的 ESP HAL
**连同全部目标文件一起删掉**，下次构建又要重下 444 MB。此外 `build.sh` 结尾会
`make savedefconfig` 并把结果**覆盖回仓库里的 defconfig**。

**2. ESP HAL / mbedtls 兼容补丁是必打项（脚本自动打）。** 本作品需要其中两项：

- **(a) `clk_ctrl_os.c` 的 spinlock 初始化** —— 不打就是硬编译失败。nuttx 自
  `508ece9fb2d` "define spinlock with atomic type" 起把 `spinlock_t` 变成了结构体，
  而 ESP HAL 钉住的 `9fc713a` 比它早 3 个月，仍按标量写 `= 0`：
  `clk_ctrl_os.c:27:41: error: invalid initializer`。改为 nuttx 的 `SP_UNLOCKED`。
- **(b) `apps/crypto/mbedtls/Make.defs` 的 `-I` → `-isystem`** —— ESP-IDF 与 nuttx
  的 `cipher_info_t` 布局不同，改用 `-isystem` 让 ESP-IDF 头文件在编译 esp-hal 源
  文件时优先，避免结构体冲突。

  官方 `packages/ai_agent/fix_esp32s3.sh` 还包含另外两项（禁用 HAL mbedtls 的
  `MBEDTLS_CCM_C`、给 `esp32s3_bringup.c` 挂 `/data` tmpfs），那是给 ai_agent 用的，
  本作品不需要 —— 因此**本仓脚本刻意不引入**，以保证评委编出的固件与本队真机验证
  的完全一致。

**3. 清理 `libapps.a` 时必须连 `.built` 标记一起删。** `apps/Application.mk` 里
「把目标文件塞进归档」这个动作挂在 `.built` 的规则上；`.built` 比 `.o` 新就会跳过归档，
链接期报一串 `undefined reference`（本项目见过 `cJSON_*`、`hello_app_main`、
`iperf2_main`）。注意 `find` 默认**不跟随符号链接**，而 `apps/packages`、`apps/external`
都是软链，必须显式扩到真实目录：

```bash
find -L apps packages external frameworks tests vendor \
     contest2026_087_gaiduimingyizhanyongdui \
     -name ".built" -not -path "*/.git/*" -delete
rm -f apps/libapps.a nuttx/staging/libapps.a
```

**4. ESP HAL 丢失后的快速恢复**（不必全量 clone，约 20 秒）：

```bash
cd nuttx/arch/xtensa/src/esp32s3 && rm -rf esp-hal-3rdparty \
  && mkdir esp-hal-3rdparty && cd $_
git init -q && git remote add origin https://github.com/espressif/esp-hal-3rdparty.git
git fetch -q --depth=1 origin 9fc713a95b1ff150dd0b0647e465d3c624056bb1
git checkout -q FETCH_HEAD
```

之后 `make -C nuttx -j2` 会自动补子模块并应用 mbedtls 补丁。

**5. 相机 DMA 帧对齐补丁是本作品必需的 —— 缺了画面会卷动 / 颜色错乱。**
这是个**内核补丁**，改的是 `nuttx/arch/xtensa/src/esp32s3/esp32s3_cam.c`，不在本
参赛仓内，因此以 patch 形式随仓提供：

```
patches/0001-nuttx-esp32s3-cam-realign-dma-on-first-vsync.patch
```

`scripts/build_hwtest.sh` 会在构建前自动应用（幂等）。成因值得记一笔：

- DMA 通道由 `esp32s3_cam_start_capture()` **异步**启动，而 OV2640 此时在自由运行，
  于是 DMA 从传感器帧的**任意相位**开始写 `fb[]`；`fb[0..P)` 是上一帧的尾部，且
  **P 每帧都不同**。`frame_complete_worker()` 却无条件拷贝 `fb[0..fb_size)`，
  得到的就是**圆周移位 P 个字节**的图。
- P 为**偶数**时：配对准好，表现为**画面水平卷动一段**。
- P 为**奇数**时：RGB565 的 2 字节配对整体错位，R/G/B 位段全乱，表现为
  **偏蓝偏绿的彩色纹样**，而且每帧形态都不同。
- 最棘手的是：移位后的帧**自洽且字节完整**，`bytesused` 与 `V4L2_BUF_FLAG_ERROR`
  都检测不出来 —— 设备端、中继、日志全都看不到任何异常，只能从画面看出来。

修复方式是在第一个 VSYNC 中断里从描述符链头重启 DMA 通道（先清 async FIFO），让
新帧正好落在 `fb[0]`；VSYNC 之后有垂直消隐，重对齐在第一个有效像素到来前就已完成。

> ⚠️ 该补丁**尚未合入上游**（截至提交时）。评委从零构建时脚本会自动打上；若你选择
> 手动构建，则需要自行 `cd nuttx && patch -p1 < ../contest.../patches/0001-*.patch`。

---

### 步骤 3：部署视觉模型服务（本机，建议 GPU）

本作品使用的视觉模型是 **Phone-Use Detection Service**（YOLOv8 + 手部分割），
完整说明见仓库内 `视觉模型README.md`。简述：

```bash
# 需要 Python 3.10+ 与 NVIDIA GPU（CPU 推理会明显变慢）
python -m venv .venv

# Windows:
.venv\Scripts\python -m pip install -r requirements.txt
# Linux/macOS:
.venv/bin/python -m pip install -r requirements.txt

# 下载手部权重（手机权重首次推理自动下载）
.venv/Scripts/python scripts/download_weights.py

# 启动服务（监听 0.0.0.0:8000）
.venv/Scripts/python scripts/run_server.py
```

启动成功输出：`Phone-use detection server on http://0.0.0.0:8000  (LAN accessible)`

**自测**（务必用**局域网 IP** 而非 127.0.0.1，并绕过系统代理）：

```bash
curl --noproxy "*" -X POST http://192.168.x.x:8000/detect -F "file=@photo.jpg"
```

返回示例：

```json
{
  "using_phone": true, "has_person": true, "confidence": 0.87,
  "reason": "hand_grabbing_phone",
  "phone": {"label": "cell phone", "bbox": [218,185,325,329]},
  "hand":  {"label": "hand",       "bbox": [241,243,404,332]},
  "persons": [{"label": "person",  "bbox": [104,12,453,330]}],
  "img_shape": [337, 596]
}
```

**模型说明**：手机检测用 YOLOv8n(COCO)，手部用 YOLO26m-seg-hand
（FreiHAND+HaGRID 训练，AGPL-3.0）。判定逻辑为「手框与手机框 IoU 超阈值 **或**
手中心落在手机框按 `hand_center_margin` 扩展的区域内」。阈值集中在
`app/detector.py` 的 `DetectorConfig`。GPU（RTX 4060）单帧约 **78ms**，冷启动约 2s。

---

### 步骤 4：部署中转服务器 + 内网穿透

设备没有 TLS 栈无法直连 HTTPS；视觉模型跑在你 PC 的局域网内。因此需要：
(a) 一台**有公网 IP 的服务器**跑中转；(b) 用 **frp 内网穿透**把服务器请求打到本机模型。

#### 4.1 服务器：启动中转服务

```bash
# 把本仓的 tools/mimo_relay.py 上传到服务器
scp tools/mimo_relay.py root@<服务器IP>:/root/

# 启动（DETECT_URL 指向 frp 映射出的本机模型端口，见 4.2）
RELAY_TOKEN=focus087relay \
PORT=8060 \
DETECT_URL=http://127.0.0.1:8001/detect \
MIMO_API_KEY=<你的 MiMo API Key，用于生成学习建议> \
nohup python3 /root/mimo_relay.py > /root/relay.log 2>&1 &
```

云服务器安全组需放行 **8060**（设备与网页访问）与 **7000**（frp）。

> `MIMO_API_KEY` 仅用于**学习报告的建议生成**，可不填（会退回本地模板文案），
> 不影响识图功能。

#### 4.2 内网穿透：frp（服务器跑 frps，本机跑 frpc）

**服务器侧**：

```bash
cat > /root/frp/frps.toml <<'EOF'
bindPort = 7000
auth.method = "token"
auth.token = "focus087frp"
proxyBindAddr = "127.0.0.1"   # 映射端口只绑回环，模型服务不暴露公网
EOF

nohup /root/frp/frps -c /root/frp/frps.toml > /root/frps.log 2>&1 &
```

**本机侧**（Windows，`C:\frp\frpc.toml`）：

```toml
loginFailExit = false          # 断线自动重连，不退出
serverAddr = "<服务器IP>"
serverPort = 7000
auth.method = "token"
auth.token = "focus087frp"

[[proxies]]
name = "phone-detect"
type = "tcp"
localIP = "127.0.0.1"
localPort = 8000               # 本机视觉模型服务
remotePort = 8001              # 映射到服务器的 127.0.0.1:8001
```

```powershell
C:\frp\frpc.exe -c C:\frp\frpc.toml
```

**验证隧道**（服务器上执行，应返回 `{"status":"ok",...}`）：

```bash
curl -s -m 8 http://127.0.0.1:8001/health
```

#### 4.3 设备端地址配置

若你的服务器 IP 与默认不同，改这三处后重新编译：

| 文件 | 常量 | 说明 |
|---|---|---|
| `app/hello_app/perception/perception.c` | `MIMO_ENDPOINT_DEFAULT` | 识图端点 `http://<服务器>:8060/v1/chat/completions` |
| `app/hello_app/ui/mimo.c` | `REPORT_API_URL` | 报告上传 `http://<服务器>:8060/report` |
| `app/hello_app/perception/perception.c` | `MIMO_API_KEY_DEFAULT` | 中继令牌（= 服务器的 `RELAY_TOKEN`） |

---

### 步骤 5：验证全链路

| # | 检查项 | 命令/现象 |
|---|---|---|
| 1 | 本机模型 | `curl --noproxy "*" http://127.0.0.1:8000/health` → ok |
| 2 | 隧道 | 服务器 `curl -s http://127.0.0.1:8001/health` → ok |
| 3 | 中继 | 服务器 `ss -tlnp \| grep 8060` |
| 4 | 设备 | `hello_app hwwifi <ssid> <pass>` 后 `hello_app` |

**串口应看到**（RAW_RGB 模式：设备端不产 JPEG，日志里不会出现 "JPEG" 字样，
这正是"编码卸载"生效的直接证据）：

```
[state_machine] MODE_SELECT -> MONITORING
[cam] #1 尝试采集...
[cam] #1 采集OK 153600B -> 预处理...            ← 320×240×2 = 153600B 原始帧
[cam] #1 RAW RGB565 全尺寸 153600B -> 识图...   ← 320×240×2 = 153600B，未做编码
[percep] 识图 HTTP OK, resp=HTTP/1.0 200 OK
[percep] 识图 person=1 phone=1 inhand=1 pitch=0.0 motion=1.00 conf=0.6x
```

**网页端**：

- 实时画面预览：`http://<服务器IP>:8060/preview`
- 学习报告：`http://<服务器IP>:8060/report`（完成一次学习后生成）

## 六、中转服务器接口

`tools/mimo_relay.py` 除转发外还提供网页服务：

| 路由 | 方法 | 说明 |
|---|---|---|
| `/v1/chat/completions` | POST | 设备识图请求（OpenAI 兼容）→ 转发视觉模型 → 转回设备格式 |
| `/report` | POST | 设备上报学习统计 → 调 LLM 生成建议 → 存盘 |
| `/report` | GET | 完整学习报告网页（**严格/鼓励两套建议，按钮切换**） |
| `/report.json` | GET | 报告数据（网页 JS 拉取，含 `advice` / `advice_gentle` / `achievements`） |
| `/preview` | GET | 摄像头实时预览网页 |
| `/preview.jpg` | GET | 最新一帧画面 |

**协议转换由中继完成**（设备发 OpenAI 格式，视觉模型是 multipart 表单且字段名不同），
因此**设备固件无需感知后端模型**，换模型只改中继。

### 成就系统

报告页在教练建议下方展示「本次专注获得成就如下」。成就由中继的
`compute_achievements()` 计算，判据**全部来自设备实际上报的四项统计**
（总时长、有效时长、专注度、分类分心次数），固件无需改动。

| 徽章 | 名称 | 条件 | 等级 |
|---|---|---|---|
| 🎯 | 首战告捷 | 完成一次专注监测 | 铜 |
| ⏳ | 坚毅前行 | 单次学习 ≥ 40 分钟 | 铜 |
| 🪑 | 稳坐如山 | 全程没有离座 | 铜 |
| 📈 | 稳扎稳打 | 专注度 ≥ 70 分 | 银 |
| ⚡ | 高效学习者 | 有效学习占比 ≥ 80% | 银 |
| 📵 | 手机绝缘体 | 全程没有玩手机 | 银 |
| 🌊 | 心流时刻 | 专注度 ≥ 90 分 | 金 |
| 🏅 | 马拉松学霸 | 单次学习 ≥ 60 分钟 | 金 |
| 💎 | 无暇可分 | 整局零分心 | 金 |

`/report.json` 返回**全部**成就（含 `unlocked: false`），网页据此区分
「已获得（亮）」与「待解锁（灰）」——只返回已获得的会让新用户看到空板块。

**演示档位**：`SEED_DEMO_REPORT=1`（默认）时中继会预置一份演示报告，
用 `DEMO_PROFILE` 切换两档：

```bash
python3 tools/mimo_relay.py                      # normal: 45 分钟 / 72 分 → 解锁 4/9
DEMO_PROFILE=hero python3 tools/mimo_relay.py    # hero  : 65 分钟 / 93 分 → 解锁 8/9
```

`hero` 档刻意保留「无暇可分」未解锁，演示时可指着说"还差一个：整局零分心"。

## 七、主机单元测试

无需硬件即可运行：

```bash
cmake -S app/hello_app/tests -B build/focus-aiot-tests
cmake --build build/focus-aiot-tests
ctest --test-dir build/focus-aiot-tests --output-on-failure
```

UI 单测也可独立编译（复用 `lcd.c` 的 `UI_UNIT_TEST` 调试接口）：

```bash
cd app/hello_app
gcc -o /tmp/t_ui tests/test_ui.c ui/lcd.c ui/lcd_icons.c ui/mimo.c \
    ui/ui_cjk_font.c ui/ui_draw.c -DUI_UNIT_TEST -I. -Iapi -Iui -lpthread
/tmp/t_ui
```

## 八、AI Coding 使用说明

本作品全程使用 AI 辅助开发：

- **需求拆解与接口设计**：把"学习监测"拆成 5 个可并行模块，冻结 `api/*.h` 接口，
  5 名成员并行开发互不阻塞。
- **跨模块集成**：由 AI 将各成员提交的实现接入主循环，并解决合并冲突
  （如感知模块换模型时与主线的冲突）。
- **深度调试**：多个硬件级问题由 AI 主导定位，例如：
  - **任务栈溢出踩坏 TLS 导致 `printf` 崩溃** —— 反汇编算出 TinyJPEG 单帧栈帧
    8032 字节，超出当时 8KB 的任务栈（且发现 builtin 注册表缓存导致 STACKSIZE
    改动未生效）；
  - **摄像头第 2 帧起采集失败** —— 读内核 `v4l2_cap.c` 发现 RING 模式判据为
    `vbuf_top != vbuf_next`，未消费容器残留导致 `-ENOMEM`；
  - **大图上传压垮 WiFi** —— 把崩溃栈解析到 `esf_buf_alloc_dynamic` / `up_irq_restore`，
    定位为请求体过大；先以 2× 降采样把 200KB 压到 50KB 绕过，但真机发现识别率
    明显下降（手机是小目标），于是改为**恢复全尺寸 + 消灭连接 churn**：定位到
    `wifi_esp32.c` 每帧 `socket/connect/close` 一次，约 10 帧后耗尽 nuttx 的
    TCP/IOB 池、`connect` 返回 `-20`，据此改为 keep-alive 长连接 + 按
    `Content-Length` 读响应，并调大 `NET_SEND_BUFSIZE` / `IOB_NBUFFERS`；
  - **构建链路"自毁"** —— 构建产物全量消失，逐层追到 `configure.sh -e` 的
    distclean 分支，再定位到 `esp32s3/Make.defs` 的
    `distclean:: $(call DELDIR, chip/esp-hal-3rdparty)`；顺带查明 ESP HAL 钉住的
    版本早于 nuttx 把 `spinlock_t` 改成结构体的那次提交，导致从零编译必然失败；
  - **归档丢成员** —— 链接报一串 `undefined reference`，读 `apps/Application.mk`
    发现「塞进 `libapps.a`」挂在 `.built` 标记的规则上，且 `find` 不跟随
    `apps/packages` 这类符号链接，据此定位到清理姿势错误而非代码问题；
  - **画面卷动 + 颜色错乱** —— 这是个排查链很长的内核 bug。先排除应用层与中继
    （比对两端 RGB565 取色代码，逐位相同，证明不是编码位置的问题），再由
    「卷动量每帧都不同」这一现象反推到相机 DMA 的**异步启动相位**：DMA 在 OV2640
    自由运行时启动，从任意相位开始写缓冲，而回调用无条件整帧拷贝。移位后的帧
    字节完整自洽，`bytesused` 与 `V4L2_BUF_FLAG_ERROR` 都检测不出来，只能从画面
    看出来。修复方式是在第一个 VSYNC 中断里重启 DMA 通道完成重对齐（见排障第 5 条）。
- **文档**：本 README 的搭建步骤与排障说明由 AI 整理。

### 把踩过的坑沉淀成 skill

上面的调试经验如果不固化，下次遇到同类问题仍要重新摸索。因此本项目把三条最贵的
经验抽取成了可复用的 skill，随仓放在 `skills/`：

| skill | 解决的问题 |
|---|---|
| `openvela-build-recovery` | 构建产物凭空消失、ESP HAL 丢失、Kconfig 默认值悄悄改掉配置、链接期一串 `undefined reference`、改了 STACKSIZE 不重编 |
| `nuttx-camera-frame-debug` | 画面卷动 / 颜色错乱（DMA 起始相位）、噪点性质判定、跨端取色比对 |
| `nuttx-http-client-stability` | 大 body 上传若干帧后连接耗尽、keep-alive 的定界陷阱、TCP/IOB 缓冲调参 |

用法：把 `skills/<name>/` 拷到 `~/.claude/skills/` 或工作区的 `.claude/skills/` 即可
被 Claude Code 自动发现；`skills/dist/*.skill` 是打包件，可直接安装。三个 skill 均
通过组委会同款 skill 校验器（`skill-creator/scripts/quick_validate.py`）。

完整对话日志见 `logs/` 目录。

## 九、已知限制

- **AI 建议依赖 LLM Key**：未配置有效 `MIMO_API_KEY` 时，报告建议正文由规则模板生成，
  功能不受影响。
- **视觉模型建议 GPU**：CPU 推理会明显变慢，建议用带 NVIDIA GPU 的机器。
- **链路依赖**：运行时本机的视觉模型服务与 `frpc` 需保持运行，否则识图失败。
- **传输量**：设备上传**全尺寸**原始 RGB565（320×240，base64 后约 200KB/帧，
  约 5s 一帧）。这是刻意的取舍 —— 降到 160×120 能把传输量减到 1/4，但手机这类
  小目标的细节会丢失，实测识别率明显下降。传输压力改由**每帧复用同一条 TCP 长
  连接** + 调大 `CONFIG_NET_SEND_BUFSIZE`(64KB) / `CONFIG_IOB_NBUFFERS`(512) 承担。
  若改回 JPEG 上传（关掉 `PERCEPTION_RAW_RGB`）或进一步增大分辨率，需重新评估这
  两个缓冲值，否则可能重新出现 `connect` 失败（`FOCUS_ERR_NET_DISCONN` = -20）。
- **中继必须同步升级**：设备端靠 HTTP keep-alive 复用连接，中继侧需为
  `protocol_version = "HTTP/1.1"` 且每个响应都带 `Content-Length`（本仓
  `tools/mimo_relay.py` 已满足）。换用 HTTP/1.0 的旧版中继会让设备每帧重连，
  退化回连接耗尽的老问题。
- **依赖一个未合入上游的内核补丁**：相机 DMA 帧对齐（见排障第 5 条）。补丁随本仓
  `patches/` 提供并由构建脚本自动应用，但**它改的是 nuttx 内核**，不在本参赛仓的
  常规改动范围内，上游也尚未合入。
- **暗光下画面噪点明显**：`esp32s3_cam.c` 驱动**只配置 ESP32-S3 的 LCD_CAM 外设**
  （DMA / 时钟 / GPIO），**没有任何 OV2640 传感器寄存器初始化表**，传感器跑的是上电
  默认值。Espressif 官方 esp32-camera 组件会下发一张调好的寄存器表（含降噪开关与
  增益上限），本驱动没有，因此暗光下自动增益拉满，画面颗粒明显。演示时建议保证
  照明；根治需要在驱动里补传感器寄存器表（内核改动，本版未做）。

## 十、许可

- 本仓代码：参赛作品。
- 手部模型 `NightingaleCen/YOLO26m-seg-hand`：AGPL-3.0。
- YOLOv8 / ultralytics：AGPL-3.0。
- TinyJPEG：public domain。
