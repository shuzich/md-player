#include "track_reader.h"

#include "dst_fram.h"
#include "dst_init.h"
#include "scarletbook.h"
#include "scarletbook_read.h"
#include "shim/logging.h"
#include "shim/sacd_reader.h"

#include <stdlib.h>
#include <string.h>

#define SECTORS_PER_READ 64u
#define SCAN_SECTORS 512u
#define FRAME_CACHE 2 // 一个 DSF 超块最多跨两个 SACD 帧（4096 < 4704）

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

// 每个 SACD 帧解出来恰好是 channels × 4704 字节，且帧头自带绝对时间码，
// 于是「帧号 → 该帧在 DSF 数据区的字节区间」是纯算术，不需要边解码边数。
// 索引里只存「帧号 → 起始 LSN」，回退 seek 就成了「跳到那个扇区、解一两帧」。
typedef struct {
    uint32_t* lsn; // lsn[rel] = 该帧起始扇区；0xFFFFFFFF 表示未知
    uint32_t cap;
    uint32_t known; // 已知条目里最大的 rel + 1
    uint32_t scan_lsn;
    int scan_done;
} frame_index_t;

// 一个 DSF 超块最多跨两个 SACD 帧（4096 < 4704），所以只需要两个槽。
// 关键：槽是**定向**的——每次组块前先声明「这一块要哪两帧」，解码回调只收这两帧，
// 其余帧连 DST 都不解。早先用环形缓存吃过亏：一次 process_frames 会一口气吐出
// 二十来帧，目标帧转眼就被挤掉，结果整块补零、read 出来全是静音。
typedef struct {
    int64_t rel;   // 槽里装的轨内帧号；-1 = 空
    uint8_t* data; // channels × 4704，已翻转比特的 muxed 字节
} frame_slot_t;

struct track_reader_s {
    scarletbook_handle_t* sb;
    sacd_reader_t* reader;
    dsf_view_t view;

    uint32_t start_lsn;
    uint32_t end_lsn;
    uint32_t track_start_frames; // 轨首的绝对时间码，用来把帧时间码换成轨内帧号
    int dst_encoded;

    ebunch dst;
    int dst_open;

    uint8_t* sector_buf;
    uint8_t* dsd_buf;
    uint8_t* block;

    frame_index_t idx;
    frame_slot_t slot[FRAME_CACHE];
    int64_t want[FRAME_CACHE];

    // 生产者状态：正在顺序解码，下一帧预计是 expect_rel
    uint32_t cur_lsn;
    int64_t expect_rel;
    int running;

    uint64_t block_index; // block 里装的是第几个超块
    int block_valid;
};

// ---- 帧索引：只读扇区头与包头，不解码 ----

static void idx_put(frame_index_t* ix, uint32_t rel, uint32_t lsn) {
    if (rel >= ix->cap) {
        uint32_t cap = ix->cap ? ix->cap : 1024;
        while (cap <= rel)
            cap *= 2;
        uint32_t* p = (uint32_t*)realloc(ix->lsn, (size_t)cap * sizeof(uint32_t));
        if (!p)
            return;
        memset(p + ix->cap, 0xFF, (size_t)(cap - ix->cap) * sizeof(uint32_t));
        ix->lsn = p;
        ix->cap = cap;
    }
    if (ix->lsn[rel] == 0xFFFFFFFFu)
        ix->lsn[rel] = lsn;
    if (rel + 1 > ix->known)
        ix->known = rel + 1;
}

// 扫一个扇区的头：数出其中每个「音频包 + frame_start」，把对应帧的起始 LSN 记下。
// 只碰 header/packet_info/frame_info 这几十个字节，不碰负载，更不解码。
static void scan_sector(track_reader_t* tr, const uint8_t* s, uint32_t lsn) {
    const uint8_t h = s[0];
    const int dst_encoded = h & 1;
    const int frame_info_count = (h >> 2) & 7;
    const int packet_info_count = (h >> 5) & 7;
    if (packet_info_count == 0 || packet_info_count > 7)
        return;

    const uint8_t* pk = s + 1;
    const uint8_t* fi = pk + (size_t)packet_info_count * 2;
    const int fi_size = dst_encoded ? 4 : 3;
    if (fi + (size_t)frame_info_count * fi_size > s + SACD_LSN_SIZE)
        return;

    int fi_idx = 0;
    for (int i = 0; i < packet_info_count; i++) {
        const int frame_start = (pk[i * 2] >> 7) & 1;
        const int data_type = (pk[i * 2] >> 3) & 7;
        if (data_type != DATA_TYPE_AUDIO || !frame_start)
            continue;
        if (fi_idx >= frame_info_count)
            break;
        const uint8_t* t = fi + (size_t)fi_idx * fi_size;
        const uint32_t abs = (uint32_t)t[0] * 60u * SACD_FRAME_RATE + (uint32_t)t[1] * SACD_FRAME_RATE + t[2];
        fi_idx++;
        if (abs < tr->track_start_frames)
            continue;
        idx_put(&tr->idx, abs - tr->track_start_frames, lsn);
    }
}

// 把索引推进到至少覆盖帧 rel。增量：每次只从上次扫到的地方往后扫，不做全量预扫。
static int index_ensure(track_reader_t* tr, uint32_t rel) {
    frame_index_t* ix = &tr->idx;
    while (rel >= ix->known && !ix->scan_done) {
        if (ix->scan_lsn >= tr->end_lsn) {
            ix->scan_done = 1;
            break;
        }
        uint32_t want = SCAN_SECTORS;
        if (ix->scan_lsn + want > tr->end_lsn)
            want = tr->end_lsn - ix->scan_lsn;
        const uint32_t got = sacd_read_block_raw(tr->reader, ix->scan_lsn, want, tr->sector_buf);
        if (got == 0) {
            ix->scan_done = 1;
            break;
        }
        for (uint32_t i = 0; i < got; i++)
            scan_sector(tr, tr->sector_buf + (size_t)i * SACD_LSN_SIZE, ix->scan_lsn + i);
        ix->scan_lsn += got;
    }
    return (rel < ix->known && ix->lsn[rel] != 0xFFFFFFFFu) ? 0 : -1;
}

// ---- 解码生产者 ----

static uint8_t* slot_find(track_reader_t* tr, int64_t rel) {
    for (int i = 0; i < FRAME_CACHE; i++)
        if (tr->slot[i].rel == rel)
            return tr->slot[i].data;
    return NULL;
}

// 声明本块要的两帧，并把已在槽里的那帧挪到对的位置（避免顺序播放时反复重解）。
static void set_wants(track_reader_t* tr, int64_t f0, int64_t f1) {
    tr->want[0] = f0;
    tr->want[1] = f1;
    // 已经装着 want[1] 的槽如果排在前面，就和另一个槽换个位置，让 want[0] 有地方放。
    if (tr->slot[0].rel == f1 && tr->slot[1].rel != f1 && f0 != f1) {
        frame_slot_t t = tr->slot[0];
        tr->slot[0] = tr->slot[1];
        tr->slot[1] = t;
    }
    for (int i = 0; i < FRAME_CACHE; i++)
        if (tr->slot[i].rel != tr->want[0] && tr->slot[i].rel != tr->want[1])
            tr->slot[i].rel = -1; // 用不上的腾空
}

static int wants_satisfied(track_reader_t* tr) {
    return slot_find(tr, tr->want[0]) != NULL && slot_find(tr, tr->want[1]) != NULL;
}

static void on_frame(scarletbook_handle_t* handle, uint8_t* data, size_t size, void* userdata) {
    track_reader_t* tr = (track_reader_t*)userdata;
    const uint32_t ch = tr->view.channels;
    const uint32_t abs = TIME_FRAMECOUNT(&handle->frame.timecode);
    if (abs < tr->track_start_frames)
        return;
    const int64_t rel = (int64_t)abs - (int64_t)tr->track_start_frames;
    tr->expect_rel = rel + 1;

    // 只收本块点名的两帧。别的帧连 DST 都不解——跳过去比解出来再丢快得多。
    if (rel != tr->want[0] && rel != tr->want[1])
        return;
    if (slot_find(tr, rel))
        return;
    int target = -1;
    for (int i = 0; i < FRAME_CACHE && target < 0; i++)
        if (tr->slot[i].rel == -1)
            target = i;
    if (target < 0)
        return;

    const uint8_t* src = data;
    size_t n = size;
    if (handle->frame.dst_encoded) {
        const int rc = DST_FramDSTDecode(data, tr->dsd_buf, (int)size, (int)rel, &tr->dst);
        if (rc != 0) {
            LOG(lm_main, LOG_ERROR, ("DST 解码失败(帧 %lld): %s", (long long)rel, DST_GetErrorMessage(rc)));
            return;
        }
        src = tr->dsd_buf;
        n = (size_t)ch * SACD_FRAME_BYTES_PER_CHANNEL;
    }
    const size_t full = (size_t)ch * SACD_FRAME_BYTES_PER_CHANNEL;
    if (n > full)
        n = full;

    uint8_t* dstbuf = tr->slot[target].data;
    for (size_t i = 0; i < n; i++)
        dstbuf[i] = g_bitrev[src[i]];
    if (n < full)
        memset(dstbuf + n, 0x00, full - n);
    tr->slot[target].rel = rel;
}

// 把 want[] 里的帧都拿到手。能顺着解就顺着解，不能就靠索引跳过去。
static int fetch_wants(track_reader_t* tr) {
    if (wants_satisfied(tr))
        return 0;
    const int64_t first = tr->want[0];
    if (first < 0 || (uint64_t)first * SACD_FRAME_BYTES_PER_CHANNEL >= tr->view.bytes_per_channel)
        return -1;

    // 目标在当前解码位置之前，或还没起步，或隔得太远 —— 用索引直接跳过去
    if (!tr->running || first < tr->expect_rel || first > tr->expect_rel + 64) {
        if (index_ensure(tr, (uint32_t)first) != 0)
            return -1;
        scarletbook_frame_init(tr->sb);
        tr->cur_lsn = tr->idx.lsn[first];
        tr->expect_rel = first;
        tr->running = 1;
    }

    int guard = 0;
    while (!wants_satisfied(tr)) {
        if (tr->cur_lsn >= tr->end_lsn)
            return slot_find(tr, tr->want[0]) ? 0 : -1;
        uint32_t want_sectors = SECTORS_PER_READ;
        if (tr->cur_lsn + want_sectors > tr->end_lsn)
            want_sectors = tr->end_lsn - tr->cur_lsn;
        const uint32_t got = sacd_read_block_raw(tr->reader, tr->cur_lsn, want_sectors, tr->sector_buf);
        if (got == 0)
            return slot_find(tr, tr->want[0]) ? 0 : -1;
        tr->cur_lsn += got;
        scarletbook_process_frames(tr->sb, tr->sector_buf, (int)got, tr->cur_lsn >= tr->end_lsn, on_frame, tr);
        if (++guard > 100000)
            return -1;
    }
    return 0;
}

// 组装第 s 个 DSF 超块：它覆盖每声道字节 [s*4096, s*4096+4096)，
// 最多跨两个 SACD 帧。缺的部分保持 0x00（D-038 的补零）。
static int build_block(track_reader_t* tr, uint64_t s) {
    if (tr->block_valid && tr->block_index == s)
        return 0;
    const uint32_t ch = tr->view.channels;
    const uint64_t lo = s * DSF_BLOCK_PER_CHANNEL;
    const uint64_t hi = lo + DSF_BLOCK_PER_CHANNEL;
    memset(tr->block, 0x00, (size_t)ch * DSF_BLOCK_PER_CHANNEL);

    const int64_t f0 = (int64_t)(lo / SACD_FRAME_BYTES_PER_CHANNEL);
    const int64_t f1 = (int64_t)((hi - 1) / SACD_FRAME_BYTES_PER_CHANNEL);
    set_wants(tr, f0, f1);
    fetch_wants(tr); // 拿不全也不让整块失败，缺的部分就是补零
    for (int64_t f = f0; f <= f1; f++) {
        const uint8_t* muxed = slot_find(tr, f);
        if (!muxed)
            continue;
        const uint64_t base = (uint64_t)f * SACD_FRAME_BYTES_PER_CHANNEL;
        uint64_t b_lo = 0, b_hi = SACD_FRAME_BYTES_PER_CHANNEL;
        if (base < lo)
            b_lo = lo - base;
        if (base + b_hi > hi)
            b_hi = hi - base;
        for (uint64_t b = b_lo; b < b_hi; b++) {
            const uint64_t g = base + b - lo; // 块内的每声道偏移
            for (uint32_t c = 0; c < ch; c++)
                tr->block[(uint64_t)c * DSF_BLOCK_PER_CHANNEL + g] = muxed[b * ch + c];
        }
    }
    tr->block_index = s;
    tr->block_valid = 1;
    return 0;
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
    tr->end_lsn = tr->start_lsn + area->area_tracklist_offset->track_length_lsn[track];
    tr->track_start_frames = TIME_FRAMECOUNT(&area->area_tracklist_time->start[track]);
    tr->dst_encoded = (area->area_toc->frame_format == FRAME_FORMAT_DST);

    const uint32_t ch = area->area_toc->channel_count;
    const uint32_t frames = TIME_FRAMECOUNT(&area->area_tracklist_time->duration[track]);
    dsf_view_init(&tr->view, ch, frames);

    tr->sector_buf =
        (uint8_t*)malloc((size_t)(SECTORS_PER_READ > SCAN_SECTORS ? SECTORS_PER_READ : SCAN_SECTORS) * SACD_LSN_SIZE);
    tr->dsd_buf = (uint8_t*)malloc((size_t)ch * SACD_FRAME_BYTES_PER_CHANNEL);
    tr->block = (uint8_t*)malloc((size_t)ch * DSF_BLOCK_PER_CHANNEL);
    int ok = tr->sector_buf && tr->dsd_buf && tr->block;
    for (int i = 0; i < FRAME_CACHE; i++) {
        tr->slot[i].rel = -1;
        tr->want[i] = -1;
        tr->slot[i].data = (uint8_t*)malloc((size_t)ch * SACD_FRAME_BYTES_PER_CHANNEL);
        ok = ok && tr->slot[i].data != NULL;
    }
    if (!ok) {
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
    tr->idx.scan_lsn = tr->start_lsn;
    tr->expect_rel = -1;
    tr->block_valid = 0;
    return tr;
}

void track_reader_close(track_reader_t* tr) {
    if (!tr)
        return;
    if (tr->dst_open)
        DST_CloseDecoder(&tr->dst);
    for (int i = 0; i < FRAME_CACHE; i++)
        free(tr->slot[i].data);
    free(tr->idx.lsn);
    free(tr->sector_buf);
    free(tr->dsd_buf);
    free(tr->block);
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
        if (build_block(tr, dd / block_bytes) != 0)
            break;
        const uint64_t within = dd % block_bytes;
        uint64_t n = block_bytes - within;
        if (n > remain - done)
            n = remain - done;
        memcpy(out + done, tr->block + within, (size_t)n);
        done += n;
        offset += n;
    }
    return (int64_t)done;
}
