// 64 位文件定位的可移植封装。
//
// `fseeko` / `ftello` 是 POSIX 的，MSVC 没有；MSVC 给的是 `_fseeki64` / `_ftelli64`，
// 偏移类型也不是 `off_t` 而是 `__int64`。SACD 镜像动辄 2–5 GB，32 位 `long` 的
// `fseek` 会直接溢出，所以这层不能省（铁律 #5：任何时刻保持 Windows 可编译）。
// MinGW 两套都有，跟着 POSIX 分支走即可。
#ifndef MD_SHIM_PORTABLE_IO_H
#define MD_SHIM_PORTABLE_IO_H

#include <stdio.h>

#if defined(_MSC_VER)
#include <stdint.h>
typedef __int64 md_off_t;
#define MD_FSEEK(fp, off, whence) _fseeki64((fp), (off), (whence))
#define MD_FTELL(fp) _ftelli64(fp)
#else
#include <sys/types.h>
typedef off_t md_off_t;
#define MD_FSEEK(fp, off, whence) fseeko((fp), (off), (whence))
#define MD_FTELL(fp) ftello(fp)
#endif

#endif
