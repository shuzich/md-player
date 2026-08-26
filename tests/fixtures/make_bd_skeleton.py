#!/usr/bin/env python3
"""生成最小可解析的 BDMV 结构骨架，用于错误路径测试。

只写 index.bdmv / MovieObject.bdmv 两个导航文件的最小合法形态，不含任何流数据，
更不含任何版权内容（CLAUDE.md 已知深坑 #6）。加了 --aacs 后会额外造出
AACS/Unit_Key_RO.inf，让 libbluray 报 aacs_detected —— 用来验证加密盘拦截。

用法:
    python3 tests/fixtures/make_bd_skeleton.py <输出目录> [--aacs] [--bdplus]
"""
import os
import struct
import sys


def index_bdmv() -> bytes:
    # AppInfoBDMV 固定 34 字节负载，接在 4 字节长度后，起点 40，故索引段从 78 开始。
    app_info = bytearray(34)
    app_info[0] = 0x00  # reserved(1) + initial_output_mode(1) + content_exist(1) + reserved(5)
    app_info[1] = 0x60  # video_format=6(1080p) frame_rate=0
    # Indexes(): FirstPlayback + TopMenu + 标题数 + 每个标题各一条，条目定长 12 字节。
    def entry(object_type: int, playback_type: int, id_ref: int) -> bytes:
        # object_type(2) + reserved(30) | playback_type(2) + reserved(14) + id_ref(16)
        return struct.pack(">IHH", object_type << 30, playback_type << 14, id_ref)

    # HDMV 的 playback_type 只允许 0=movie / 1=interactive，越界会被 libbluray 判为非法。
    first_play = entry(1, 1, 0)   # HDMV 交互式，指向 MovieObject #0
    top_menu = entry(1, 1, 0)     # HDMV 交互式
    titles = entry(1, 0, 0)       # 一个 HDMV 影片标题
    body = first_play + top_menu + struct.pack(">H", 1) + titles
    indexes = struct.pack(">I", len(body)) + body

    head = bytearray(40)
    head[0:8] = b"INDX0200"
    struct.pack_into(">I", head, 8, 78)  # indexes_start_addr
    struct.pack_into(">I", head, 12, 0)  # extension_data_start_addr：无
    return bytes(head) + struct.pack(">I", len(app_info)) + bytes(app_info) + indexes


def movie_object_bdmv() -> bytes:
    # 一个不带任何导航命令的 MovieObject：resume/mask 位全 0，命令数 0。
    mobj = struct.pack(">HH", 0, 0)
    body = struct.pack(">IH", 0, 1) + mobj  # reserved(32) + number_of_mobjs(16)
    head = bytearray(40)
    head[0:8] = b"MOBJ0200"
    struct.pack_into(">I", head, 8, 0)  # extension_data_start_addr：无
    return bytes(head) + struct.pack(">I", len(body)) + body


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    root = sys.argv[1]
    opts = set(sys.argv[2:])

    for sub in ("BDMV/PLAYLIST", "BDMV/CLIPINF", "BDMV/STREAM", "BDMV/BACKUP"):
        os.makedirs(os.path.join(root, sub), exist_ok=True)

    idx = index_bdmv()
    mobj = movie_object_bdmv()
    for base in ("BDMV", "BDMV/BACKUP"):
        with open(os.path.join(root, base, "index.bdmv"), "wb") as f:
            f.write(idx)
        with open(os.path.join(root, base, "MovieObject.bdmv"), "wb") as f:
            f.write(mobj)

    if "--aacs" in opts:
        os.makedirs(os.path.join(root, "AACS"), exist_ok=True)
        # 内容无意义：libbluray 只看文件在不在，我们也绝不实现任何密钥逻辑（铁律 #2）。
        with open(os.path.join(root, "AACS", "Unit_Key_RO.inf"), "wb") as f:
            f.write(b"\x00" * 64)
    if "--bdplus" in opts:
        os.makedirs(os.path.join(root, "BDSVM"), exist_ok=True)
        with open(os.path.join(root, "BDSVM", "00000.svm"), "wb") as f:
            f.write(b"\x00" * 64)

    print("已生成 %s%s" % (root, " (AACS)" if "--aacs" in opts else ""))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
