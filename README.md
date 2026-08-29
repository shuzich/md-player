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
| [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) | vendored 第三方源码的来源与许可 |

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

命令行参数与拖拽走同一个入口（`src/media/router`）：先判定资源类型，必要时**向下最多 3 层**
去找真正的碟根（实际资源常在外面套一到两层同名目录），再分派给蓝光 / DVD 模块或直通 mpv。
一个文件夹里放了多张碟时会列出碟名让你指定，**不会替你挑一张打开**。
SACD ISO 直接可播（T6）：立体声区的曲目列在同一个标题·章节面板里，选中一曲会把同区
后续曲目一并排进播放列表，曲目间无缝；多声道区**枚举出来但置灰**，点了会说明暂不支持
（M1 范围见 docs/ARCHITECTURE.md §SACD）。解码在独立的 `sacd-helper` 进程里，它死了
播放器不会崩，只会给一条「SACD 解码进程意外退出」的提示。

碟类资源打开后自动播主标题（时长最长优先，并列取章节多者），标题与章节都在左侧
**标题·章节面板**里（蓝光与 DVD 共用同一个面板）：列出碟内标题（演唱会碟常按曲目组分多条，故不按时长排序），
主标题带徽标且默认展开章节。按 `T` 或播控条上的 `☰` 开关面板——**打开碟不会自动弹面板**，
默认直接播主标题。面板顶部的「隐藏 10 秒以下条目」默认开启，用来滤掉 UHD 原盘的占位
playlist；它只影响显示，碟内完整结构始终保留，主标题与正在播放的那条永不隐藏。
碟内未提供章节名时降级为「第 N 章」（DVD 的 IFO 结构里根本没有章节名这一项，一律显示编号）。

**加密盘**：蓝光看 libbluray 的 `aacs_detected && !aacs_handled`；DVD 由应用层自检 VOB 扇区的
PES 扰码位（D-022）。两者都给统一文案「不支持加密原盘，请使用已解密资源」，不做任何绕过尝试。
本机即便装了 libdvdcss 也不受影响——自编译的 libdvdread 关掉了该选项，进程运行期不加载它。

**不完整的镜像**：只比对「镜像自己声明的总大小」与「文件实际大小」（ISO9660 主卷描述符，
或纯 UDF 镜像的卷末锚点），下载没下完的 ISO 在**打开时**就会被拦下，不会先列出标题再在
播放时失败。判定不抽扇区试读——那在真盘上会误伤（D-026）。

**碟片指纹**：每次成功打开碟片都会往日志里写一行
`fingerprint={"type":...,"sha256":...,"label":...}`。M1 只计算与落日志，**不做任何网络上行**；
拼法钉死在 D-024，同一张碟无论放在哪个路径、以目录还是镜像形态打开，结果都一样。

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
| `g` | SACD +6dB 增益开关（临时入口，正式安家 T7 设置页） |
| `PgUp` / `PgDn` | 上一个 / 下一个条目（SACD 曲目、蓝光与 DVD 标题）；`<` / `>` 等效 |

进度条拖动时贴关键帧即时出画，松手落精确点（CLAUDE.md 深坑 #2）；章节以刻度显示在进度条上。
截图落 `~/Pictures/md-player/`。断点续播记录在
`~/Library/Application Support/MusicDisc/md-player/resume.json`。

### SACD helper

`helper/sacd-helper/` 是一个独立的 C 进程：解析 Scarlet Book 结构、解 DST，把每条曲目
呈现成一个**尺寸可预先算出**的 DSF 视图。vendored 的 GPL 血统代码只存在于这个目录，
崩溃也隔离在这个进程（docs/ARCHITECTURE.md §SACD、docs/DECISIONS.md D-006 / D-033）。
它不链接 Qt、不链接播放栈，只认管道。

```bash
export MD_TEST_MEDIA=/path/to/your/test-media

scripts/sacd-helper-drive.py --list                       # 列出目录下所有 SACD 镜像
scripts/sacd-helper-drive.py --all --determinism          # 逐张 open/stat/read + 两遍逐字节比对
scripts/sacd-helper-drive.py --iso "<某张.iso>" --track 3 # 单张单轨
scripts/sacd-helper-drive.py --all --kill-test            # kill -9 helper，看驱动侧是否收到 EOF
scripts/sacd-helper-drive.py --iso "<某张.iso>" --verify-random   # 正确性 oracle：随机存取 vs 顺序参照件
scripts/sacd-helper-drive.py --iso "<某张.iso>" --seek-bench      # seek 耗时统计
```

没有真碟时用自造骨架（不含任何音频内容）：

```bash
python3 tests/fixtures/make_sacd_skeleton.py tests/fixtures/generated
scripts/sacd-helper-drive.py --iso tests/fixtures/generated/sacd-skeleton.iso --track 1
```

helper 只认管道，不链 Qt 也不链播放栈。播放器侧通过自注册的 `sacd://` 协议接它，
每条流一个 helper 子进程（docs/DECISIONS.md D-042）。

随机 seek 靠「帧号 → 起始扇区」的增量索引，只解析扇区头不解码（D-043）：
已索引区间回退 seek 实测中位 5–7 ms（DST）/ 0 ms（纯 DSD），跳到未索引的轨尾 8–25 ms。

### 测试素材

真实碟片与样片**不入库**（见 CLAUDE.md 深坑 #6）。开发和验收时用环境变量
`MD_TEST_MEDIA` 指向本地素材目录，**所有脚本与验证一律走它，不在任何地方硬编码路径**：

```bash
export MD_TEST_MEDIA=/path/to/your/test-media
./build/md-player "$MD_TEST_MEDIA/某张碟"
```

该目录应含普通媒体文件（mp4 / mkv / flac，T1 / T2 用）与四类碟样本：BDMV 目录、
VIDEO_TS 目录、BD / DVD / SACD 镜像。SACD 验收还需要覆盖 **纯 DSD** 与 **DST 压缩**
两种编码（多声道区几乎必用 DST），`scripts/sacd-helper-drive.py --list` 会把目录下所有
带 `SACDMTOC` 签名的镜像列出来。

`tests/fixtures/` 只放自造的最小结构骨架，**绝不放任何版权内容**。

错误路径不依赖真实碟片——用自造骨架覆盖（详见 `tests/fixtures/README.md`）：

```bash
python3 tests/fixtures/make_bd_skeleton.py tests/fixtures/generated/bd-aacs --aacs
MD_LOG_UI=1 ./build/md-player tests/fixtures/generated/bd-aacs

python3 tests/fixtures/make_dvd_skeleton.py tests/fixtures/generated/dvd-css --css
MD_LOG_UI=1 ./build/md-player tests/fixtures/generated/dvd-css
# 期望: [TOAST] 不支持加密原盘，请使用已解密资源

python3 tests/fixtures/make_broken_iso.py tests/fixtures/generated/broken
MD_LOG_UI=1 ./build/md-player tests/fixtures/generated/broken/truncated-udf.iso
# 期望: [TOAST] 这个镜像不完整（下载未完成或已损坏），请换一份完整的镜像
```

### 诊断日志

| 环境变量 | 作用 |
|---|---|
| `MD_LOG_PROGRESS=1` | 按秒打印播放进度与暂停态变化 |
| `MD_LOG_PROPS=1` | 打印全部 mpv 属性变更事件 |
| `MD_LOG_SEEK=1` | 打印每次 seek 的标志、目标与「命令→画面到位」耗时 |
| `MD_LOG_TRACKS=1` | 打印章节与音轨/字幕轨枚举结果 |
| `MD_LOG_UI=1` | 把提示条（toast）文案打到 stdout，用于无截屏权限时校验用户实际看到的文案 |
| `MD_LOG_UI=1` | 同时打印标题·章节面板的开合（`[PANEL]` 行）——面板没有提示条，无截屏权限时这是唯一可核对的痕迹 |
| `MD_MPV_CONF=<路径>` | 覆盖 `configs/mpv-baseline.conf` |
| `MD_SACD_DEBUG=1` | sacd-helper 打印 vendored 解析层的全部诊断（一律走 stderr） |
| `MD_SACD_STDOUT_TEST=1` | sacd-helper 启动时故意用四种方式往 stdout 写字，用来实测保险闸（D-040） |
| `MD_SACD_DUMP=<目录>` | 把 mpv 经 `sacd://` 实际读到的字节按偏移写进稀疏文件（另附 `.log` 记每次读的 offset/len），用来和顺序导出的参照件逐字节对账 |

### 代码格式

提交前必须格式化（LLVM 基础，120 列）：

```bash
clang-format -i $(git ls-files '*.cpp' '*.h')
```
