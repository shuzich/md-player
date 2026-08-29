// vendored 代码里的 LOG() 落点。上游的 libcommon/logging 是一整套带 socket
// 上报的日志框架（我们不导入），这里用 10 行顶掉。
//
// 关键约束：**一律写 stderr，绝不写 stdout**。helper 的 stdout 是协议的数据面，
// 库函数往那里打一个字就会撕碎二进制帧（D-034 里排除 EuFlo 分支就是这个原因）。
// main.c 里还有一道保险闸把 stdout 整个重定向到 stderr，这里是第一道。
#ifndef MD_SHIM_LOGGING_H
#define MD_SHIM_LOGGING_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

enum { lm_main = 0 };
enum { LOG_ERROR = 0, LOG_WARNING, LOG_NOTICE, LOG_DEBUG };

// MD_SACD_DEBUG=1 时才把 vendored 代码的絮叨打出来，默认静音。
static inline int md_sacd_debug(void) {
    static int cached = -1;
    if (cached < 0) {
        const char* v = getenv("MD_SACD_DEBUG");
        cached = (v && *v && *v != '0') ? 1 : 0;
    }
    return cached;
}

// 上游的调用形态是 LOG(lm_main, LOG_ERROR, ("fmt", a, b))——第三个参数是带括号的
// printf 参数表，没有流参数，所以这里得有个只吃 fmt 的转发函数。
static inline void md_log_printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

#define LOG(module, level, args)                                                                                       \
    do {                                                                                                               \
        if (md_sacd_debug() || (level) == LOG_ERROR) {                                                                 \
            fprintf(stderr, "[sacd-helper] ");                                                                         \
            md_log_printf args;                                                                                        \
            fputc('\n', stderr);                                                                                       \
        }                                                                                                              \
    } while (0)

#endif
