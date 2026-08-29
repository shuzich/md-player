# 第三方代码声明（THIRD-PARTY NOTICES）

md-player 本体为 **GPL-2.0-or-later**（见 [LICENSE](LICENSE)）。
仓库内另有一批 vendored 源码，全部位于 `helper/sacd-helper/vendor/`，
只编进独立的 `sacd-helper` 进程，与播放器主体无任何链接关系（D-006）。

**当前 vendored 总量：21 个文件 / 5 354 行，两种许可。**

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
| 位置 | `helper/sacd-helper/vendor/scarletbook/` |

**7 个文件 / 1 956 行**，逐一核对过版权头，全部为 GPL-2.0-or-later：

| 文件 | 行数 |
|---|---|
| `scarletbook.h` | 617 |
| `scarletbook.c` | 77 |
| `scarletbook_read.c` | 923 |
| `scarletbook_read.h` | 67 |
| `scarletbook_helpers.h` | 72 |
| `sacd_read_internal.h` | 36 |
| `endianess.h` | 164 |

许可与本项目一致，源码版权头原样保留、一行未改。选型与裁剪范围见 `docs/DECISIONS.md` D-033 / D-034 / D-035。

## 2. MPEG-4 Audio RM Module —— DST（Direct Stream Transfer）无损解码

| 项 | 值 |
|---|---|
| 来源 | 随 sacd-ripper 一同分发（`libs/libdstdec/`），同一 commit |
| 许可 | **MPEG-4 Audio 参考软件条款**（非 GPL，全文见下） |
| 版权 | Copyright 2004，Philips Digital Systems Laboratories / Philips Research Eindhoven |
| 位置 | `helper/sacd-helper/vendor/dstdec/` |

**14 个文件 / 3 398 行。经逐文件核对，14 个文件第 2 行全部是 `MPEG-4 Audio RM Module`，
即 14 / 14 携带下述声明，没有例外，也没有任何一个带 GPL 头。**

| 文件 | 行数 | 文件 | 行数 |
|---|---|---|---|
| `ccp_calc.c` | 156 | `dst_fram.c` | 460 |
| `ccp_calc.h` | 70 | `dst_fram.h` | 78 |
| `conststr.h` | 170 | `dst_init.c` | 365 |
| `dst_ac.c` | 219 | `dst_init.h` | 74 |
| `dst_ac.h` | 74 | `types.h` | 174 |
| `dst_data.c` | 445 | `unpack_dst.c` | 942 |
| `dst_data.h` | 94 | `unpack_dst.h` | 77 |

声明原文（以下逐字取自 `vendor/dstdec/unpack_dst.c` 文件头，未作任何删改、缩写或
翻译；其余 13 个文件的头部除末尾 `Source file:` / `Authors:` / `Changes:` 三段
随文件不同外，许可与版权段落逐字相同）：

```
/***********************************************************************
MPEG-4 Audio RM Module
Lossless coding of 1-bit oversampled audio - DST (Direct Stream Transfer)

This software was originally developed by:

* Aad Rijnberg 
  Philips Digital Systems Laboratories Eindhoven 
  <aad.rijnberg@philips.com>

* Fons Bruekers
  Philips Research Laboratories Eindhoven
  <fons.bruekers@philips.com>
   
* Eric Knapen
  Philips Digital Systems Laboratories Eindhoven
  <h.w.m.knapen@philips.com> 

And edited by:

* Richard Theelen
  Philips Digital Systems Laboratories Eindhoven
  <r.h.m.theelen@philips.com>

in the course of development of the MPEG-4 Audio standard ISO-14496-1, 2 and 3.
This software module is an implementation of a part of one or more MPEG-4 Audio
tools as specified by the MPEG-4 Audio standard. ISO/IEC gives users of the
MPEG-4 Audio standards free licence to this software module or modifications
thereof for use in hardware or software products claiming conformance to the
MPEG-4 Audio standards. Those intending to use this software module in hardware
or software products are advised that this use may infringe existing patents.
The original developers of this software of this module and their company,
the subsequent editors and their companies, and ISO/EIC have no liability for
use of this software module or modifications thereof in an implementation.
Copyright is not released for non MPEG-4 Audio conforming products. The
original developer retains full right to use this code for his/her own purpose,
assign or donate the code to a third party and to inhibit third party from
using the code for non MPEG-4 Audio conforming products. This copyright notice
must be included in all copies of derivative works.

Copyright  2004.

Source file: UnpackDST.c (Unpacking DST Frame Data)

Required libraries: <none>

Authors:
RT:  Richard Theelen, PDSL-labs Eindhoven <r.h.m.theelen@philips.com>

Changes:
08-Mar-2004 RT  Initial version

************************************************************************/
```

> 注：正文第 41 行原文即为 `Copyright  2004.`（`Copyright` 与 `2004` 之间是两个
> 空格，上游文件里本就没有 © 字符）——此处照录，未作补正。

**要点**：ISO/IEC 对**符合 MPEG-4 Audio 标准**的用途授予免费许可；明确提示该用途
**可能触及现有专利**；对**不符合 MPEG-4 Audio 标准**的产品不释放版权；且
**该版权声明必须包含在所有衍生作品的副本中**。本项目的用途（解码 SACD 碟内的 DST
码流）属于 MPEG-4 Audio 一致性用途，14 个文件的版权头原样保留、未作任何修改。

## 3. 已评估但**未**导入的第三方代码

记在这里是为了让「为什么没有它」也有据可查：

| 项 | 许可 | 未导入的原因 |
|---|---|---|
| `libcommon/list.h`（Linux 内核 2.6.17-rt1 用户态改版） | **GPL-2.0-only**（原文 "version 2 of the License"，无 "or any later"） | 缩减后的导入集里**一个 `list_*` 都没有真正用到**——`scarletbook_read.c` 里看着像的全是 `tracklist` / `access_list` 这类字段名。873 行只为一个从未展开的 `#include` 而存在，留着它会把整个 helper 的许可从 GPL-2.0-or-later 钉死成 GPL-2.0，还要多担一份第三方声明。改用 `src/shim/list.h` 空头顶掉 |
| `libcommon/charset.c/h`（xmms 血统，Haavard Kvaalen） | LGPL-2 | helper 不解释碟内文本，改为原样吐字节 + 字符集编号，转码交 Qt 侧（D-036）。整个依赖连同 LGPL 义务一并消失 |
| `libdstdec/yarn.c/h`（Mark Adler，pigz 血统） | zlib | DST 走单线程帧级 API，上游的多线程调度层整层不启用（D-033 偏离点 1） |
| `libdstdec/dst_decoder.c/h`、`buffer_pool.c/h` | GPL-2.0-or-later | 同上，属多线程调度层 |
| `libsacd/dsf.h` | GPL-2.0-or-later | DSF 头布局在自研的 `dsf_view.c` 里按规范直接写，用不上这个结构体 |
| `libsacd/sacd_reader.h`、`sacd_input.c/h` | GPL-2.0-or-later | 会拖进 PS3 光驱 ioctl 与 protobuf 网络传输；且声明了 `sacd_decrypt()` / `sacd_authenticate()` 鉴权面，本项目不导入不声明不实现 |
| `libiconv` | LGPL | 平台自带；PS3 才需要随包分发 |
| `libanergistic` / `libunself` / `libpatchutils` | GPL-2.0-or-later | PS3 SPU 模拟、自签名 ELF 解包，与播放无关 |

## 4. 开放项

- **DST 解码器换用 libavcodec 的可行性尚未定论**，见 `docs/DECISIONS.md` D-037。
  ffmpeg 自 2015 年起自带 DST 解码器，若在 helper 内部改用它，可以整段去掉本文件
  第 2 节那 14 个文件与随之而来的 MPEG-4 参考软件条款（含其专利提示）；代价是
  helper 要链 libavcodec（LGPL-2.1+ 或 GPL，取决于构建配置）。
  **T7 打 tag 前必须给出结论**：要么落实替换并删掉第 2 节，要么明确保留并确认
  MPEG-4 条款的分发义务已履行。

---

本项目**不包含、不链接、不携带**任何解密组件（libaacs / libbdplus / libdvdcss），
也不实现任何密钥或鉴权逻辑。上游 `sacd_input.c` 里读物理 SACD 所需的
`sacd_decrypt()` / `sacd_authenticate()` **未导入、未声明、未实现**（D-003 / D-012）。
