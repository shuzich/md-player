// vendored 的 scarletbook_read.c #include <utils.h>，但实际一个符号都没用到
// （CHECK_ZERO 是它自己文件里定义的）。上游 libcommon/utils.c 里全是文件名清洗
// 与路径拼接，属「落盘导出」那一类，整块不导入。留个空头满足包含即可。
#ifndef MD_SHIM_UTILS_H
#define MD_SHIM_UTILS_H
#endif
