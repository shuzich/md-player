#include "dsf_view.h"

#include <string.h>

static void put_le32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void put_le64(uint8_t* p, uint64_t v) {
    put_le32(p, (uint32_t)(v & 0xffffffffu));
    put_le32(p + 4, (uint32_t)(v >> 32));
}

void dsf_view_init(dsf_view_t* v, uint32_t channels, uint32_t duration_frames) {
    memset(v, 0, sizeof(*v));
    v->channels = channels;
    v->duration_frames = duration_frames;
    v->bytes_per_channel = (uint64_t)duration_frames * SACD_FRAME_BYTES_PER_CHANNEL;
    v->samples_per_channel = v->bytes_per_channel * 8u;
    v->blocks = (v->bytes_per_channel + DSF_BLOCK_PER_CHANNEL - 1) / DSF_BLOCK_PER_CHANNEL;
    v->data_size = v->blocks * DSF_BLOCK_PER_CHANNEL * channels;
    v->total_size = DSF_HEADER_SIZE + v->data_size;
}

void dsf_view_header(const dsf_view_t* v, uint8_t out[DSF_HEADER_SIZE]) {
    memset(out, 0, DSF_HEADER_SIZE);

    // DSD chunk（28 字节）
    memcpy(out + 0, "DSD ", 4);
    put_le64(out + 4, 28);
    put_le64(out + 12, v->total_size);
    put_le64(out + 20, 0); // 无 metadata

    // fmt chunk（52 字节）
    memcpy(out + 28, "fmt ", 4);
    put_le64(out + 32, 52);
    put_le32(out + 40, 1); // format version
    put_le32(out + 44, 0); // format id = DSD raw
    // channel type: 2ch=2(stereo), 5ch=6, 6ch=7；其余按声道数填 0 交给播放器自判
    put_le32(out + 48, v->channels == 2 ? 2u : (v->channels == 5 ? 6u : (v->channels == 6 ? 7u : 0u)));
    put_le32(out + 52, v->channels);
    put_le32(out + 56, SACD_FRAME_BYTES_PER_CHANNEL * SACD_FRAMES_PER_SECOND * 8u); // 2822400
    put_le32(out + 60, 1);                                                          // 1 bit / sample
    put_le64(out + 64, v->samples_per_channel);
    put_le32(out + 72, DSF_BLOCK_PER_CHANNEL);
    put_le32(out + 76, 0); // reserved

    // data chunk（12 字节头）
    memcpy(out + 80, "data", 4);
    put_le64(out + 84, 12 + v->data_size);
}
