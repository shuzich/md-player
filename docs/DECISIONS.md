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
- **D-009 libmpv = 自编译到 `third_party/prefix`（取代系统 Homebrew 版）。** Homebrew 的 mpv 0.41.0 编入了 libbluray（`bd://` [OK]）但**未编入 dvdnav**（`dvd://` 缺失），而 M1 T4 必须走 `dvd://<title>`，故按 CLAUDE.md 深坑 #3 用 meson 自编译，`-Dlibbluray=enabled -Ddvdnav=enabled`，探测结果转为 `bd://` 与 `dvd://` 双 [OK]。CMakeLists 已实现 prefix 优先于系统的 pkg-config/CMake 查找；重建脚本见 `scripts/build-deps.sh`。
- **D-010 T0 骨架不打成 macOS .app bundle。** M1-PLAN T0 验收口径是 `./build/md-player` 直接出窗口，故 `MACOSX_BUNDLE FALSE` + 产物落 build 根；正式打包形态放 M2（Windows 首发打包一并处理）。
- **D-011 DVD 栈（libdvdread + libdvdnav）一并自编译，且 `-Dlibdvdcss=disabled`。** Homebrew 的 libdvdread 把 **libdvdcss 编成硬加载项**（`LC_LOAD_DYLIB`，非 dlopen），libdvdnav 依赖它，链条 libmpv → libdvdnav → libdvdread → libdvdcss 会让解密库随进程启动被加载，直接违反铁律 #2。故在 prefix 内重建两库并关闭该选项；递归依赖闭包扫描 46 个库，确认无 libdvdcss / libaacs / libbdplus。**残留说明**：关闭链接后 libdvdread 上游仍保留运行时 `dlopen("libdvdcss.2.dylib")` 兜底分支，我们既不链接也不分发该库，用户自行安装属其自身行为；如需彻底铲除需 patch 上游源码，见【待决策】。
