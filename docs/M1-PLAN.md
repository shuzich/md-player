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
- 验收：演唱会 DVD 样本 title 列表正确、播放与换 title 正常、章节跳转准确；DVD ISO 与文件夹两种形态均通过。

## T5 统一资源路由 + 指纹

- `open(path)` / 拖拽统一入口，按 ARCHITECTURE 判定表分派四类；普通媒体文件直通 mpv。
- 错误路径全覆盖：损坏 ISO、加密盘、无权限、误拖普通文件夹 → 各自明确文案。
- 指纹 v1 计算并写结构化日志（不做任何网络上行）。
- 验收：混合测试目录（四类 + 干扰项）识别正确率 100%；每次打开日志含 fingerprint 行。

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
