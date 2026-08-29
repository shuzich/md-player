// 曲目 → DSF 字节流。
//
// 碟上的音频是「按扇区打包的帧」，DSF 要的是「按 4096 字节/声道 分块交织」，
// 两者之间是一次纯排列 + 一次比特翻转（碟内 MSB first，DSF 规范要 LSB first）。
// 中间态称作 muxed 流：逐字节按声道轮转（ch0,ch1,ch0,ch1,…），这正是
// vendored 的 scarletbook_process_frames() 回调吐出来的形态，DST 解码器
// DST_FramDSTDecode() 的输出也是同一形态，所以 DSD 与 DST 只在「怎么拿到 muxed」
// 这一步分叉，往后完全同构（D-037）。
#ifndef MD_SACD_TRACK_READER_H
#define MD_SACD_TRACK_READER_H

#include "dsf_view.h"

#include <stdint.h>

typedef struct track_reader_s track_reader_t;
struct scarletbook_handle_s;

// area_idx 是 handle->area[] 的下标；track 从 0 起。
track_reader_t* track_reader_open(void* sb_handle, void* sacd_reader, int area_idx, int track);
void track_reader_close(track_reader_t* tr);

const dsf_view_t* track_reader_view(const track_reader_t* tr);
// 读 DSF 视图的 [offset, offset+length)，写进 out，返回实际字节数（到尾即短读）。
// 允许任意 offset：向前跳靠跳过解码，向后跳靠从头重来（协议 v1 明确允许追赶）。
int64_t track_reader_read(track_reader_t* tr, uint64_t offset, uint32_t length, uint8_t* out);

#endif
