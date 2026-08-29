// 确定性 DSF 视图（ARCHITECTURE §SACD 协议 v1：stat 必须能预先精确算出 dsf_size）。
//
// 视图完全由「碟内声明的曲目时长 + 声道数」推出，不依赖实际解出多少字节：
//   每 SACD 帧 = 1/75 秒 = 每声道 4704 字节（588 样本 × 64fs ÷ 8）
//   每声道字节数 = 时长帧数 × 4704
//   DSF 数据区按 4096 字节/声道 分块，不足补 0x00
// 于是同一 (area,track) 任何时候 stat 出来都是同一个数，read 也逐字节可复现。
#ifndef MD_SACD_DSF_VIEW_H
#define MD_SACD_DSF_VIEW_H

#include <stdint.h>

#define DSF_HEADER_SIZE 92u
#define DSF_BLOCK_PER_CHANNEL 4096u
#define SACD_FRAME_BYTES_PER_CHANNEL 4704u // FRAME_SIZE_64
#define SACD_FRAMES_PER_SECOND 75u

typedef struct {
    uint32_t channels;
    uint32_t duration_frames;     // 碟内声明的时长，单位 1/75 秒
    uint64_t bytes_per_channel;   // duration_frames * 4704
    uint64_t samples_per_channel; // bytes_per_channel * 8
    uint64_t blocks;              // ceil(bytes_per_channel / 4096)
    uint64_t data_size;           // blocks * 4096 * channels（含补零）
    uint64_t total_size;          // 92 + data_size
} dsf_view_t;

void dsf_view_init(dsf_view_t* v, uint32_t channels, uint32_t duration_frames);
// 把 92 字节头写进 out（小端，DSF 规范）。
void dsf_view_header(const dsf_view_t* v, uint8_t out[DSF_HEADER_SIZE]);

#endif
