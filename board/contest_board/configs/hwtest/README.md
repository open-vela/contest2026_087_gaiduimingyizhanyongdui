# hwtest 配置 - 设备硬件验证

本配置用于**烧录后验证 ESP32-S3-EYE 硬件可用性**，内置 `hello_app` 硬件验证测试程序。
基于官方 `vendor/espressif/boards/esp32s3/esp32s3-eye/configs/openvela` 裁剪：
移除 LVGL / BLE（验证镜像不需要，显著缩短编译），保留全部外设驱动。

## 构建

```bash
cd <openvela 根目录>          # 如 /home/ubuntu/openvela
bash contest2026_087_gaiduimingyizhanyongdui/scripts/build_hwtest.sh
```

产物：`nuttx/nuttx.bin`（烧录固件，烧到 **0x0**）。

> 环境要求（见 `scripts/build_hwtest.sh`）：
> - 工具链 `prebuilts/gcc/linux-x86_64/xtensa-esp32s3-elf`（缺失时 `git clone
>   https://github.com/openvela-toolchain-external/prebuilts_gcc_linux-x86_64_xtensa-esp32s3-elf`）
> - `esptool >= 4.8`（`pip install -i https://pypi.tuna.tsinghua.edu.cn/simple esptool==4.8.1`）
> - 首次构建需从 GitHub 拉取 ESP HAL 子模块，网络慢时请重试或使用 `nxtmpdir` 缓存

## 烧录

```bash
# 设备 USB 连接电脑后
esptool.py -c esp32s3 -p /dev/ttyACM0 -b 460800 \
  --before default_reset --after hard_reset \
  write_flash 0x0 nuttx/nuttx.bin
```

Windows 用户串口号通常为 `COMx`；也可用 ESP-IDF 的 Flash Download Tool。

## 串口观察

USB 连接后打开串口终端（如 `minicom -D /dev/ttyACM0 -b 115200` 或 PuTTY），
应看到 NSH 提示符：

```
nsh>
```

## 运行硬件测试

在 `nsh>` 下输入：

```bash
hello_app          # 运行全部测试 (LCD → LED → 按键 → 摄像头 → 加速度计 → SD)
hello_app lcd      # 单项: lcd / led / button / camera / accel / sd
```

每个测试打印 `[PASS]` / `[FAIL]`，最后汇总通过/失败数量。

## 测试项与预期

| 测试 | 预期现象 | 实测(2026-07-31) |
|---|---|---|
| LCD | 屏幕依次显示 红/绿/蓝/白/黑 五条色带 | ✅ PASS (240x240 RGB565) |
| LED | 板载电源 LED 闪烁 5 次后保持点亮 | ✅ PASS |
| 按键 | 串口打印按键事件（请在 15s 内按 BOOT 键数次） | ✅ PASS |
| 摄像头 | 采集 RGB565 帧，打印每帧字节数 | ✅ PASS (至少 1 帧即验证链路) |
| 加速度计 | 打印 5 组 x/y/z 加速度读数 | ⚠️ 传感器存在(CHIP_ID有效)但数据全 0, 待排查驱动时序 |
| SD | 显示 SD 设备节点是否存在 | ⚠️ 需插入 microSD 卡后才注册节点 |

### 已知特性 / 待办

- **摄像头为单帧模式**：ESP32-S3-EYE 的 esp32s3_cam 驱动每收到一帧即停 DMA，
  连续采集需上层感知模块按帧重新 arm（或走 STILL_CAPTURE/JPEG 路径）。
  硬件测试判定：**采到 1 帧 = 摄像头链路正常**。
- **加速度计(QMA7981)**：CHIP_ID 读取正常（I2C 通信 OK），但加速度数据寄存器
  读回全 0，疑似软复位/配置时序问题。非 FOCUS 核心传感器，暂缓，建议硬件负责人
  对照数据手册调 `drivers/sensors/qma7981.c` 的 `qma7981_chip_init()`。
- **SD**：需插入 microSD 卡再测。

WiFi 联网请在 NSH 用 `wapi` 命令（本镜像已内置）：
```bash
wapi devifname              # 查看无线网卡名 (wlan0)
wapi scan wlan0             # 扫描
wapi connect wlan0 <ssid> <password>   # 连接 (WPA2)
```
