# 决策记录（DECISIONS）

规则：影响架构、依赖、协议、验收口径的决策都记一条；一行现状 + 一行理由。修改既有决策 = 新增条目并标注取代关系，不改历史。

- **D-001 播放内核 = libmpv。** ffmpeg 生态 + gpu-next 渲染，seek/截图/HDR/独占输出全覆盖；PotPlayer 闭源不可用，自研解码链违反铁律 #1。
- **D-002 菜单策略 = 虚拟菜单优先，BD-J 列 P3 评估。** 演唱会碟菜单本质是选曲；MusicDisc 数据可做出优于原生菜单的体验。开源 BD-J 仅 libbluray+JVM 一条路，兼容性天花板即 VLC/Kodi，不值得阻塞主线。
- **D-003 明确不做解密。** 只支持已解密资源，绕开 AACS/CSS 的法律与工程复杂度；加密盘给明确报错。
- **D-004 License = GPL-2.0-or-later。** 依赖链（mpv 默认构建、libdvdread、scarletbook 血统代码）均为 GPL 系；项目本身定位社区工具，开源为正资产。
- **D-005 UI = Qt 6 Quick，渲染强制 OpenGL RHI。** mpv render API 需要 GL；Metal 默认路径不可用（CLAUDE.md 深坑 #1）。
- **D-006 SACD 解码隔离在独立 helper 进程。** GPL vendored 代码物理隔离、崩溃隔离、协议清晰（ARCHITECTURE §SACD）。
- **D-007 开发与 M1 验收平台 = macOS；Windows 走 T8 构建通道、M2 打包首发。** 开发机为 macOS；Qt+libmpv 栈双平台同源。
- **D-008 碟片指纹 v1 规范冻结。** 见 ARCHITECTURE §指纹；发布后不可变，演进走 v2 双写。
- **D-009（T0 探测已跑，结论待定）系统 libmpv vs 自编译。** 2026-08-25 在验收机（macOS，Darwin 25.6）执行 `scripts/check-mpv-caps.sh`：机器上**完全没有 mpv、Homebrew、CMake、Qt、pkg-config**，`bd://` 与 `dvd://` 均报缺失，pkg-config 三项全部「未找到」——这是「工具链尚未安装」而非「系统 mpv 能力不足」。装好工具链后须重跑探测才能定稿本条：若 `brew install mpv` 后 `bd://` 与 `dvd://` 均 [OK] → 选系统 libmpv；任一缺失 → 按脚本注释用 meson 自编译到 `third_party/prefix`（CMakeLists 已实现该前缀优先于系统的链接逻辑）。
- **D-010 T0 骨架不打成 macOS .app bundle。** M1-PLAN T0 验收口径是 `./build/md-player` 直接出窗口，故 `MACOSX_BUNDLE FALSE` + 产物落 build 根；正式打包形态放 M2（Windows 首发打包一并处理）。
