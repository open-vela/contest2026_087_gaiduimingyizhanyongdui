# FOCUS AIoT - UI 实时摄像头预览区域（开发需求）

> 负责人：郭黄亦昕 | 提出：万思源 | 日期：2026-08-27
> 目标：运行 `hello_app` 后，LCD 上**单独划出一块区域实时显示当前摄像头采集到的画面**，
> 方便用户调整设备的摆放角度/位置。

---

## 一、需求

1. 屏幕上有一个**独立的预览区域**，显示当前摄像头拍摄的画面。
2. 用户在监测开始后能看着这块区域调整设备摆放（对准学习桌面/人物）。
3. 预览画面**逐帧更新**（目前采集节奏是每 5s 一帧；如需要可加快，见下文）。
4. 不挤占现有状态/统计/模式等文字信息的排版。

## 二、现状

- 摄像头采集：`hardware/camera_ov2640.c` 的 `camera_capture_frame()`，
  输出 **RGB565 320×240**（153600 字节），存在主循环的 `frame` 缓冲。
- 主循环（`hello_app_main.c`）每 5s：采集 → `rgb565_to_jpeg` → MiMo 识图。
- LCD：ST7789，分辨率 **320×240**（`CONFIG_LCD_ST7789_YRES=240`，竖屏），
  通过 `/dev/fb0` framebuffer 驱动（`hardware/lcd_st7789.c`）。
- UI 层：`ui/lcd.c` 提供 `lcd_show_status / lcd_show_report / lcd_show_message`，
  用 `ui/lcd_icons.c + ui/ui_draw.c + ui/ui_cjk_font.c` 渲染文字/图标。

## 三、设计建议

### 3.1 接口（新增到 `api/lcd.h` + `ui/lcd.c`）

建议新增一个预览绘制接口，**由主循环每帧调用**，UI 层负责把 RGB565 帧
缩放绘制到 LCD 指定区域：

```c
/* 显示一帧摄像头预览 (RGB565)。
 * rgb565: 320x240 RGB565 帧 (来自 camera_capture_frame 的 frame 缓冲)
 * 区域: 由 UI 层内部决定 (如屏幕右侧竖条 / 顶部横条)
 */
int lcd_show_preview(const uint8_t *rgb565, int src_w, int src_h);
```

主循环侧（hello_app_main.c）在 `DEVICE_MONITORING` 采集到帧后调用：

```c
if (st == DEVICE_MONITORING && camera_capture_frame(frame, &fsize) == FOCUS_OK) {
    lcd_show_preview(frame, 320, 240);   // 先上屏预览
    rgb565_to_jpeg(...);                 // 再转 JPEG 走识图
    perception_process(...);
}
```

### 3.2 预览区域布局

- 屏幕 320×240。建议预留预览区（如右上角 160×120 或顶部整条 320×80），
  其余区域保持现有状态文字/统计排版。
- 具体布局由你定，但要在文档/PR 里附一张布局示意（可用 ASCII 或画图）。

### 3.3 缩放绘制

- 源 320×240 RGB565 → 目标预览区（等比缩放）。每 2×2 / 4×4 像素取 1
  即可做 1/2 / 1/4 缩小，无需滤波（实时预览够用）。
- 像素格式：源是 RGB565，LCD framebuffer 也是 RGB565（若 fb 是 RGB565），
  可直接按行拷贝 + 横向抽点。
- 若 framebuffer 格式不同（需确认 `/dev/fb0` 的 `FBIOGET_FSCREENINFO`），
  做一次像素格式转换。

### 3.4 刷新时机

- 每采到一帧就刷一次（当前 5s/帧）。若觉得预览太卡，可把采集节奏加快
  （如 1s/帧）但注意 MiMo 识图每帧阻塞数秒——**预览绘制必须在识图
  之前完成**（3.1 的顺序），且识图期间 UI 其他刷新不被阻塞。
- 状态机修正后（见 `docs/状态机采集时序_开发需求.md`），预览只在
  `DEVICE_MONITORING` 出现。

### 3.5 文件改动范围

| 文件 | 改动 |
|---|---|
| `app/hello_app/api/lcd.h` | 新增 `lcd_show_preview()` 声明 |
| `app/hello_app/ui/lcd.c` | 实现预览区域绘制（缩放 + 上屏） |
| `app/hello_app/ui/ui_draw.c`（可选） | 复用/新增底层像素绘制辅助 |
| `app/hello_app/hello_app_main.c` | 采集到帧后先调 `lcd_show_preview()`（万思源配合） |

## 四、验收标准

1. `hello_app` 进入监测后，屏幕出现独立预览区，显示当前画面。
2. 转动/移动设备，预览区画面跟着变（有延迟可接受，但方向/色彩大致正确）。
3. 预览区不遮挡现有状态文字/统计数字。
4. 不采集时（IDLE/MODE_SELECT/REPORT）预览区不刷新或显示占位。

## 五、开发方式

- 分支从 `openvela/dev-ai-contest-2026` 切出，命名如 `feat/ui-camera-preview`。
- 提交 → push 到 fork（Skadi-QL）→ 万思源用 GitHub API/网页建 PR。
- 需要核对 LCD framebuffer 像素格式与 `/dev/fb0` 接口时可问万思源或查
  `hardware/lcd_st7789.c`。
- 预览帧数据来源：主循环 `frame`（RGB565 320×240），由万思源在
  `hello_app_main.c` 采集后传入。

---

## 附：环境速查

- 构建：`cd /home/ubuntu/openvela && export PATH=.../xtensa-esp32s3-elf/bin:$PATH && make -C nuttx -j2`
- 烧录：`esptool -c esp32s3 -p COM6 -b 460800 --before default-reset --after hard-reset write_flash 0x0 nuttx.bin`
- 代理：`export http_proxy=http://192.168.253.1:7892 https_proxy=http://192.168.253.1:7892`
- PR base 分支：`dev-ai-contest-2026`
