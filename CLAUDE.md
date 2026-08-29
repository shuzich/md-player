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
├── helper/sacd-helper/        # 独立进程（C）
│   ├── src/                   # 自研：协议 v1、DSF 视图、帧装配、vendor shim
│   └── vendor/                # vendored 第三方源码，一行不改（THIRD-PARTY-NOTICES.md）
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
7. **Qt Quick Popup 的焦点陷阱**：Popup（含 Drawer/Dialog）设 `focus: true` 后，`QQuickPopupItem` 会成为窗口的 `activeFocusItem`，此后**所有 `Qt.WindowShortcut` 快捷键静默失效**——`enabled` 仍是 `true`，就是不触发，没有任何报错。本项目所有覆盖在播放画面上的 Popup 一律 `focus: false`，Esc 关闭改由窗口级 `Shortcut` 承担；面板内部的 Controls 控件还须显式 `focusPolicy: Qt.NoFocus`，否则点一下控件同样会把焦点吸进 Popup。详见 D-018。
   **例外是必须要键盘焦点的模态框**（续播询问框：Esc 要能关、按钮要能 Tab），它只能 `focus: true`，于是窗口级快捷键在它开着时天然失效——这正是我们要的。但代价是回车也进不来：Qt Quick Controls 的 Button 只吃 Space，没有 Widgets 那套 default button 语义，Return 落到按钮上直接 ignore。解法是**在 Popup 内部沿父链接住冒泡的按键**（把 `Keys.onPressed` 挂到 `DialogButtonBox` 这类按钮的父项上），不要再去试窗口级 `Shortcut`——那条路从机制上就不通。详见 D-032。
8. **重叠的 TapHandler 会一起触发**：父项与子项各挂一个 `TapHandler` 且区域重叠时，点子项会**两个都响应**（T3 的标题行与展开箭头就是这样，点箭头顺带把该标题重新载入一遍；蓝光上不明显，DVD 换 title 要几秒，一眼可见）。解法是让点击区物理不重叠——把行的 `TapHandler` 挂到一个 anchors 避开箭头的 `Item` 上，而不是指望事件被"消费"。
9. **交互验证前先确保环境干净**：跑任何 UI 验证之前必须先清 `resume.json`（断点记录）、`pkill md-player` 确认没有残留进程、确认没有模态框开着。曾经因为遗留断点记录让模态续播框一直开着，把「模态框挡住点击 + 按住快捷键失效」整整误判成播控条与快捷键功能回归，白查了十几轮。**现象不对先怀疑环境，再怀疑代码。**
10. **mpv 的「属性无值」事件 data 是空指针**：`aid` / `sid` 关成 `no` 时，mpv 发的 `MPV_EVENT_PROPERTY_CHANGE` 带的是 `MPV_FORMAT_NONE` + `data == nullptr`，而不是某个负数。`handlePropertyChange` 开头那道 `if (!prop->data) return;` 闸门会把这类事件整个吞掉，于是「关字幕」在 mpv 侧生效了、UI 状态却永远停在上一条轨上——没有任何报错，看起来就像点了没反应。凡是**能被关闭**的属性（aid / sid / vid / 各种 `-delay`），一律在空指针闸门**之前**单独处理，把 NONE 折算成 -1。查这类问题时先用 `MD_LOG_PROPS=1` 看 `fmt=0 data=0x0`，再用 `mpv_get_property_string` 对一遍 mpv 侧真值，别只信 UI（issue #2）。

11. **验证方法缺参照系 —— 两种典型，都会给出「没问题」的假结论**：
    (a) **阴性结论未自检工具**。「命中 0 / 无匹配」当证据之前，先确认工具真的看得见目标。实例：本仓库的 `grep` 是个 shell 函数包装，对 ISO-8859 等非 UTF-8 文件**静默跳过**，于是 `list.h` 的引用数被数成 5（实际 0）、`vendor/dstdec/` 的许可头被数成 6/14（实际 14/14），两处结论都反了。校验办法：换一个独立工具（`awk` / `sed` / Python）复算一遍，或先用一个**必然命中**的模式自检工具本身。
    (b) **把确定性/幂等当成正确性**。「两次读一致」「跨版本 sha256 一致」只证明可复现，**确定性的错误数据同样全部通过**。实例：T6 阶段 2 的 seek 缺陷——人工验收听出杂音，而自动证据（同偏移两次读一致 + 与阶段 1 的 sha256 一致）全绿。正确性必须对**独立参照物**：顺序全量导出做参照件，再让随机存取路径逐段 memcmp（`scripts/sacd-helper-drive.py --verify-random`）；或者拿外部实现（ffmpeg / ffprobe）对一遍账。
    一句话：**任何「没问题」的结论，先问它的参照物是什么；没有独立参照物的自比对不构成证据。**

## 与产品侧的关系

产品目标、模块边界、SACD helper 协议、碟片指纹 v1 规范见 `docs/ARCHITECTURE.md`。
M1 阶段指纹只需**算出来并写入结构化日志**，上行 MusicDisc 匹配属于 P2，不要提前实现。
