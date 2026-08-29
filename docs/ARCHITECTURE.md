# md-player 架构 v1

## 总览

```
┌────────────────────────  Qt Quick UI  ────────────────────────┐
│  播放窗口 / 进度条 / 标题·章节面板 / 设置 / （P2: 虚拟菜单）    │
└──────────────┬────────────────────────────────┬───────────────┘
               │                                │
       PlayerController                    MediaRouter
       （唯一 mpv handle，                （open(path) 统一入口）
        render API + 命令 + 属性观察）      │
               │                 ┌──────────┼──────────┬─────────────┐
             libmpv          BlurayModule  DvdModule  SacdClient    Fingerprint
        （解码/渲染/seek/     (libbluray)  (libdvdread)   │         （v1 算法，
         截图/音频输出）                            sacd-helper 进程    落日志）
                                                （scarletbook + dst）
```

原则重申：libmpv 负责一切"播"，三个 media 模块负责一切"读结构"，自研集中在路由、壳、胶水与（P2）虚拟菜单。

## 模块职责

### src/core/PlayerController
- 唯一持有 `mpv_handle`；启动时加载 `configs/mpv-baseline.conf`。
- 属性观察 → Qt signal：`time-pos` `duration` `pause` `chapter` `chapter-list` `track-list` `video-params` `eof-reached`。
- 命令封装：`load(uri, extraOpts)`、`seekDrag(t)`（keyframes）、`seekExact(t)`、`screenshot(withSubs, path)`、`setAudioExclusive(bool)`、`setSpdifPassthrough(bool)`。
- 播放定位符即 mpv URI：`bd://mpls/<N>`（配 `--bluray-device=<root>`）、`dvd://<N>`（配 `--dvd-device=<path>`）、`sacd://<token>/<area>/<track>`、普通文件路径。

### src/media/router
判定表（按序尝试，命中即分派）：

| 输入 | 判定 | 类型 |
|---|---|---|
| 目录 | 存在 `BDMV/index.bdmv` | BD（含 UHD、蓝光音频） |
| 目录 | 存在 `VIDEO_TS/VIDEO_TS.IFO` | DVD |
| *.iso | 字节偏移 `510*2048` 处签名 `SACDMTOC` | SACD |
| *.iso | `bd_open()` 成功且 `bluray_detected` | BD |
| *.iso | libdvdread 可打开 | DVD |
| 其他 | 交给 mpv 直接播 | 普通媒体文件 |

错误路径（损坏 ISO、加密盘、无权限）必须给出区分明确的用户文案。

### src/media/bluray
- 枚举：playlist 列表（时长、章节、章节名*、视频流 HDR 类型*、音频流编码/语言、PG 字幕）。带 * 的字段做 libbluray 版本运行时探测，缺失时优雅降级。
- 主标题启发式：时长最长优先，并列取章节多者；**始终**同时展示完整列表（演唱会碟常按曲目组分多个 playlist）。
- 加密检测见 CLAUDE.md 深坑 #4。

### src/media/dvd
- VMG → title 数；逐 VTS IFO → 每 title 的 PGC 时长与章节数。
- 播放走 `dvd://<title>`；依赖 T0 能力探测结论（见 M1-PLAN T0）。

### src/media/sacd + helper/sacd-helper
见下节协议。GPL vendored 代码（scarletbook 解析、DST 解码，sacd-ripper 血统）只存在于 helper 目录。

## SACD helper 协议 v1

- 形态：helper 为子进程，stdin/stdout 通信。控制面 JSON-lines；数据面为二进制帧（帧头 = 4 字节请求 id + 4 字节负载长度，小端）。
- 每条请求带 `id`（正整数，**< 2^31**）；控制响应回同一个 `id` 的 JSON 行，`read` 回同一个 `id` 的二进制帧。`read` 失败时帧头 id 的**最高位置 1**、负载为一行 JSON 错误对象——8 字节帧头的格式因此不变，调用方靠高位区分成功与失败。
- 全部诊断走 **stderr**；helper 启动第一件事是把 fd 1 复制到私有 fd、再把 `stdout` 重定向到 stderr，任何库函数误写 stdout 都撕不到帧流。
- `{"cmd":"open","path":...}` → `{"ok":true,"album":{"charset":n,"title_b64":...,"artist_b64":...},"areas":[{"area":i,"kind":"2ch"|"multi","channels":2,"dst":bool,"charset":n,"tracks":[{"index":1,"frames":n,"seconds":x,"title_b64":...,"performer_b64":...}]}]}`
  **文本一律 base64 原样吐碟内字节，helper 不转码**；`charset` 是碟内 `character_set_t` 编号（1=ISO646 / 2=ISO8859-1 / 3=Music Shift-JIS / 4=KSC5601 / 5=GB2312 / 6=Big5 / 7=ISO8859-1+ESC），由播放器侧的 `QStringDecoder` 解释，不认的编码降级为 `Track N`（D-036）。缺省字段直接不出现。
- `{"cmd":"stat","area":a,"track":t}` → `{"dsf_size":n, ...}`。helper 依据时长/声道生成**确定性 DSF 视图**（DSD+fmt+data 三段头共 92 字节 + 数据区），尺寸可预先精确计算。
- `{"cmd":"read","area":a,"track":t,"offset":o,"length":l}` → 二进制帧。DSD 音轨为线性映射；DST 音轨由 helper 实时解码，随机 seek 允许解码追赶，helper 可自行落临时缓存换取后续 seek 速度。
- `{"cmd":"close"}` 释放当前碟；`{"cmd":"quit"}` 让 helper 退出。
- 播放侧：`mpv_stream_cb_add_ro("sacd", ...)` 注册协议，SacdClient 将 URI 映射到 helper 会话，实现 read/seek/size 回调。
- M1 范围：2ch 立体声区；多声道区枚举出来但 UI 置灰（P2 开放）——置灰条目仍可点，点了给出明确文案而不是没反应。
- 曲目无缝：选中一曲时把同区后续曲目排进 mpv 播放列表，`gapless-audio` 才生效；append 必须等 `MPV_EVENT_FILE_LOADED`（D-045）。
- 随机 seek：帧号 → 起始扇区的增量索引，只解析扇区头不解码（D-043）。
- 增益：见 CLAUDE.md 深坑 #5。

## 碟片指纹 v1（M1 只计算 + 落日志）

- BD：`sha256( index.bdmv ∥ MovieObject.bdmv ∥ 按文件名排序的每个 PLAYLIST/*.mpls 的 (文件名 ∥ 大小 ∥ sha256(全文)) )`
- DVD：`sha256( VIDEO_TS.IFO ∥ 按文件名排序的每个 VTS_xx_0.IFO 的 sha256 )`
- SACD：`sha256( Master TOC 原始扇区 ∥ SACDText 区块 )`
- 输出：结构化日志一行 `fingerprint={"type":...,"sha256":...,"label":卷标或专辑名}`。
- 用途（P2）：作为 MusicDisc `/match` 接口的键，换回 Release/Medium/Track/Segment 数据渲染虚拟菜单。规范一旦发布不可变更，如需演进则新增 v2 并双写。

## 播放与画质基线

- `configs/mpv-baseline.conf` 随 T1 接入；硬解 `auto-safe`（macOS videotoolbox / Windows d3d11va）。
- HDR→SDR 由 mpv gpu 渲染 tone mapping 完成；HDR 直通（Windows 全屏）列 P3。
- 截图：`screenshot-to-file`，PNG、高位深、色彩空间标记，"含字幕 / 纯画面"两种模式。

## 路线

- **M1**（docs/M1-PLAN.md）：四类资源打开 / 枚举 / 播放 / 丝滑 seek / 截图，macOS 验收；T8 铺 Windows 构建通道。
- **M2 / P2**：虚拟菜单（指纹上行 MusicDisc、曲目与 Segment 跳转、离线缓存）、SACD 多声道、断点续播完善、Windows 首发打包、UI 引入 MusicDisc 深色玻璃拟态视觉。
- **P3 评估项**（不承诺）：BD-J 真菜单兼容模式（libbluray + JVM）、原生 DSD / DoP 输出、HDR passthrough、进度条缩略图预览。

产品愿景与模式划分见 [VISION.md](VISION.md)。
