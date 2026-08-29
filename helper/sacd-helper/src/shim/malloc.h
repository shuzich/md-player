// macOS / BSD 没有 <malloc.h>，而 vendored 代码无条件包含它。
// 转给 <stdlib.h>，不改 vendored 源码。
#ifndef MD_SHIM_MALLOC_H
#define MD_SHIM_MALLOC_H
#include <stdlib.h>
#endif
