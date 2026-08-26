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
