#include "track_reader.h"

#include "dst_fram.h"
#include "dst_init.h"
#include "scarletbook.h"
#include "scarletbook_read.h"
#include "shim/logging.h"
#include "shim/sacd_reader.h"

#include <stdlib.h>
#include <string.h>

#define SECTORS_PER_READ 32u

// 碟内 DSD 是 MSB first，DSF 规范要 LSB first，逐字节翻转。
static uint8_t g_bitrev[256];
static int g_bitrev_ready = 0;

static void bitrev_init(void) {
    if (g_bitrev_ready)
        return;
    for (int i = 0; i < 256; i++) {
        unsigned b = 0;
        for (int k = 0; k < 8; k++)
            if (i & (1 << k))
                b |= 1u << (7 - k);
        g_bitrev[i] = (uint8_t)b;
    }
    g_bitrev_ready = 1;
}

// 一次 process_frames 会吐出若干帧，帧边界与 DSF 超块边界不对齐，所以中间摆一个
// 会长大的 pending 缓冲：回调只管往里塞，超块的切分交给 seek_block 去排。
typedef struct {
    uint8_t* buf;
    size_t cap;
    size_t len;
    size_t pos;
} pending_t;

static int pending_push(pending_t* p, const uint8_t* src, size_t n) {
    if (p->len + n > p->cap) {
        size_t cap = p->cap ? p->cap : 65536;
        while (cap < p->len + n)
            cap *= 2;
        uint8_t* nb = (uint8_t*)realloc(p->buf, cap);
        if (!nb)
            return -1;
        p->buf = nb;
        p->cap = cap;
    }
    memcpy(p->buf + p->len, src, n);
    p->len += n;
    return 0;
}

static void pending_reset(pending_t* p) {
    p->len = 0;
    p->pos = 0;
}

struct track_reader_s {
    scarletbook_handle_t* sb;
    sacd_reader_t* reader;
    dsf_view_t view;

    uint32_t start_lsn;
    uint32_t length_lsn;
    int dst_encoded;

    ebunch dst; // DST 解码器状态（单线程，一帧一调用）
    int dst_open;
    int frame_no;

    uint8_t* sector_buf; // SECTORS_PER_READ * 2048
    uint8_t* dsd_buf;    // DST 解码输出：channels * 4704
    uint8_t* mux;        // 当前超块的 muxed 字节：channels * 4096
    uint8_t* block;      // 转置后的 DSF 超块：channels * 4096
    pending_t pending;

    uint64_t sb_index; // block/mux 对应的超块序号
    int block_valid;
    uint64_t mux_fill;
    uint32_t cur_lsn;
    uint64_t ch_bytes; // 已产出的每声道字节数，用来按声明时长封顶
    int exhausted;
};

static void on_frame(scarletbook_handle_t* handle, uint8_t* data, size_t size, void* userdata) {
    track_reader_t* tr = (track_reader_t*)userdata;
    const uint32_t ch = tr->view.channels;
    const uint8_t* src = data;
    size_t n = size;

    if (handle->frame.dst_encoded) {
        const int rc = DST_FramDSTDecode(data, tr->dsd_buf, (int)size, tr->frame_no, &tr->dst);
        if (rc != 0) {
            LOG(lm_main, LOG_ERROR, ("DST 解码失败(帧 %d): %s", tr->frame_no, DST_GetErrorMessage(rc)));
            return;
        }
        src = tr->dsd_buf;
        n = (size_t)ch * SACD_FRAME_BYTES_PER_CHANNEL;
    }
    tr->frame_no++;

    // 就地翻转比特再入队，后面全程不用再碰位序。
    uint8_t tmp[4096];
    size_t off = 0;
    while (off < n) {
        size_t chunk = n - off;
        if (chunk > sizeof(tmp))
            chunk = sizeof(tmp);
        for (size_t i = 0; i < chunk; i++)
            tmp[i] = g_bitrev[src[off + i]];
        if (pending_push(&tr->pending, tmp, chunk) != 0)
            return;
        off += chunk;
    }
}

static void transpose(track_reader_t* tr) {
    const uint32_t ch = tr->view.channels;
    memset(tr->block, 0x00, (size_t)ch * DSF_BLOCK_PER_CHANNEL);
    for (uint64_t k = 0; k < tr->mux_fill; k++) {
        const uint32_t c = (uint32_t)(k % ch);
        const uint64_t b = k / ch;
        tr->block[(uint64_t)c * DSF_BLOCK_PER_CHANNEL + b] = tr->mux[k];
    }
    tr->block_valid = 1;
}

static void rewind_track(track_reader_t* tr) {
    scarletbook_frame_init(tr->sb);
    pending_reset(&tr->pending);
    tr->cur_lsn = tr->start_lsn;
    tr->sb_index = 0;
    tr->mux_fill = 0;
    tr->ch_bytes = 0;
    tr->block_valid = 0;
    tr->exhausted = 0;
    tr->frame_no = 0;
}

// 把生产者推进到超块 target，返回后 tr->block 里就是它。
static int seek_block(track_reader_t* tr, uint64_t target) {
    if (tr->block_valid && tr->sb_index == target)
        return 0;
    if (target < tr->sb_index || !tr->block_valid)
        rewind_track(tr);

    const uint32_t ch = tr->view.channels;
    const uint64_t mux_cap = (uint64_t)ch * DSF_BLOCK_PER_CHANNEL;
    const uint32_t end_lsn = tr->start_lsn + tr->length_lsn;

    for (;;) {
        // 1) 先把 pending 里的字节倒进当前超块
        while (tr->mux_fill < mux_cap && tr->pending.pos < tr->pending.len) {
            if (tr->ch_bytes >= tr->view.bytes_per_channel) {
                pending_reset(&tr->pending); // 已到声明时长，后面的帧不要了
                tr->exhausted = 1;
                break;
            }
            tr->mux[tr->mux_fill++] = tr->pending.buf[tr->pending.pos++];
            if ((tr->mux_fill % ch) == 0)
                tr->ch_bytes++;
        }
        if (tr->pending.pos == tr->pending.len)
            pending_reset(&tr->pending);

        // 2) 超块满了（或数据到头）就结账
        if (tr->mux_fill == mux_cap || (tr->exhausted && tr->mux_fill > 0)) {
            if (tr->sb_index == target) {
                transpose(tr);
                return 0;
            }
            tr->sb_index++;
            tr->mux_fill = 0;
            tr->block_valid = 0;
            continue;
        }
        // 3) 数据到头且当前超块空：目标之后全是补零区
        if (tr->exhausted) {
            if (tr->sb_index <= target) {
                tr->sb_index = target;
                tr->mux_fill = 0;
                transpose(tr);
                return 0;
            }
            return -1;
        }
        // 4) 再读一批扇区喂给 vendored 的帧装配器
        if (tr->cur_lsn >= end_lsn) {
            tr->exhausted = 1;
            continue;
        }
        uint32_t want = SECTORS_PER_READ;
        if (tr->cur_lsn + want > end_lsn)
            want = end_lsn - tr->cur_lsn;
        const uint32_t got = sacd_read_block_raw(tr->reader, tr->cur_lsn, want, tr->sector_buf);
        if (got == 0) {
            tr->exhausted = 1;
            continue;
        }
        tr->cur_lsn += got;
        scarletbook_process_frames(tr->sb, tr->sector_buf, (int)got, tr->cur_lsn >= end_lsn, on_frame, tr);
    }
}

track_reader_t* track_reader_open(void* sb_handle, void* sacd_reader, int area_idx, int track) {
    bitrev_init();
    scarletbook_handle_t* sb = (scarletbook_handle_t*)sb_handle;
    if (!sb || area_idx < 0 || area_idx >= sb->area_count)
        return NULL;
    scarletbook_area_t* area = &sb->area[area_idx];
    if (!area->area_toc || !area->area_tracklist_offset || !area->area_tracklist_time)
        return NULL;
    if (track < 0 || track >= area->area_toc->track_count)
        return NULL;

    track_reader_t* tr = (track_reader_t*)calloc(1, sizeof(*tr));
    if (!tr)
        return NULL;
    tr->sb = sb;
    tr->reader = (sacd_reader_t*)sacd_reader;
    tr->start_lsn = area->area_tracklist_offset->track_start_lsn[track];
    tr->length_lsn = area->area_tracklist_offset->track_length_lsn[track];
    tr->dst_encoded = (area->area_toc->frame_format == FRAME_FORMAT_DST);

    const uint32_t ch = area->area_toc->channel_count;
    const uint32_t frames = TIME_FRAMECOUNT(&area->area_tracklist_time->duration[track]);
    dsf_view_init(&tr->view, ch, frames);

    tr->sector_buf = (uint8_t*)malloc(SECTORS_PER_READ * SACD_LSN_SIZE);
    tr->dsd_buf = (uint8_t*)malloc((size_t)ch * SACD_FRAME_BYTES_PER_CHANNEL);
    tr->mux = (uint8_t*)malloc((size_t)ch * DSF_BLOCK_PER_CHANNEL);
    tr->block = (uint8_t*)malloc((size_t)ch * DSF_BLOCK_PER_CHANNEL);
    if (!tr->sector_buf || !tr->dsd_buf || !tr->mux || !tr->block) {
        track_reader_close(tr);
        return NULL;
    }
    if (tr->dst_encoded) {
        // 单线程：直接用帧级 API，不启用上游的多线程调度层（D-033 偏离点）。
        if (DST_InitDecoder(&tr->dst, (int)ch, 64) != 0) {
            track_reader_close(tr);
            return NULL;
        }
        tr->dst_open = 1;
    }
    rewind_track(tr);
    return tr;
}

void track_reader_close(track_reader_t* tr) {
    if (!tr)
        return;
    if (tr->dst_open)
        DST_CloseDecoder(&tr->dst);
    free(tr->sector_buf);
    free(tr->dsd_buf);
    free(tr->mux);
    free(tr->block);
    free(tr->pending.buf);
    free(tr);
}

const dsf_view_t* track_reader_view(const track_reader_t* tr) {
    return &tr->view;
}

int64_t track_reader_read(track_reader_t* tr, uint64_t offset, uint32_t length, uint8_t* out) {
    if (!tr || offset >= tr->view.total_size)
        return 0;
    uint64_t remain = tr->view.total_size - offset;
    if (remain > length)
        remain = length;

    uint64_t done = 0;
    if (offset < DSF_HEADER_SIZE) {
        uint8_t hdr[DSF_HEADER_SIZE];
        dsf_view_header(&tr->view, hdr);
        uint64_t n = DSF_HEADER_SIZE - offset;
        if (n > remain)
            n = remain;
        memcpy(out, hdr + offset, (size_t)n);
        done += n;
        offset += n;
    }
    const uint64_t block_bytes = (uint64_t)tr->view.channels * DSF_BLOCK_PER_CHANNEL;
    while (done < remain) {
        const uint64_t dd = offset - DSF_HEADER_SIZE;
        const uint64_t s = dd / block_bytes;
        const uint64_t within = dd % block_bytes;
        if (seek_block(tr, s) != 0)
            break;
        uint64_t n = block_bytes - within;
        if (n > remain - done)
            n = remain - done;
        memcpy(out + done, tr->block + within, (size_t)n);
        done += n;
        offset += n;
    }
    return (int64_t)done;
}
