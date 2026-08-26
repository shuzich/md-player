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
