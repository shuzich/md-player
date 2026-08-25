#!/usr/bin/env bash
# T0 必跑：探测系统 mpv/libmpv 是否编入本项目必需的能力。
# 输出必须原样贴进任务汇报。
set -u

echo "== mpv 版本 =="
# 注意：不能写成 `mpv --version | head -n2 || ...`，管道的退出码取自 head，
# mpv 缺失时该分支永远不触发（会静默输出空版本号）。必须先探测可执行文件本身。
if ! command -v mpv >/dev/null 2>&1; then
  echo "未找到 mpv，请先 brew install mpv"
  MPV_MISSING=1
else
  mpv --version 2>/dev/null | head -n 2
  MPV_MISSING=0
fi

echo
echo "== 协议支持 =="
PROTOS=$(mpv --list-protocols 2>/dev/null)
echo "$PROTOS" | tr ' ' '\n' | grep -E '^(bd|br|bluray|dvd)' || true

echo
if echo "$PROTOS" | grep -qw bd; then
  echo "[OK]   bd://  可用（libbluray 已编入）"
else
  echo "[缺失] bd://  —— 需自编译 libmpv，见下方注释"
fi
if echo "$PROTOS" | grep -qw dvd; then
  echo "[OK]   dvd:// 可用（dvdnav 已编入）"
else
  echo "[缺失] dvd:// —— 需自编译 libmpv，见下方注释"
fi

echo
echo "== pkg-config 侧 =="
for lib in mpv libbluray dvdread; do
  printf '%-10s ' "$lib:"
  pkg-config --modversion "$lib" 2>/dev/null || echo "未找到"
done

# ---------------------------------------------------------------------------
# 缺失时的自编译方案（在仓库根目录执行）：
#   brew install meson ninja libbluray libdvdread libdvdnav
#   git clone https://github.com/mpv-player/mpv third_party/mpv-src
#   cd third_party/mpv-src
#   meson setup build -Dlibmpv=true -Dlibbluray=enabled -Ddvdnav=enabled \
#         --prefix="$PWD/../prefix"
#   meson compile -C build && meson install -C build
# CMake 侧：
#   export PKG_CONFIG_PATH="$PWD/third_party/prefix/lib/pkgconfig:$PKG_CONFIG_PATH"
# 并把选型结论写入 docs/DECISIONS.md 的 D-009。
# ---------------------------------------------------------------------------
