#!/usr/bin/env python3
"""造「结构声明得出、内容却不全」的碟镜像骨架，用来验 T5 的完整性校验。

真镜像不入库（CLAUDE.md 深坑 #6），所以这里连内容都不造——只写卷描述符，
让镜像**声明**自己有几 GB，文件本身却只有几百 KB。md-player 的判定恰好
只看「结构声明 vs 文件实际大小」，不抽扇区试读，所以这样的骨架就够用，
也正因为不试读，真盘上才不会被误伤。

两种形态各造一个，对应两条判定路线：
  iso9660  ISO9660 主卷描述符声明 volume space size —— DVD / SACD 镜像走这条
  udf      UDF 只在 LSN 256 留头部锚点、卷末没有锚点 —— 蓝光原盘走这条

用法:
  python3 tests/fixtures/make_broken_iso.py 输出目录
"""
import os
import struct
import sys

SECTOR = 2048
DECLARED_SECTORS = 3_783_281  # 随便挑的「一张双层 DVD」的规模
ACTUAL_SECTORS = 512          # 1MiB，够放下卷描述符区，远不够放内容


def write_sector(f, lsn, data):
    f.seek(lsn * SECTOR)
    f.write(data.ljust(SECTOR, b"\0"))


def make_iso9660(path):
    """ISO9660 主卷描述符声明 DECLARED_SECTORS 块，文件只有 ACTUAL_SECTORS 块。"""
    with open(path, "wb") as f:
        pvd = bytearray(SECTOR)
        pvd[0] = 1                       # 类型 1 = 主卷描述符
        pvd[1:6] = b"CD001"
        pvd[6] = 1                       # 版本
        pvd[40:72] = b"BROKEN_ISO9660".ljust(32)   # 卷标
        # volume space size：两端序各存一份，判定只读小端那份
        pvd[80:84] = struct.pack("<I", DECLARED_SECTORS)
        pvd[84:88] = struct.pack(">I", DECLARED_SECTORS)
        # logical block size 同理
        pvd[128:130] = struct.pack("<H", SECTOR)
        pvd[130:132] = struct.pack(">H", SECTOR)
        write_sector(f, 16, bytes(pvd))
        f.truncate(ACTUAL_SECTORS * SECTOR)


def make_udf(path):
    """UDF 头部锚点在 LSN 256，卷末不放锚点——正是下载到一半的样子。"""
    with open(path, "wb") as f:
        # Anchor Volume Descriptor Pointer：判定只认 tag identifier == 2
        avdp = bytearray(SECTOR)
        avdp[0:2] = struct.pack("<H", 2)
        write_sector(f, 256, bytes(avdp))
        f.truncate(ACTUAL_SECTORS * SECTOR)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    iso = os.path.join(out, "truncated-iso9660.iso")
    udf = os.path.join(out, "truncated-udf.iso")
    make_iso9660(iso)
    make_udf(udf)
    for p in (iso, udf):
        print(f"{p}  {os.path.getsize(p)} 字节")
    print("期望: 两个都报「这个镜像不完整…」，都不进标题列表")
    return 0


if __name__ == "__main__":
    sys.exit(main())
