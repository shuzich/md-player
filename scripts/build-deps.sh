#!/usr/bin/env bash
# 自编译 md-player 的播放依赖到 third_party/prefix。
# 决策见 docs/DECISIONS.md：D-009（自编译 libmpv）、D-011 / D-012（不链接 libdvdcss）。
#
# 为什么不能直接用发行版的 mpv：
#   1. Homebrew mpv 未编入 dvdnav，`dvd://` 不可用（T4 必需）。
#   2. Homebrew libdvdread 把 libdvdcss 编成硬加载项，违反 CLAUDE.md 铁律 #2。
#
# 版本策略：三个依赖全部**钉死到具体 commit**，禁止追 master。
# 本机与 CI 必须构建出同一份产物；升级依赖 = 改本文件的 SHA + 重跑验证 + 记 DECISIONS。
#
# 前置：brew install qt cmake ninja meson pkg-config libbluray ffmpeg libass libplacebo
# 用法：./scripts/build-deps.sh          幂等；prefix 已是目标版本时直接跳过
#       ./scripts/build-deps.sh --force  忽略戳记强制重建
set -euo pipefail

# ---------------------------------------------------------------------------
# 钉死的依赖版本（升级时同时更新 SHA 与注释里的日期）
# ---------------------------------------------------------------------------
MPV_URL=https://github.com/mpv-player/mpv
MPV_SHA=654e9382c0bbccb09ccac348f792e9d378e9c7a9          # 2026-08-23，T0 验证通过

DVDREAD_URL=https://code.videolan.org/videolan/libdvdread.git
DVDREAD_SHA=0f30df53125b564ef45903714403ce0a1e6c5e9e      # T0 验证通过

DVDNAV_URL=https://code.videolan.org/videolan/libdvdnav.git
DVDNAV_SHA=8147ccd35e5aae4afdd21171cf7b6b4d8f179d28       # T0 验证通过

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX="$ROOT/third_party/prefix"
SRC="$ROOT/third_party"
STAMP="$PREFIX/.md-player-deps-stamp"
STAMP_WANT="mpv=$MPV_SHA dvdread=$DVDREAD_SHA dvdnav=$DVDNAV_SHA"

FORCE=0
[ "${1:-}" = "--force" ] && FORCE=1

export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"

# 缓存命中判定：戳记一致且关键产物在位 → 整段跳过（CI 缓存复用的关键路径）
if [ "$FORCE" -eq 0 ] && [ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$STAMP_WANT" ] \
   && [ -f "$PREFIX/lib/libmpv.dylib" -o -f "$PREFIX/lib/libmpv.so" ]; then
  echo "== 依赖已是钉定版本，跳过构建 =="
  echo "   $STAMP_WANT"
  exit 0
fi

# 把 $2 目录取到 $1 仓库的 $3 提交（浅取优先，服务端不允许时回退全量）
fetch_pinned() {
  local url="$1" dir="$2" sha="$3" path="$SRC/$2"
  if [ "$(git -C "$path" rev-parse HEAD 2>/dev/null)" = "$sha" ]; then
    echo "== $dir 已在 ${sha:0:9}，跳过拉取 =="
    return
  fi
  echo "== 拉取 $dir @ ${sha:0:9} =="
  rm -rf "$path"; mkdir -p "$path"
  git -C "$path" init -q
  git -C "$path" remote add origin "$url"
  git -C "$path" fetch -q --depth 1 origin "$sha" 2>/dev/null \
    || { echo "   （服务端不支持按 SHA 浅取，回退全量）"; git -C "$path" fetch -q origin; }
  git -C "$path" checkout -q "$sha"
  local got; got="$(git -C "$path" rev-parse HEAD)"
  [ "$got" = "$sha" ] || { echo "!!! $dir 版本不符：期望 $sha 实得 $got"; exit 1; }
}

build_meson() {
  local dir="$1"; shift
  local path="$SRC/$dir"
  rm -rf "$path/build"
  meson setup "$path/build" "$path" --prefix="$PREFIX" --buildtype=release "$@"
  meson compile -C "$path/build"
  meson install -C "$path/build"
}

echo "########## libdvdread（-Dlibdvdcss=disabled，铁律 #2 / D-012）##########"
fetch_pinned "$DVDREAD_URL" libdvdread-src "$DVDREAD_SHA"
build_meson libdvdread-src -Dlibdvdcss=disabled

echo "########## libdvdnav ##########"
fetch_pinned "$DVDNAV_URL" libdvdnav-src "$DVDNAV_SHA"
build_meson libdvdnav-src

echo "########## libmpv（libbluray + dvdnav）##########"
fetch_pinned "$MPV_URL" mpv-src "$MPV_SHA"
build_meson mpv-src -Dlibmpv=true -Dcplayer=true -Dlibbluray=enabled -Ddvdnav=enabled

echo
echo "########## 校验：依赖闭包中不得出现解密库（铁律 #2 / D-012）##########"
# 注意 `|| true`：两个 glob 必有一个匹配不上，ls 会返回 1，set -e 会就地掐断脚本。
LIBMPV="$(ls "$PREFIX"/lib/libmpv.*.dylib "$PREFIX"/lib/libmpv.so.* 2>/dev/null | head -1 || true)"
[ -n "$LIBMPV" ] || { echo "!!! 未找到 libmpv 产物"; exit 1; }
if command -v otool >/dev/null 2>&1; then
  DEPS="$(otool -L "$LIBMPV")"
else
  DEPS="$(readelf -d "$LIBMPV" 2>/dev/null || true)"
fi
if echo "$DEPS" | grep -qiE 'dvdcss|aacs|bdplus'; then
  echo "!!! 失败：libmpv 依赖中出现解密库"; echo "$DEPS"; exit 1
fi
echo "✓ 无 libdvdcss / libaacs / libbdplus"

echo "$STAMP_WANT" > "$STAMP"

echo
echo "########## 能力探测 ##########"
PATH="$PREFIX/bin:$PATH" "$ROOT/scripts/check-mpv-caps.sh"

echo
echo "完成。CMake 会自动优先使用 ${PREFIX}（见 CMakeLists.txt）。"
