#!/usr/bin/env bash
# 自编译 md-player 的播放依赖到 third_party/prefix（决策见 docs/DECISIONS.md D-009 / D-011）。
#
# 为什么不能直接用 Homebrew 的 mpv：
#   1. Homebrew mpv 未编入 dvdnav，`dvd://` 不可用（T4 必需）。
#   2. Homebrew libdvdread 把 libdvdcss 编成硬加载项，违反 CLAUDE.md 铁律 #2。
# 本脚本因此重建 libdvdread（关闭 libdvdcss）→ libdvdnav → libmpv。
#
# 前置：brew install qt cmake ninja meson pkg-config libbluray ffmpeg libass libplacebo
# 用法：./scripts/build-deps.sh   （幂等，可重复执行）
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX="$ROOT/third_party/prefix"
SRC="$ROOT/third_party"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"

clone_or_update() {
  local url="$1" dir="$2"
  if [ -d "$SRC/$dir/.git" ]; then
    echo "== 更新 $dir =="
    git -C "$SRC/$dir" fetch --depth 1 origin HEAD && git -C "$SRC/$dir" reset --hard FETCH_HEAD
  else
    echo "== 克隆 $dir =="
    rm -rf "${SRC:?}/$dir"
    git clone --depth 1 "$url" "$SRC/$dir"
  fi
}

echo "########## libdvdread（禁用 libdvdcss，铁律 #2）##########"
clone_or_update https://code.videolan.org/videolan/libdvdread.git libdvdread-src
rm -rf "$SRC/libdvdread-src/build"
meson setup "$SRC/libdvdread-src/build" "$SRC/libdvdread-src" \
    -Dlibdvdcss=disabled --prefix="$PREFIX" --buildtype=release
meson install -C "$SRC/libdvdread-src/build"

echo "########## libdvdnav ##########"
clone_or_update https://code.videolan.org/videolan/libdvdnav.git libdvdnav-src
rm -rf "$SRC/libdvdnav-src/build"
meson setup "$SRC/libdvdnav-src/build" "$SRC/libdvdnav-src" \
    --prefix="$PREFIX" --buildtype=release
meson install -C "$SRC/libdvdnav-src/build"

echo "########## libmpv（libbluray + dvdnav）##########"
clone_or_update https://github.com/mpv-player/mpv mpv-src
rm -rf "$SRC/mpv-src/build"
meson setup "$SRC/mpv-src/build" "$SRC/mpv-src" \
    -Dlibmpv=true -Dcplayer=true -Dlibbluray=enabled -Ddvdnav=enabled \
    --prefix="$PREFIX" --buildtype=release
meson compile -C "$SRC/mpv-src/build"
meson install -C "$SRC/mpv-src/build"

echo
echo "########## 校验：依赖闭包中不得出现解密库（铁律 #2）##########"
if otool -L "$PREFIX/lib/libmpv.2.dylib" 2>/dev/null | grep -qiE 'dvdcss|aacs|bdplus'; then
  echo "!!! 失败：libmpv 依赖中出现解密库"; exit 1
fi
echo "✓ 无 libdvdcss / libaacs / libbdplus"

echo
echo "########## 能力探测 ##########"
PATH="$PREFIX/bin:$PATH" "$ROOT/scripts/check-mpv-caps.sh"

echo
echo "完成。CMake 会自动优先使用 $PREFIX（见 CMakeLists.txt）。"
