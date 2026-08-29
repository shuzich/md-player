# tests/fixtures

只放**自造的最小结构骨架**，绝不放任何版权内容（CLAUDE.md 已知深坑 #6）。
真实碟资源通过环境变量 `MD_TEST_MEDIA` 指向仓库外的本地目录。

## make_bd_skeleton.py

生成最小可被 libbluray 解析的 BDMV 结构，用来覆盖蓝光模块的错误路径。
只写 `index.bdmv` 与 `MovieObject.bdmv` 两个导航文件的最小合法形态，无任何流数据。

```bash
# 结构正常但一条 playlist 都没有 → 期望「碟片结构正常，但没有找到可播放的标题」
python3 tests/fixtures/make_bd_skeleton.py tests/fixtures/generated/bd-clean

# 带 AACS 标记 → libbluray 报 aacs_detected=1 / aacs_handled=0
# → 期望「不支持加密原盘，请使用已解密资源」，且不得崩溃、不得静默播放
python3 tests/fixtures/make_bd_skeleton.py tests/fixtures/generated/bd-aacs --aacs

./build/md-player tests/fixtures/generated/bd-aacs
```

`--aacs` 只是造出 `AACS/Unit_Key_RO.inf` 这个空文件让 libbluray 认为碟片带 AACS，
本项目不实现、也不链接任何密钥逻辑（铁律 #2 / D-012）。

生成产物落在 `tests/fixtures/generated/`，已在 .gitignore 中排除。

## make_dvd_skeleton.py

生成最小 DVD 结构骨架，用来覆盖 DVD 模块的加密拦截与错误路径。
IFO 只有 12 字节魔数 + 补零，VOB 是自造的 MPEG-PS pack，无任何流数据。

```bash
# PES 扰码位置位 → 期望「不支持加密原盘，请使用已解密资源」，且不得崩溃、不得静默播放
python3 tests/fixtures/make_dvd_skeleton.py tests/fixtures/generated/dvd-css --css

# 同结构但不加扰 → 期望「无法读取碟片结构」
# 这条是对照组：证明拦截确实由扰码位驱动，而不是因为 IFO 读不了
python3 tests/fixtures/make_dvd_skeleton.py tests/fixtures/generated/dvd-clean

./build/md-player tests/fixtures/generated/dvd-css
```

md-player 的加密判定**先于** IFO 解析（D-022），所以骨架不需要合法的 IFO，
只需要一个结构正确的 VOB。骨架不含任何密钥逻辑，只是把两个比特置位（铁律 #2 / D-012）。

## make_broken_iso.py —— 不完整的碟镜像骨架（T5）

```bash
python3 tests/fixtures/make_broken_iso.py tests/fixtures/generated/broken
MD_LOG_UI=1 ./build/md-player tests/fixtures/generated/broken/truncated-iso9660.iso
MD_LOG_UI=1 ./build/md-player tests/fixtures/generated/broken/truncated-udf.iso
# 期望两个都是: [TOAST] 这个镜像不完整（下载未完成或已损坏），请换一份完整的镜像
```

连内容都不造——只写卷描述符，让镜像**声明**自己有 7.7GB，文件本身只有 1MiB。
md-player 的完整性判定恰好只看「结构声明 vs 文件实际大小」，不抽扇区试读
（D-026），所以这样的骨架就够验，也不含任何版权内容。

两个文件对应两条判定路线：`truncated-iso9660.iso` 走 ISO9660 主卷描述符
（DVD / SACD 镜像的形态），`truncated-udf.iso` 走 UDF 卷末锚点（蓝光原盘的形态）。

真镜像的截断验证用自己的资源做，别入库：

```bash
head -c 8388608 "$MD_TEST_MEDIA/某张.iso" > /tmp/truncated.iso
```

## make_sacd_skeleton.py —— SACD 结构骨架

```bash
python3 tests/fixtures/make_sacd_skeleton.py tests/fixtures/generated
scripts/sacd-helper-drive.py --iso tests/fixtures/generated/sacd-skeleton.iso --track 1
```

写出 1.4 MB 的镜像：ISO9660 主卷描述符（声明大小 = 实际大小，能过 T5 的完整性校验）、
`SACDMTOC` 主 TOC、8 个 `SACDText` 与 1 个 `SACD_Man`（`scarletbook_read_master_toc()`
的硬要求，少一个就判定「不是 SACD 碟」）、`TWOCHTOC` 区 TOC 与 `SACDTRL1` / `SACDTRL2`
两张曲目表，两条轨（2.000s / 1.000s，2 声道，纯 DSD）。

**音频区全零**，一个合法音频扇区都没有——这既保证零版权内容，又正好覆盖 helper
「数据不足时补零到声明时长」那条路径（D-038）。CI 用它做 sacd-helper 的冒烟。
