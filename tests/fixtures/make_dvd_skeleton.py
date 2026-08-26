#!/usr/bin/env python3
"""生成最小 DVD 结构骨架，用来验加密盘拦截与错误路径。

绝不包含任何版权内容：IFO 只有 12 字节魔数 + 补零，VOB 是自造的 MPEG-PS pack。
真实碟片一律走 MD_TEST_MEDIA 指向的库外目录（CLAUDE.md 深坑 #6）。

    python3 make_dvd_skeleton.py out/dvd-css   --css     # PES 扰码位置位 -> 应被拦截
    python3 make_dvd_skeleton.py out/dvd-clean           # 同结构不加扰 -> 应报「结构读不了」

为什么这样就够：md-player 的加密判定**先于** IFO 解析，只读 VOB 扇区偏移 0x14 的
PES_scrambling_control（bit5..4）。所以骨架不需要合法的 IFO，只需要一个结构正确的 VOB。
反过来，clean 版本正好验证「拦截确实由扰码位驱动，而不是因为 IFO 读不了」。
"""
import os
import struct
import sys

SECTOR = 2048


def write_ifo(video_ts, name, magic):
    data = bytearray(SECTOR)
    data[0:12] = magic
    with open(os.path.join(video_ts, name), "wb") as f:
        f.write(bytes(data))


def make_vob(sectors, scrambled):
    out = bytearray()
    for _ in range(sectors):
        s = bytearray(SECTOR)
        # pack header：DVD 恒为 14 字节，无填充字节
        s[0:4] = b"\x00\x00\x01\xba"
        s[4:14] = b"\x44\x00\x04\x00\x04\x01\x00\x9c\x4f\xf8"
        # 紧随其后的 video PES（stream id 0xE0）
        s[14:17] = b"\x00\x00\x01"
        s[17] = 0xE0
        s[18:20] = struct.pack(">H", SECTOR - 20)
        # 偏移 0x14 = PES flags：'10' + PES_scrambling_control(2 bit) + ...
        s[20] = 0x80 | (0x20 if scrambled else 0x00)
        out += s
    return bytes(out)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        print(__doc__)
        return 2
    out = args[0]
    scrambled = "--css" in sys.argv
    video_ts = os.path.join(out, "VIDEO_TS")
    os.makedirs(video_ts, exist_ok=True)

    write_ifo(video_ts, "VIDEO_TS.IFO", b"DVDVIDEO-VMG")
    write_ifo(video_ts, "VIDEO_TS.BUP", b"DVDVIDEO-VMG")
    write_ifo(video_ts, "VTS_01_0.IFO", b"DVDVIDEO-VTS")
    write_ifo(video_ts, "VTS_01_0.BUP", b"DVDVIDEO-VTS")
    with open(os.path.join(video_ts, "VTS_01_1.VOB"), "wb") as f:
        f.write(make_vob(64, scrambled))

    print("已生成 %s（PES 扰码位=%s）" % (video_ts, "置位" if scrambled else "清零"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
