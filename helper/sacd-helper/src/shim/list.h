// vendored 的 scarletbook.h 无条件 #include <list.h>，但缩减后的导入集里
// **一个 list_* 都没有真正用到**（`scarletbook_read.c` 里看着像的全是
// `tracklist` / `access_list` 这类字段名）。上游那份 list.h 是从 Linux 内核
// 2.6.17-rt1 改的用户态版本，**GPL-2.0-only**（没有 "or any later version"），
// 873 行只为一个从未展开的宏而存在——留着它等于把整个 helper 的许可
// 从 GPL-2.0-or-later 钉死成 GPL-2.0，且多担一份第三方声明。
// 故用这个空头顶掉，不改 vendored 源码（D-033 偏离点）。
#ifndef MD_SHIM_LIST_H
#define MD_SHIM_LIST_H
#endif
