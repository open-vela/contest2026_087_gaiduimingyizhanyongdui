#!/usr/bin/env bash
# ============================================================================
# FOCUS AIoT - ESP32-S3-EYE 硬件验证固件构建脚本
#
# 用法:
#   cd <openvela 工作区根目录>   # 即本仓的上一级, 如 /home/ubuntu/openvela
#   bash contest2026_087_gaiduimingyizhanyongdui/scripts/build_hwtest.sh
#
# 产物:
#   nuttx/nuttx.bin   <- 烧录到 ESP32-S3-EYE 的固件 (烧到 0x0)
#   nuttx/nuttx       <- ELF (调试用)
#
# 说明:
#   1. 本脚本封装了官方 `./build.sh <board-config-path>` 流程, config 位于
#      本仓 board/contest_board/configs/hwtest (通过 manifest linkfile 挂载)。
#   2. 已按官方赛道指引修正 build 环境: 工具链 PATH / esptool>=4.8 / ESP HAL 兼容补丁。
#   3. 网络慢时 ESP HAL 子模块可能 TLS 抖动, 脚本会自动重试。
#   4. 建议把 HAL 缓存到 <openvela>/nxtmpdir/esp-hal-3rdparty, 可避免每次
#      distclean 后重新下载 500MB+ (见下方 CACHE_HAL 开关)。
# ============================================================================
set -uo pipefail

# ---------- 可调参数 ----------
ROOTDIR="$(cd "$(dirname "$0")/../../.." && pwd)"      # openvela 根目录
CONFIG_PATH="contest2026_087_gaiduimingyizhanyongdui/board/contest_board/configs/hwtest"
PROXY="${http_proxy:-}"                                 # 继承当前代理(如需)

CACHE_HAL=1        # 1=使用 nxtmpdir 缓存 HAL(推荐), 0=每次重新克隆
JOBS="${JOBS:-$(nproc)}"

# ---------- 环境 ----------
export PATH="$HOME/.local/bin:$ROOTDIR/prebuilts/gcc/linux-x86_64/xtensa-esp32s3-elf/bin:$PATH"
export CCACHE_DISABLE=1
if [ -n "$PROXY" ]; then
  export http_proxy="$PROXY" https_proxy="$PROXY"
fi

echo "[build] openvela root : $ROOTDIR"
echo "[build] config path   : $CONFIG_PATH"
echo "[build] jobs          : $JOBS"
echo "[build] cache hal     : $CACHE_HAL (1=启用)"

# ---------- 可选: 预置 HAL 缓存 (省去每次 distclean 后重下 500MB) ----------
HAL_SRC="$ROOTDIR/nuttx/arch/xtensa/src/chip/esp-hal-3rdparty"
HAL_CACHE="$ROOTDIR/nxtmpdir/esp-hal-3rdparty"

if [ "$CACHE_HAL" = "1" ]; then
  mkdir -p "$ROOTDIR/nxtmpdir"
  touch "$ROOTDIR/nxtmpdir/.marker"     # 令 chip/Make.defs 的 STORAGETMP=y
  export USE_NXTMPDIR_ESP_REPO_DIRECTLY=y

  if [ ! -d "$HAL_CACHE/.git" ]; then
    echo "[build] 预置 HAL 缓存: 从当前树复制到 nxtmpdir (仅首次较慢)..."
    if [ -d "$HAL_SRC/.git" ]; then
      cp -r "$HAL_SRC" "$HAL_CACHE"
    else
      echo "[build] 警告: 当前树没有 HAL, 首次构建会重新克隆 (网络慢时需耐心/重试)"
    fi
  fi
fi

# ---------- 构建 ----------
echo "[build] 开始构建: ./build.sh $CONFIG_PATH -j$JOBS"
if ! ./build.sh "$CONFIG_PATH" -j"$JOBS"; then
  echo "[build] 首次构建常因 ESP HAL 子模块 TLS 抖动失败, 自动重试一次..."
  sleep 5
  ./build.sh "$CONFIG_PATH" -j"$JOBS"
fi

# ---------- 检查产物 ----------
BIN="$ROOTDIR/nuttx/nuttx.bin"
if [ -f "$BIN" ]; then
  echo ""
  echo "[build] ✅ 构建成功!"
  echo "  固件: $BIN"
  echo "  大小: $(du -h "$BIN" | cut -f1)"
  echo "  烧录: esptool.py -c esp32s3 -p <串口> -b 460800 --before default_reset --after hard_reset write_flash 0x0 $BIN"
  echo ""
  echo "[build] 提示: 若遇到 ESP HAL 编译错误, 请先运行官方 fix 脚本对应的两个补丁:"
  echo "  (1) clk_ctrl_os.c / modem_clock.c: LOCK_INITIALIZER_UNLOCKED 0 -> SP_UNLOCKED"
  echo "  (2) mbedtls_config.h: 禁用 MBEDTLS_CCM_C"
  echo "  详见 packages/ai_agent/fix_esp32s3.sh (Fix 2/3)。"
else
  echo "[build] ❌ nuttx.bin 未生成, 请检查上方日志。"
  exit 1
fi
