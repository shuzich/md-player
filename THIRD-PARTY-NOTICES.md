# 第三方代码声明（THIRD-PARTY NOTICES）

md-player 本体为 **GPL-2.0-or-later**（见 [LICENSE](LICENSE)）。
仓库内另有一批 vendored 源码，全部位于 `helper/sacd-helper/vendor/`，
只编进独立的 `sacd-helper` 进程，与播放器主体无任何链接关系（D-006）。

**T7 打 tag 前本文件必须与 `helper/sacd-helper/vendor/` 的实际内容一致。**
新增或删除 vendored 文件时同步更新这里。

---

## 1. sacd-ripper —— Scarlet Book 结构解析

| 项 | 值 |
|---|---|
| 来源 | https://github.com/sacd-ripper/sacd-ripper |
| commit | `a3d981c935c3224217e2842cd492f9351106c81e`（2023-01-14, master） |
| 许可 | **GPL-2.0-or-later** |
| 版权 | Copyright (c) 2010-2015 by respective authors |
| 位置 | `helper/sacd-helper/vendor/scarletbook/`、`helper/sacd-helper/vendor/common/list.h` |

文件：`scarletbook.h` `scarletbook.c` `scarletbook_read.c` `scarletbook_read.h`
`scarletbook_helpers.h` `sacd_read_internal.h` `endianess.h` `list.h`（共 8 个，1 956 + 873 行）

许可与本项目一致，源码版权头原样保留。选型与裁剪范围见 `docs/DECISIONS.md` D-033 / D-034 / D-035。

## 2. MPEG-4 Audio RM Module —— DST（Direct Stream Transfer）无损解码

| 项 | 值 |
|---|---|
| 来源 | 随 sacd-ripper 一同分发（`libs/libdstdec/`），同一 commit |
| 许可 | **MPEG-4 Audio 参考软件条款**（非 GPL，见下） |
| 版权 | Copyright 2004，Philips Digital Systems Laboratories / Philips Research Eindhoven |
| 作者 | Aad Rijnberg、Fons Bruekers、Eric Knapen；编辑：Richard Theelen |
| 位置 | `helper/sacd-helper/vendor/dstdec/` |

文件：`ccp_calc.c/.h` `conststr.h` `dst_ac.c/.h` `dst_data.c/.h` `dst_fram.c/.h`
`dst_init.c/.h` `types.h` `unpack_dst.c/.h`（共 14 个，3 398 行）

原始声明（逐字保留在每个文件头部）要点：

> This software module is an implementation of a part of one or more MPEG-4 Audio
> tools as specified by the MPEG-4 Audio standard. ISO/IEC gives users of the
> MPEG-4 Audio standards free licence to this software module or modifications
> thereof for use in hardware or software products claiming conformance to the
> MPEG-4 Audio standards. Those intending to use this software module in hardware
> or software products are advised that this use may infringe existing patents.
> … Copyright is not released for non MPEG-4 Audio conforming products. …
> This copyright notice must be included in all copies of derivative works.

即：ISO/IEC 对**符合 MPEG-4 Audio 标准**的用途授予免费许可，但保留专利风险提示，
且要求版权声明随所有衍生作品分发。本项目的用途（解码 SACD 碟内的 DST 码流）属于
MPEG-4 Audio 一致性用途，版权头原样保留、未作任何修改。

## 3. 已评估但**未**导入的第三方代码

记在这里是为了让「为什么没有它」也有据可查：

| 项 | 许可 | 未导入的原因 |
|---|---|---|
| `libcommon/charset.c/h`（xmms 血统，Haavard Kvaalen） | LGPL-2 | helper 不解释碟内文本，改为原样吐字节 + 字符集编号，转码交 Qt 侧（D-036）。整个依赖连同 LGPL 义务一并消失 |
| `libdstdec/yarn.c/h`（Mark Adler，pigz 血统） | zlib | DST 走单线程帧级 API，上游的多线程调度层整层不启用（D-033 偏离点 1） |
| `libdstdec/dst_decoder.c/h`、`buffer_pool.c/h` | GPL-2.0-or-later | 同上，属多线程调度层 |
| `libiconv` | LGPL | 平台自带；PS3 才需要随包分发 |
| `libanergistic` / `libunself` / `libpatchutils` | GPL-2.0-or-later | PS3 SPU 模拟、自签名 ELF 解包，与播放无关 |

---

本项目**不包含、不链接、不携带**任何解密组件（libaacs / libbdplus / libdvdcss），
也不实现任何密钥或鉴权逻辑。上游 `sacd_input.c` 里读物理 SACD 所需的
`sacd_decrypt()` / `sacd_authenticate()` **未导入、未声明、未实现**（D-003 / D-012）。
