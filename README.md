<div align="center">
  <img src="assets/brand/musicdisc-logo.svg" width="96" alt="MusicDisc">
  <h1>md-player</h1>
  <p>MusicDisc Player —— 本地实体碟资源统一播放器（macOS / Windows）</p>
</div>

播放**已解密**的 BDMV 文件夹 / BD ISO / UHD 原盘 / VIDEO_TS 文件夹 / DVD ISO /
蓝光音频（Pure Audio）/ SACD ISO。

内核 libmpv，结构解析 libbluray + libdvdread + scarletbook 血统代码，UI Qt 6（Qt Quick）。

> **不支持加密原盘。** 本项目不接入 libaacs / libbdplus / libdvdcss，不实现任何密钥逻辑。
> 检测到加密盘会明确报错。详见 [docs/DECISIONS.md](docs/DECISIONS.md) D-003 / D-012。

License：**GPL-2.0-or-later**

## 文档

| 文档 | 内容 |
|---|---|
| [CLAUDE.md](CLAUDE.md) | 项目宪法：铁律、技术栈、已知深坑 |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | 模块边界、SACD helper 协议、碟片指纹规范 |
| [docs/M1-PLAN.md](docs/M1-PLAN.md) | 当期里程碑（T0–T8） |
| [docs/DECISIONS.md](docs/DECISIONS.md) | 决策记录 |
| [docs/VISION.md](docs/VISION.md) | 产品愿景（非当期承诺） |

## 构建

### 前置

```bash
brew install qt cmake ninja meson pkg-config libbluray ffmpeg libass libplacebo
```

### 播放依赖（必须自编译）

系统包管理器的 mpv **不能直接用**，两个原因：

1. Homebrew 的 mpv 未编入 dvdnav，`dvd://` 不可用（T4 必需）。
2. Homebrew 的 libdvdread 把 libdvdcss 编成硬加载项（`LC_LOAD_DYLIB`），
   会让解密库随进程启动被加载，违反项目铁律。

因此用本脚本把 libdvdread（关闭 libdvdcss）、libdvdnav、libmpv 构建到
`third_party/prefix`。三个依赖都**钉死到具体 commit**，本机与 CI 构建同一份产物：

```bash
./scripts/build-deps.sh
```

首次约 3–5 分钟；之后靠戳记短路，重复执行秒退。脚本结尾会自动校验依赖闭包中
不存在 libdvdcss / libaacs / libbdplus，并打印能力探测结果（`bd://` 与 `dvd://`
均须为 `[OK]`）。

单独跑能力探测：

```bash
./scripts/check-mpv-caps.sh
```

### 编译

```bash
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build
./build/md-player
```

CMake 会自动优先使用 `third_party/prefix`，无需手动设 `PKG_CONFIG_PATH`。

## 开发

### 运行

```bash
./build/md-player                 # 空窗口，拖入文件或文件夹
./build/md-player <路径>          # 直接打开
./build/md-player /path/to/BDMV上级目录       # 蓝光文件夹
./build/md-player /path/to/disc.iso           # 蓝光 ISO（无需先挂载）
./build/md-player /path/to/VIDEO_TS上级目录   # DVD 文件夹（拖 VIDEO_TS 本身也认）
./build/md-player /path/to/dvd.iso            # DVD ISO（无需先挂载）
```

分派顺序是蓝光 → DVD → 普通播放，前两个「不认就交还」，所以普通视频文件照常直通 mpv。

碟类资源打开后自动播主标题（时长最长优先，并列取章节多者），标题与章节都在左侧
**标题·章节面板**里（蓝光与 DVD 共用同一个面板）：列出碟内标题（演唱会碟常按曲目组分多条，故不按时长排序），
主标题带徽标且默认展开章节。按 `T` 或播控条上的 `☰` 开关面板——**打开碟不会自动弹面板**，
默认直接播主标题。面板顶部的「隐藏 10 秒以下条目」默认开启，用来滤掉 UHD 原盘的占位
playlist；它只影响显示，碟内完整结构始终保留，主标题与正在播放的那条永不隐藏。
碟内未提供章节名时降级为「第 N 章」（DVD 的 IFO 结构里根本没有章节名这一项，一律显示编号）。

**加密盘**：蓝光看 libbluray 的 `aacs_detected && !aacs_handled`；DVD 由应用层自检 VOB 扇区的
PES 扰码位（D-022）。两者都给统一文案「不支持加密原盘，请使用已解密资源」，不做任何绕过尝试。
本机即便装了 libdvdcss 也不受影响——自编译的 libdvdread 关掉了该选项，进程运行期不加载它。

快捷键：

| 键 | 作用 |
|---|---|
| `空格` | 暂停 / 继续 |
| `←` / `→` | ±5 秒 |
| `Ctrl+←` / `Ctrl+→` | ±60 秒 |
| `↑` / `↓` | 音量 ±5 |
| `m` | 静音 |
| `s` / `Shift+S` | 截图（纯画面 / 含字幕） |
| `t` | 标题·章节面板（碟类资源） |

进度条拖动时贴关键帧即时出画，松手落精确点（CLAUDE.md 深坑 #2）；章节以刻度显示在进度条上。
截图落 `~/Pictures/md-player/`。断点续播记录在
`~/Library/Application Support/MusicDisc/md-player/resume.json`。

### 测试素材

真实碟片与样片**不入库**（见 CLAUDE.md 深坑 #6）。开发和验收时用环境变量
`MD_TEST_MEDIA` 指向本地素材目录：

```bash
export MD_TEST_MEDIA=/path/to/your/test-media
./build/md-player "$MD_TEST_MEDIA/sample.m2ts"
```

该目录应含 4K 高码率 m2ts 与 mkv（T1 / T2 验收用），碟类样本（BDMV / VIDEO_TS /
BD·DVD·SACD ISO）随 T3 起逐步补充。`tests/fixtures/` 只放自造的最小结构骨架，
**绝不放任何版权内容**。

错误路径不依赖真实碟片——用自造骨架覆盖（详见 `tests/fixtures/README.md`）：

```bash
python3 tests/fixtures/make_bd_skeleton.py tests/fixtures/generated/bd-aacs --aacs
MD_LOG_UI=1 ./build/md-player tests/fixtures/generated/bd-aacs

python3 tests/fixtures/make_dvd_skeleton.py tests/fixtures/generated/dvd-css --css
MD_LOG_UI=1 ./build/md-player tests/fixtures/generated/dvd-css
# 期望: [TOAST] 不支持加密原盘，请使用已解密资源
```

### 诊断日志

| 环境变量 | 作用 |
|---|---|
| `MD_LOG_PROGRESS=1` | 按秒打印播放进度与暂停态变化 |
| `MD_LOG_PROPS=1` | 打印全部 mpv 属性变更事件 |
| `MD_LOG_SEEK=1` | 打印每次 seek 的标志、目标与「命令→画面到位」耗时 |
| `MD_LOG_TRACKS=1` | 打印章节与音轨/字幕轨枚举结果 |
| `MD_LOG_UI=1` | 把提示条（toast）文案打到 stdout，用于无截屏权限时校验用户实际看到的文案 |
| `MD_MPV_CONF=<路径>` | 覆盖 `configs/mpv-baseline.conf` |

### 代码格式

提交前必须格式化（LLVM 基础，120 列）：

```bash
clang-format -i $(git ls-files '*.cpp' '*.h')
```
