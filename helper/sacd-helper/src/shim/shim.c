// shim 的实现面：本地 ISO 读取 + charset 直通。
// 这是铁律 #1 明确允许的「进程/协议胶水层」——上游那 1900 行 sacd_input.c 是
// PS3 光驱 + 网络传输，我们只要 pread。
#include "charset.h"
#include "portable_io.h"
#include "sacd_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#define SACD_SECTOR 2048u

struct sacd_reader_s {
    FILE* fp;
    uint64_t size;
};

sacd_reader_t* sacd_open(const char* path) {
    if (!path)
        return NULL;
    // "rb" 在 POSIX 上是空操作，在 Windows 上不可省——省了会做换行翻译（铁律 #5）。
    FILE* fp = fopen(path, "rb");
    if (!fp)
        return NULL;
    if (MD_FSEEK(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    const md_off_t end = MD_FTELL(fp);
    if (end <= 0) {
        fclose(fp);
        return NULL;
    }
    sacd_reader_t* r = (sacd_reader_t*)calloc(1, sizeof(*r));
    if (!r) {
        fclose(fp);
        return NULL;
    }
    r->fp = fp;
    r->size = (uint64_t)end;
    return r;
}

void sacd_close(sacd_reader_t* r) {
    if (!r)
        return;
    if (r->fp)
        fclose(r->fp);
    free(r);
}

uint32_t sacd_get_total_sectors(sacd_reader_t* r) {
    return r ? (uint32_t)(r->size / SACD_SECTOR) : 0u;
}

uint32_t sacd_read_block_raw(sacd_reader_t* r, uint32_t lb_number, uint32_t block_count, uint8_t* data) {
    if (!r || !data || block_count == 0)
        return 0;
    const uint64_t off = (uint64_t)lb_number * SACD_SECTOR;
    if (off >= r->size)
        return 0;
    // 越界请求截到文件尾——截断镜像不该让上层读出脏数据（与 T5 的完整性口径同源）。
    uint64_t want = (uint64_t)block_count * SACD_SECTOR;
    if (off + want > r->size)
        want = r->size - off;
    if (MD_FSEEK(r->fp, (md_off_t)off, SEEK_SET) != 0)
        return 0;
    const size_t got = fread(data, 1, (size_t)want, r->fp);
    return (uint32_t)(got / SACD_SECTOR);
}

char* charset_convert(const char* string, size_t insize, const char* from, const char* to) {
    (void)from;
    (void)to; // 直通：不转码，编码由 Qt 侧按 charset 编号处理（D-036）
    if (!string)
        return NULL;
    char* out = (char*)malloc(insize + 1);
    if (!out)
        return NULL;
    memcpy(out, string, insize);
    out[insize] = '\0';
    return out;
}
