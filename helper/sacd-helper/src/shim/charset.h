// charset_convert 的直通替身。
//
// 上游用 libcommon/charset.c（LGPL-2，走 iconv）把碟内文本就地转成 UTF-8。
// 本项目不这么做：helper 不解释文本，原样吐字节 + 字符集编号，转码交给 Qt 侧的
// QStringDecoder（D-036）。于是这里的 charset_convert 只做一件事——把 insize
// 个字节复制出来并补一个 NUL，字符集编号另行从 Area TOC 的 locale 表取。
//
// 副作用是把 LGPL-2 的 charset.c 整个从导入清单里去掉了，第三方许可少一种。
// 前提：SACDText 用的几种编码（ISO646 / ISO8859-1 / Music Shift-JIS / GB2312 /
// Big5 / KSC5601）都不含内嵌 NUL，当 C 字符串传递不丢字节。
#ifndef MD_SHIM_CHARSET_H
#define MD_SHIM_CHARSET_H

#include <stddef.h>

char* charset_convert(const char* string, size_t insize, const char* from, const char* to);

#endif
