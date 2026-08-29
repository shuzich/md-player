// sacd-helper —— SACD ISO 的独立解析/解码进程（ARCHITECTURE §SACD 协议 v1）。
//
// 为什么是独立进程：vendored 的 GPL 血统代码（scarletbook 解析 + MPEG-4 RM 的 DST
// 解码器）物理隔离在这里，崩溃也隔离在这里（D-006）。播放器侧只通过管道说话。
//
// stdin  ← 一行一条 JSON 请求
// stdout → 控制响应是 JSON 行；read 响应是二进制帧（4B id + 4B 长度，小端）
// stderr → 全部诊断。stdout 在 main() 第一件事就被保险闸接管。
#include "dsf_view.h"
#include "proto.h"
#include "scarletbook.h"
#include "scarletbook_read.h"
#include "shim/logging.h"
#include "shim/sacd_reader.h"
#include "track_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#define MAX_LINE (1 << 16)
#define MAX_READ (1 << 20)

typedef struct {
    sacd_reader_t* reader;
    scarletbook_handle_t* sb;
    track_reader_t* tr;
    int tr_area;
    int tr_track;
} session_t;

// ---- 极简 JSON 取值：只支持平坦对象里的字符串与整数，够本协议用 ----

static const char* json_find(const char* s, const char* key) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* p = strstr(s, pat);
    if (!p)
        return NULL;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p != ':')
        return NULL;
    p++;
    while (*p == ' ' || *p == '\t')
        p++;
    return p;
}

static int json_int(const char* s, const char* key, long long* out) {
    const char* p = json_find(s, key);
    if (!p)
        return 0;
    char* end = NULL;
    const long long v = strtoll(p, &end, 10);
    if (end == p)
        return 0;
    *out = v;
    return 1;
}

static int json_str(const char* s, const char* key, char* out, size_t cap) {
    const char* p = json_find(s, key);
    if (!p || *p != '"')
        return 0;
    p++;
    size_t o = 0;
    while (*p && *p != '"' && o + 1 < cap) {
        if (*p == '\\' && p[1]) {
            p++;
            switch (*p) {
            case 'n':
                out[o++] = '\n';
                break;
            case 't':
                out[o++] = '\t';
                break;
            case 'r':
                out[o++] = '\r';
                break;
            default:
                out[o++] = *p;
                break;
            }
            p++;
        } else {
            out[o++] = *p++;
        }
    }
    out[o] = '\0';
    return *p == '"';
}

// ---- 响应 ----

static void reply_error(long long id, const char* msg) {
    char* esc = proto_json_escape(msg);
    char buf[1024];
    const int n = snprintf(buf, sizeof(buf), "{\"id\":%lld,\"ok\":false,\"error\":\"%s\"}", id, esc ? esc : "");
    free(esc);
    proto_write_json(buf, (size_t)(n > 0 ? n : 0));
}

// 文本一律 base64 原样吐，不转码；编码由 charset 字段告诉调用方（D-036）。
static void append_text_field(char** out, size_t* cap, size_t* len, const char* name, const char* raw) {
    if (!raw || !*raw)
        return;
    char* b64 = proto_base64((const uint8_t*)raw, strlen(raw));
    if (!b64)
        return;
    const size_t need = *len + strlen(name) + strlen(b64) + 8;
    if (need > *cap) {
        size_t c = *cap ? *cap : 4096;
        while (c < need)
            c *= 2;
        char* nb = (char*)realloc(*out, c);
        if (!nb) {
            free(b64);
            return;
        }
        *out = nb;
        *cap = c;
    }
    *len += (size_t)snprintf(*out + *len, *cap - *len, ",\"%s\":\"%s\"", name, b64);
    free(b64);
}

static void cmd_open(session_t* s, long long id, const char* path) {
    if (s->sb) {
        scarletbook_close(s->sb);
        s->sb = NULL;
    }
    if (s->reader) {
        sacd_close(s->reader);
        s->reader = NULL;
    }
    s->reader = sacd_open(path);
    if (!s->reader) {
        reply_error(id, "打不开镜像文件");
        return;
    }
    s->sb = scarletbook_open(s->reader);
    if (!s->sb) {
        sacd_close(s->reader);
        s->reader = NULL;
        reply_error(id, "不是 SACD 镜像，或 Master TOC 读不出来");
        return;
    }

    size_t cap = 8192, len = 0;
    char* out = (char*)malloc(cap);
    if (!out) {
        reply_error(id, "内存不足");
        return;
    }
    const int album_charset = s->sb->master_toc ? s->sb->master_toc->locales[0].character_set : 0;
    len = (size_t)snprintf(out, cap, "{\"id\":%lld,\"ok\":true,\"album\":{\"charset\":%d", id, album_charset);
    append_text_field(&out, &cap, &len, "title_b64", s->sb->master_text.album_title);
    append_text_field(&out, &cap, &len, "artist_b64", s->sb->master_text.album_artist);
    append_text_field(&out, &cap, &len, "disc_title_b64", s->sb->master_text.disc_title);

    // 只报 TOC-1 的两个区（下标 2/3 是上游放备份 TOC-2 的位置，不对外暴露）
    const int idx[2] = {s->sb->twoch_area_idx, s->sb->mulch_area_idx};
    const char* kind[2] = {"2ch", "multi"};
    len += (size_t)snprintf(out + len, cap - len, "},\"areas\":[");
    int first_area = 1;
    for (int a = 0; a < 2; a++) {
        if (idx[a] < 0)
            continue;
        scarletbook_area_t* area = &s->sb->area[idx[a]];
        if (!area->area_toc)
            continue;
        const area_toc_t* toc = area->area_toc;
        const int charset = toc->languages[0].character_set;
        if (cap - len < 4096) {
            char* nb = (char*)realloc(out, cap * 2);
            if (!nb)
                break;
            out = nb;
            cap *= 2;
        }
        len += (size_t)snprintf(out + len, cap - len,
                                "%s{\"area\":%d,\"kind\":\"%s\",\"channels\":%d,\"dst\":%s,\"charset\":%d,\"tracks\":[",
                                first_area ? "" : ",", idx[a], kind[a], toc->channel_count,
                                toc->frame_format == FRAME_FORMAT_DST ? "true" : "false", charset);
        first_area = 0;
        for (int t = 0; t < toc->track_count; t++) {
            if (cap - len < 2048) {
                char* nb = (char*)realloc(out, cap * 2);
                if (!nb)
                    break;
                out = nb;
                cap *= 2;
            }
            const uint32_t frames = TIME_FRAMECOUNT(&area->area_tracklist_time->duration[t]);
            len += (size_t)snprintf(out + len, cap - len, "%s{\"index\":%d,\"frames\":%u,\"seconds\":%.3f",
                                    t ? "," : "", t + 1, frames, (double)frames / (double)SACD_FRAME_RATE);
            append_text_field(&out, &cap, &len, "title_b64", area->area_track_text[t].track_type_title);
            append_text_field(&out, &cap, &len, "performer_b64", area->area_track_text[t].track_type_performer);
            len += (size_t)snprintf(out + len, cap - len, "}");
        }
        len += (size_t)snprintf(out + len, cap - len, "]}");
    }
    len += (size_t)snprintf(out + len, cap - len, "]}");
    proto_write_json(out, len);
    free(out);
}

// 找到 (area,track) 对应的 reader，必要时新建；area 是 handle->area[] 的下标。
static track_reader_t* ensure_track(session_t* s, int area, int track) {
    if (s->tr && s->tr_area == area && s->tr_track == track)
        return s->tr;
    if (s->tr) {
        track_reader_close(s->tr);
        s->tr = NULL;
    }
    s->tr = track_reader_open(s->sb, s->reader, area, track - 1);
    s->tr_area = area;
    s->tr_track = track;
    return s->tr;
}

static void cmd_stat(session_t* s, long long id, int area, int track) {
    track_reader_t* tr = ensure_track(s, area, track);
    if (!tr) {
        reply_error(id, "没有这个区或这条曲目");
        return;
    }
    const dsf_view_t* v = track_reader_view(tr);
    char buf[512];
    const int n = snprintf(buf, sizeof(buf),
                           "{\"id\":%lld,\"ok\":true,\"dsf_size\":%llu,\"channels\":%u,\"frames\":%u,"
                           "\"bytes_per_channel\":%llu,\"blocks\":%llu}",
                           id, (unsigned long long)v->total_size, v->channels, v->duration_frames,
                           (unsigned long long)v->bytes_per_channel, (unsigned long long)v->blocks);
    proto_write_json(buf, (size_t)n);
}

static void cmd_read(session_t* s, long long id, int area, int track, long long offset, long long length) {
    track_reader_t* tr = ensure_track(s, area, track);
    if (!tr) {
        char msg[128];
        const int n = snprintf(msg, sizeof(msg), "{\"error\":\"没有这个区或这条曲目\"}");
        proto_write_frame((uint32_t)id | PROTO_ERROR_FLAG, (const uint8_t*)msg, (uint32_t)n);
        return;
    }
    if (length < 0 || length > MAX_READ)
        length = MAX_READ;
    uint8_t* buf = (uint8_t*)malloc((size_t)length);
    if (!buf) {
        const char* msg = "{\"error\":\"内存不足\"}";
        proto_write_frame((uint32_t)id | PROTO_ERROR_FLAG, (const uint8_t*)msg, (uint32_t)strlen(msg));
        return;
    }
    const int64_t got = track_reader_read(tr, (uint64_t)offset, (uint32_t)length, buf);
    proto_write_frame((uint32_t)id, buf, (uint32_t)(got > 0 ? got : 0));
    free(buf);
}

int main(void) {
    if (proto_guard_stdout() != 0) {
        fprintf(stderr, "[sacd-helper] stdout 保险闸装不上，退出\n");
        return 1;
    }
    // 保险闸自检：MD_SACD_STDOUT_TEST=1 时故意用最常见的三种方式往 stdout 写字。
    // 装了闸之后它们全部落到 stderr，帧流一个字节都不受影响——这条路径存在的意义
    // 就是让「库函数误写 stdout」这件事有一个可复现的实测，而不是靠口头保证。
    if (getenv("MD_SACD_STDOUT_TEST")) {
        printf("printf 直写 stdout：这行不该出现在帧流里\n");
        fprintf(stdout, "fprintf(stdout) ：这行也不该出现在帧流里\n");
        fputs("fputs(stdout)   ：这行同样不该出现在帧流里\n", stdout);
        fflush(stdout);
        const char* raw = "write(1) 裸写    ：连它也不该出现在帧流里\n";
        (void)!write(1, raw, strlen(raw));
    }

    session_t s;
    memset(&s, 0, sizeof(s));
    s.tr_area = -1;
    s.tr_track = -1;

    char* line = (char*)malloc(MAX_LINE);
    if (!line)
        return 1;
    while (fgets(line, MAX_LINE, stdin)) {
        long long id = 0;
        json_int(line, "id", &id);
        char cmd[32] = {0};
        if (!json_str(line, "cmd", cmd, sizeof(cmd))) {
            reply_error(id, "请求里没有 cmd");
            continue;
        }
        if (strcmp(cmd, "open") == 0) {
            char path[4096] = {0};
            if (!json_str(line, "path", path, sizeof(path))) {
                reply_error(id, "open 缺 path");
                continue;
            }
            cmd_open(&s, id, path);
        } else if (strcmp(cmd, "stat") == 0 || strcmp(cmd, "read") == 0) {
            long long area = 0, track = 1, offset = 0, length = 0;
            json_int(line, "area", &area);
            json_int(line, "track", &track);
            if (!s.sb) {
                reply_error(id, "还没有打开镜像");
                continue;
            }
            if (strcmp(cmd, "stat") == 0) {
                cmd_stat(&s, id, (int)area, (int)track);
            } else {
                json_int(line, "offset", &offset);
                json_int(line, "length", &length);
                cmd_read(&s, id, (int)area, (int)track, offset, length);
            }
        } else if (strcmp(cmd, "close") == 0 || strcmp(cmd, "quit") == 0) {
            char buf[64];
            const int n = snprintf(buf, sizeof(buf), "{\"id\":%lld,\"ok\":true}", id);
            proto_write_json(buf, (size_t)n);
            if (strcmp(cmd, "quit") == 0)
                break;
        } else {
            reply_error(id, "不认识的 cmd");
        }
    }
    free(line);
    if (s.tr)
        track_reader_close(s.tr);
    if (s.sb)
        scarletbook_close(s.sb);
    if (s.reader)
        sacd_close(s.reader);
    return 0;
}
