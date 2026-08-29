#include "proto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

static int g_out_fd = -1;

int proto_guard_stdout(void) {
#ifdef _WIN32
    g_out_fd = _dup(1);
    if (g_out_fd < 0)
        return -1;
    // 二进制模式是硬要求：文本模式会把 0x0A 翻成 0x0D 0x0A，DSD 数据里全是这种字节（铁律 #5）。
    _setmode(g_out_fd, _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
    if (_dup2(_fileno(stderr), 1) < 0)
        return -1;
#else
    g_out_fd = dup(1);
    if (g_out_fd < 0)
        return -1;
    if (dup2(2, 1) < 0)
        return -1;
#endif
    setvbuf(stdout, NULL, _IONBF, 0);
    return 0;
}

static void raw_write(const void* data, size_t len) {
    const char* p = (const char*)data;
    while (len > 0) {
#ifdef _WIN32
        const int n = _write(g_out_fd, p, (unsigned)(len > 1u << 20 ? 1u << 20 : len));
#else
        const ssize_t n = write(g_out_fd, p, len);
#endif
        if (n <= 0)
            return;
        p += n;
        len -= (size_t)n;
    }
}

void proto_write_json(const char* line, size_t len) {
    raw_write(line, len);
    raw_write("\n", 1);
}

void proto_write_frame(uint32_t id, const uint8_t* payload, uint32_t len) {
    uint8_t hdr[8];
    hdr[0] = (uint8_t)(id);
    hdr[1] = (uint8_t)(id >> 8);
    hdr[2] = (uint8_t)(id >> 16);
    hdr[3] = (uint8_t)(id >> 24);
    hdr[4] = (uint8_t)(len);
    hdr[5] = (uint8_t)(len >> 8);
    hdr[6] = (uint8_t)(len >> 16);
    hdr[7] = (uint8_t)(len >> 24);
    raw_write(hdr, 8);
    if (len)
        raw_write(payload, len);
}

static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char* proto_base64(const uint8_t* data, size_t len) {
    const size_t out_len = ((len + 2) / 3) * 4;
    char* out = (char*)malloc(out_len + 1);
    if (!out)
        return NULL;
    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        const uint32_t a = data[i];
        const uint32_t b = (i + 1 < len) ? data[i + 1] : 0;
        const uint32_t c = (i + 2 < len) ? data[i + 2] : 0;
        const uint32_t v = (a << 16) | (b << 8) | c;
        out[o++] = kB64[(v >> 18) & 63];
        out[o++] = kB64[(v >> 12) & 63];
        out[o++] = (i + 1 < len) ? kB64[(v >> 6) & 63] : '=';
        out[o++] = (i + 2 < len) ? kB64[v & 63] : '=';
    }
    out[o] = '\0';
    return out;
}

char* proto_json_escape(const char* s) {
    if (!s)
        s = "";
    const size_t n = strlen(s);
    char* out = (char*)malloc(n * 6 + 1);
    if (!out)
        return NULL;
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        const unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"':
            out[o++] = '\\';
            out[o++] = '"';
            break;
        case '\\':
            out[o++] = '\\';
            out[o++] = '\\';
            break;
        case '\n':
            out[o++] = '\\';
            out[o++] = 'n';
            break;
        case '\r':
            out[o++] = '\\';
            out[o++] = 'r';
            break;
        case '\t':
            out[o++] = '\\';
            out[o++] = 't';
            break;
        default:
            if (c < 0x20) {
                o += (size_t)sprintf(out + o, "\\u%04x", c);
            } else {
                out[o++] = (char)c;
            }
        }
    }
    out[o] = '\0';
    return out;
}
