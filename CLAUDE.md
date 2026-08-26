# md-player — 项目宪法（Claude Code 必读）

MusicDisc Player：本地实体碟资源统一播放器（macOS / Windows）。
播放**已解密**的 BDMV 文件夹 / BD ISO / UHD 原盘 / VIDEO_TS 文件夹 / DVD ISO / 蓝光音频（Pure Audio）/ SACD ISO。
内核 libmpv；结构解析 libbluray + libdvdread + scarletbook 血统代码；UI Qt 6（Qt Quick）。
开源项目，License：**GPL-2.0-or-later**。

## 铁律（任何任务不得违反）

1. **能用成熟组件绝不自研。** 解码/渲染/seek/截图/音频输出 = libmpv；蓝光结构 = libbluray；DVD 结构 = libdvdread；SACD = vendored scarletbook + dst。自研只允许出现在：应用壳、资源路由、虚拟菜单、进程/协议胶水层。
2. **绝不引入解密。** 不接入 libaacs / libbdplus / libdvdcss，不实现任何密钥逻辑。检测到加密盘 → 界面明确提示"不支持加密原盘，请使用已解密资源"，文案统一放 `src/app/strings.h`。
3. **产品决策不擅断。** 涉及交互方案、范围增减、视觉风格的选择：停下，在汇报末尾列【待决策】清单等确认。技术实现方案凡是本文档与 docs/ 已定的，直接执行，不要反复请示。
   **控制器最新指令与本文档冲突时，以控制器指令为准；长耗时（>15 分钟）、安装系统级依赖、改动 git 历史或远端、任何删除操作，一律先报后动。**
4. **仓库始终干净。** 每个任务：feature 分支 → 完成 → 所有文件要么已跟踪、要么已入 .gitignore → conventional commit（feat: / fix: / chore: / docs:）→ 汇报。严禁遗留未跟踪文件，严禁堆积半成品代码。
5. **双平台意识。** 开发与验收在 macOS，但代码任何时刻保持 Windows 可编译的写法：路径一律 `std::filesystem` / Qt 抽象；平台特定代码集中到 `src/platform/`；`#ifdef` 最小化。

## 技术栈与版本

- C++20 · CMake ≥ 3.26 · Ninja · Qt 6.7+（Qt Quick / QML）
- libmpv（render API，OpenGL 模式）
- libbluray ≥ 1.4（1.5 的章节名 / HDR 元数据能力做**运行时探测**，不硬性依赖）
- libdvdread
- helper：C（vendored scarletbook + dst 解码，来源 sacd-ripper 血统，保留 GPL 版权头），独立进程
- 代码格式：clang-format（LLVM 基础，120 列），提交前必须格式化

## 目录结构

```
md-player/
├── CLAUDE.md                  # 本文件
├── docs/                      # ARCHITECTURE / M1-PLAN / DECISIONS
├── configs/mpv-baseline.conf  # mpv 基准配置（T1 接入）
├── scripts/                   # check-mpv-caps.sh 等
├── src/
│   ├── app/                   # 窗口、播放列表、设置、快捷键、strings.h
│   ├── core/                  # PlayerController（唯一持有 mpv handle）
│   ├── media/
│   │   ├── router/            # 四类资源识别与分派
│   │   ├── bluray/            # libbluray 封装
│   │   ├── dvd/               # libdvdread 封装
│   │   └── sacd/              # helper 客户端 + mpv stream_cb
│   ├── platform/              # 平台特定代码（最小化）
│   └── ui/                    # QML
├── helper/sacd-helper/        # 独立进程（C，GPL vendored 代码在此）
├── third_party/               # 自编译产物 prefix / vendored 源码
└── tests/fixtures/            # 只放自造最小结构骨架，禁止版权内容
```

## 构建

```bash
brew install qt cmake ninja mpv libbluray libdvdread pkg-config meson
cmake -S . -B build -G Ninja
cmake --build build
./build/md-player
```

## 当前里程碑

执行 `docs/M1-PLAN.md`，从 T0 开始按序进行。**每完成一个任务必须停下汇报**（按 M1-PLAN 末尾的汇报模板），等确认后再进入下一个任务。禁止连续执行多个任务。

## 已知深坑与既定解法（不要重新踩）

1. **Qt Quick + libmpv 嵌入**：必须在创建任何窗口前调用 `QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL)`（macOS 默认 Metal，会导致 mpv render API 无法工作）。渲染用 `QQuickFramebufferObject` + `mpv_render_context`（OPENGL 类型，get_proc_address 走 `QOpenGLContext`）。参考 mpv-examples 仓库 `libmpv/qml` 的成熟模式。
2. **seek 手感配方（PotPlayer 级丝滑的关键）**：全局 hr-seek 保持默认；进度条**拖动过程中**发 `seek <t> absolute+keyframes`（贴关键帧，即时出画面），**松手时**发 `seek <t> absolute+exact`（精确落点）。拖动事件节流到 ~30Hz。配合 baseline.conf 的大 demuxer 缓存。禁止在拖动中做 exact seek——那是"拉进度慢"的根源。
3. **Homebrew 的 mpv 不保证编入 `bd://` 与 `dvd://` 协议**。T0 第一件事跑 `scripts/check-mpv-caps.sh` 探测并原样汇报输出；缺失则按脚本内注释用 meson 自编译 libmpv（启用 libbluray + dvdnav）到 `third_party/prefix`，CMake 通过 PKG_CONFIG_PATH 优先链接自编译版。
4. **加密盘检测**：libbluray `bd_get_disc_info()` → `aacs_detected && !aacs_handled` 即报加密盘；DVD 侧 IFO 打不开或读扇区出现 CSS 加扰迹象 → 同样报错。不做任何绕过尝试。
5. **SACD 电平**：DSF 经 ffmpeg 解为 PCM 后比 foobar2000 + SACD 插件低约 6dB。播放 SACD 时默认 +6dB 增益，设置页可关。
6. **测试碟不入库**：真实碟资源通过环境变量 `MD_TEST_MEDIA` 指向本地目录；`tests/fixtures` 只放自造的最小结构骨架（空 mpls / ifo 壳），绝不放任何版权内容。

## 与产品侧的关系

产品目标、模块边界、SACD helper 协议、碟片指纹 v1 规范见 `docs/ARCHITECTURE.md`。
M1 阶段指纹只需**算出来并写入结构化日志**，上行 MusicDisc 匹配属于 P2，不要提前实现。
