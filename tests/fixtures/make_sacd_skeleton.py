#!/usr/bin/env python3
"""造一张最小的 SACD 结构骨架镜像（M1-PLAN T6 阶段 1）。

只写卷描述符与 Scarlet Book 的 TOC 结构，**不含任何音频内容**——音频区留空，
helper 读到的就是补零。用途是让识别、TOC 解析、DSF 视图尺寸计算、补零路径这几条
在没有真碟的环境（比如 CI）里也能跑，真碟一律走 MD_TEST_MEDIA（CLAUDE.md 深坑 #6）。

结构偏移取自 vendored 的 scarletbook.h（offsetof 实测核对过，见 T6 阶段 1 汇报）。

    python3 tests/fixtures/make_sacd_skeleton.py tests/fixtures/generated
"""
import os
import struct
import sys

SECTOR = 2048
MASTER_TOC_LSN = 510
AREA_TOC_LSN = 544
AREA_TOC_2_LSN = 560
AREA_TOC_SECTORS = 8
AUDIO_START_LSN = 600
TOTAL_SECTORS = 700

# 曲目时长用 1/75 秒帧计；两条轨，够验证边界又不至于让文件变大。
TRACKS = [
    {"start_frames": 0, "frames": 150, "start_lsn": AUDIO_START_LSN, "length_lsn": 20},
    {"start_frames": 150, "frames": 75, "start_lsn": AUDIO_START_LSN + 20, "length_lsn": 10},
]
CHANNELS = 2
FRAME_FORMAT_DSD_3_IN_14 = 2


def iso9660_pvd(total_sectors):
    """一个刚好能过 T5 完整性校验的主卷描述符：声明的卷大小 = 文件实际大小。"""
    s = bytearray(SECTOR)
    s[0] = 1
    s[1:6] = b"CD001"
    s[6] = 1
    s[40:72] = b"MD-SACD-SKELETON".ljust(32)
    s[80:84] = struct.pack("<I", total_sectors)
    s[84:88] = struct.pack(">I", total_sectors)
    s[128:130] = struct.pack("<H", SECTOR)
    s[130:132] = struct.pack(">H", SECTOR)
    return bytes(s)


def master_toc():
    s = bytearray(SECTOR)
    s[0:8] = b"SACDMTOC"
    s[8] = 1
    s[9] = 0x14  # 1.20
    s[16:18] = struct.pack(">H", 1)  # album_set_size
    s[18:20] = struct.pack(">H", 1)  # album_sequence_number
    s[24:40] = b"MDSKEL0000000001"
    s[64:68] = struct.pack(">I", AREA_TOC_LSN)    # area_1_toc_1_start
    s[68:72] = struct.pack(">I", AREA_TOC_2_LSN)  # area_1_toc_2_start
    s[72:76] = struct.pack(">I", 0)               # 无多声道区
    s[76:80] = struct.pack(">I", 0)
    s[84:86] = struct.pack(">H", AREA_TOC_SECTORS)
    s[86:88] = struct.pack(">H", 0)
    s[128] = 0  # text_area_count：骨架不带文本，避免任何版权内容
    s[136:138] = b"en"
    s[138] = 1  # ISO 646
    return bytes(s)


def sacd_text():
    """8 个 SACDText 扇区是 scarletbook_read_master_toc() 的硬要求（少一个直接判定
    不是 SACD 碟）。骨架里全部位置字段留 0 = 没有任何文本，天然零版权内容。"""
    s = bytearray(SECTOR)
    s[0:8] = b"SACDText"
    return bytes(s)


def sacd_man():
    """紧跟 8 个 SACDText 之后的制造信息扇区，同样是硬要求。"""
    s = bytearray(SECTOR)
    s[0:8] = b"SACD_Man"
    return bytes(s)


def area_toc():
    total = sum(t["frames"] for t in TRACKS)
    s = bytearray(SECTOR)
    s[0:8] = b"TWOCHTOC"
    s[8] = 1
    s[9] = 0x14
    s[10:12] = struct.pack(">H", AREA_TOC_SECTORS)
    s[16:20] = struct.pack(">I", 716800)          # max_byte_rate
    s[20] = 0x04                                   # 64fs
    s[21] = FRAME_FORMAT_DSD_3_IN_14 & 0x0F        # Area_Flags 低 4 位 = frame_format
    s[32] = CHANNELS
    s[34] = CHANNELS                               # max_available_channels
    s[64] = total // (60 * 75)
    s[65] = (total // 75) % 60
    s[66] = total % 75
    s[68] = 0                                      # track_offset
    s[69] = len(TRACKS)
    s[72:76] = struct.pack(">I", AUDIO_START_LSN)
    s[76:80] = struct.pack(">I", AUDIO_START_LSN + sum(t["length_lsn"] for t in TRACKS))
    s[80] = 0                                      # text_area_count
    s[88:90] = b"en"
    s[90] = 1
    return bytes(s)


def tracklist_offsets():
    s = bytearray(SECTOR)
    s[0:8] = b"SACDTRL1"
    for i, t in enumerate(TRACKS):
        s[8 + i * 4: 12 + i * 4] = struct.pack(">I", t["start_lsn"])
        s[8 + 255 * 4 + i * 4: 12 + 255 * 4 + i * 4] = struct.pack(">I", t["length_lsn"])
    return bytes(s)


def tracklist_times():
    def tc(frames):
        return bytes([frames // (60 * 75), (frames // 75) % 60, frames % 75, 0])

    s = bytearray(SECTOR)
    s[0:8] = b"SACDTRL2"
    for i, t in enumerate(TRACKS):
        s[8 + i * 4: 12 + i * 4] = tc(t["start_frames"])
        s[8 + 255 * 4 + i * 4: 12 + 255 * 4 + i * 4] = tc(t["frames"])
    return bytes(s)


def build(path):
    img = bytearray(TOTAL_SECTORS * SECTOR)

    def put(lsn, data):
        img[lsn * SECTOR: lsn * SECTOR + len(data)] = data

    put(16, iso9660_pvd(TOTAL_SECTORS))
    put(MASTER_TOC_LSN, master_toc())
    for i in range(8):
        put(MASTER_TOC_LSN + 1 + i, sacd_text())
    put(MASTER_TOC_LSN + 9, sacd_man())
    for base in (AREA_TOC_LSN, AREA_TOC_2_LSN):
        put(base, area_toc())
        put(base + 1, tracklist_offsets())
        put(base + 2, tracklist_times())
    # 音频区保持全零：没有合法的音频扇区，helper 会一路补零到声明时长，
    # 正好把「数据不足时的补零路径」也覆盖掉。
    with open(path, "wb") as f:
        f.write(img)
    return len(img)


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "tests/fixtures/generated"
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, "sacd-skeleton.iso")
    n = build(path)
    print(f"{path}  {n} 字节  {TOTAL_SECTORS} 扇区  {len(TRACKS)} 曲 / {CHANNELS} 声道 / 纯 DSD")


if __name__ == "__main__":
    main()
