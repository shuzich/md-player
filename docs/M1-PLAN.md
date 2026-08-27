# M1 计划：四类资源可播 + 丝滑 seek + 截图（macOS 验收）

## 执行规则

- 一次只执行一个任务，完成后**停下**，按文末模板汇报，等确认再继续。
- 需要产品决策的点不要自作主张，进【待决策】清单。
- 每个任务独立 feature 分支 + conventional commit，仓库保持干净（CLAUDE.md 铁律 #4）。
- 真实测试碟通过 `MD_TEST_MEDIA` 环境变量指向本地目录。

## T0 仓库初始化与能力探测

- CMake + Qt6 骨架（可运行的空窗口，标题 "md-player"）；`.clang-format`、`.gitignore`（Qt/CMake/macOS/build）、`LICENSE`（GPL-2.0-or-later）。
- GitHub Actions：macOS 构建 job（配置 + 编译即可）。
- 运行 `scripts/check-mpv-caps.sh`，**完整输出原样贴进汇报**。若缺 `bd://` 或 `dvd://`：按脚本注释自编译 libmpv 到 `third_party/prefix`，CMake 通过 PKG_CONFIG_PATH 优先链接，并把最终选型写入 docs/DECISIONS.md。
- 验收：一次 `cmake` 配置成功；`./build/md-player` 出窗口；CI 绿；能力探测结论明确。

## T1 libmpv 嵌入与文件播放

- `MpvObject`（QQuickFramebufferObject）+ render API（OpenGL），严格按 CLAUDE.md 深坑 #1。
- 启动加载 `configs/mpv-baseline.conf`；属性观察接到 QML（时长 / 进度 / 暂停态）。
- 先播 `MD_TEST_MEDIA` 下普通 mkv / mp4。
- 验收：打开本地视频画音正常；空格暂停；←/→ ±5s；窗口缩放画面正确；退出无崩溃无 mpv 错误刷屏。

## T2 播控与 seek 手感

- 进度条：拖动事件节流 ~30Hz 发 keyframes seek，松手发 exact seek（深坑 #2）；章节刻度显示。
- 音量 / 静音；音轨 / 字幕轨切换菜单（读 `track-list`）。
- 截图按钮：含字幕 / 纯画面两种，PNG 落 `~/Pictures/md-player/`。
- 简单断点续播：退出时记录 `{文件标识: 位置}` 到应用数据目录 JSON，重开询问是否继续。
- 验收：用 4K 高码率 m2ts 样本，拖动进度条画面反馈即时（主观 < 100ms）、松手落点精确；截图两种模式均正确。

## T3 蓝光模块（BDMV / BD ISO / UHD）

- libbluray 枚举 + 加密检测 + 主标题启发式（ARCHITECTURE §bluray）；标题·章节面板；选中 playlist 以 `bd://mpls/<N>` 播放。
- 章节名与 HDR 元数据做运行时探测，拿不到则降级为编号显示。
- 验收：测试集（普通演唱会 BD、UHD 原盘、蓝光音频各 ≥ 1）全部：列表与时长正确、即选即播、章节跳转准确、HDR 碟 tone mapping 正常观感；加密盘给出明确报错而非崩溃。

## T4 DVD 模块（VIDEO_TS / DVD ISO）

- libdvdread 枚举 title / 章节 / 时长；`dvd://<N>` 播放（按 T0 探测结论用系统或自编译 libmpv）。
- **加密盘拦截**：DVD 模块读 VOB 扇区自检 PES scrambling control 位（参考位置：扇区偏移 0x14 字节，**以实测为准**），命中即拦截并走 `strings.h` 的「不支持加密原盘」统一文案。拦截发生在应用层，不依赖底层库解不开（D-012）。
- 验收：演唱会 DVD 样本 title 列表正确、播放与换 title 正常、章节跳转准确；DVD ISO 与文件夹两种形态均通过。
- 验收（加密盘）：**在已安装 libdvdcss 的机器上**打开 CSS 加密 DVD，md-player 必须给出「不支持加密原盘」统一报错；**严禁静默播放成功**，也不得崩溃。

## T5 统一资源路由 + 指纹

- `open(path)` / 拖拽统一入口，按 ARCHITECTURE 判定表分派四类；普通媒体文件直通 mpv。
- **碟根自动下探**（裁决归入本任务）：拖入的目录若本身不是碟根，向下做**有限深度递归**（≤3 层）寻找 `BDMV/index.bdmv` 与 `VIDEO_TS/VIDEO_TS.IFO`。实际资源常在外面套一到两层同名目录（样本：张敬轩 酷爱演唱会、Bryan Adams Reckless、王菲 SACD 4 碟）。**找到恰好一个候选碟根 → 直接打开；找到多个 → 报错并让用户选，绝不替用户猜**；一个都没有 → 按普通文件夹的既有文案处理。递归遇到 `BDMV` / `VIDEO_TS` 目录本身即停止下钻，不进碟内目录扫描。
- 错误路径全覆盖：损坏 ISO、加密盘、无权限、误拖普通文件夹 → 各自明确文案。
  **截断 / 残缺 ISO 的完整性校验归入本任务**（裁决）：T4 遗留的实测缺口是「截断到 8MiB 的 DVD ISO，IFO 与首扇区都还读得出来，于是先报『已载入 N 条标题』、真正播放时才由 mpv 报错」。本任务要把这类镜像在**打开阶段**就判死：校验镜像实际大小是否覆盖结构声明的最末扇区（DVD 看 VOB 的 last sector，蓝光看 m2ts 的 clip 长度），不足即走「损坏 ISO」文案，不再进入标题列表。判定必须只用结构声明与文件大小，不要靠「随机抽扇区读失败」——真盘上会误伤。
- **SACD ISO 在本任务只做识别**（裁决）：按碟片签名判定为 SACD 类型并给出「SACD 将在 T6 支持」的明确提示，**不实现播放**，也不再落到 mpv 报英文原文。
- 指纹 v1 计算并写结构化日志（不做任何网络上行）。
- 验收：混合测试目录（四类 + 干扰项）识别正确率 100%；每次打开日志含 fingerprint 行；SACD ISO 给出「T6 支持」提示而非 mpv 的英文报错。
- 验收（自动下探）：套 1 层与套 2 层同名目录的样本均能直接打开；深度超过 3 层不下探；同一目录下并列两个碟根时给出「发现多个碟根，请指定」而**不是**任选其一。
- 验收（损坏 ISO）：把一张真 DVD ISO 与一张真 BD ISO 各截断到结构可解析但内容残缺的长度（如 8MiB），打开时必须**当场**给出「损坏 ISO」文案；**不得**先列出标题再在播放时失败。同一批未截断的原件必须仍然正常打开——不许为了拦截截断镜像而误伤完整碟。

## T6 SACD 支持

- vendored scarletbook + DST 解码到 `helper/sacd-helper/`（保留原 GPL 版权头，来源写入 DECISIONS）。
- 实现 helper 协议 v1（ARCHITECTURE §SACD）；`SacdClient` + `mpv_stream_cb` 注册 `sacd://`。
- 曲目列表展示碟内 SACDText 文本（缺省则显示 Track N）；2ch 区；`gapless-audio` 生效；+6dB 增益开关。
- 验收：非 DST 碟即点即播、曲目间切换无缝、seek 正常；DST 碟可播放且 seek 可用（允许首次追赶）；增益开关 A/B 可闻。

## T7 M1 收口

- 设置页：硬解开关、独占输出开关、直通开关、截图目录、SACD 增益。
- 全量异常路径回归一遍；README 写清构建与使用；打 tag `v0.1.0-m1`。
- 验收：从零 clone → 按 README 构建 → 四类资源演示全部跑通。

## T8（stretch）Windows 构建通道

- GitHub Actions windows job：Qt（aqtinstall）+ 上游预编译 libmpv + MSVC；产物 artifact。
- 验收：CI 双平台绿；Windows artifact 在 Win10/11 可启动并播放普通 mkv（碟类联调放 M2）。

## 汇报模板（每任务必用）

```
【任务】T_
【改动】文件级清单 + 一句话说明
【验证】做了哪些验证、结果如何（探测/日志类输出原样贴）
【遗留与风险】
【待决策】没有则写"无"
```
