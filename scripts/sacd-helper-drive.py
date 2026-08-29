#!/usr/bin/env python3
"""sacd-helper 的最小驱动（M1-PLAN T6 阶段 1）。

不是测试框架，就是一根能把协议 v1 走一遍的探针：open 拿曲目表、stat 拿 DSF 尺寸、
read 取若干段并打摘要，外加两项硬检查——同一 (area,track) 两次 stat/read 是否逐字节
一致（确定性），以及 helper 被 kill -9 后驱动侧是收到 EOF 还是挂死（崩溃隔离）。

真 ISO 一律走 MD_TEST_MEDIA，版权内容零入库（CLAUDE.md 深坑 #6）。

    export MD_TEST_MEDIA=/path/to/media
    scripts/sacd-helper-drive.py --list
    scripts/sacd-helper-drive.py --iso "<路径>" --track 1
    scripts/sacd-helper-drive.py --iso "<路径>" --kill-test
"""
import argparse
import base64
import hashlib
import json
import os
import signal
import struct
import subprocess
import sys
import time

ERROR_FLAG = 0x80000000


class Helper:
    """协议 v1 的客户端：控制面读 JSON 行，数据面读 4B id + 4B 长度（小端）。"""

    def __init__(self, exe):
        self.p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        self.next_id = 1

    def _send(self, obj):
        obj["id"] = self.next_id
        self.next_id += 1
        self.p.stdin.write((json.dumps(obj, ensure_ascii=False) + "\n").encode("utf-8"))
        self.p.stdin.flush()
        return obj["id"]

    def call(self, **obj):
        rid = self._send(obj)
        line = self.p.stdout.readline()
        if not line:
            raise EOFError("helper 关闭了 stdout（进程已死？）")
        res = json.loads(line)
        assert res["id"] == rid, f"响应 id 对不上: {res['id']} != {rid}"
        return res

    def read(self, area, track, offset, length):
        rid = self._send({"cmd": "read", "area": area, "track": track,
                          "offset": offset, "length": length})
        hdr = self._recv_exact(8)
        fid, n = struct.unpack("<II", hdr)
        payload = self._recv_exact(n)
        if fid & ERROR_FLAG:
            raise RuntimeError(f"read 失败: {payload.decode('utf-8', 'replace')}")
        assert fid == rid, f"帧 id 对不上: {fid} != {rid}"
        return payload

    def _recv_exact(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.p.stdout.read(n - len(buf))
            if not chunk:
                raise EOFError("读帧时遇到 EOF（helper 已死）")
            buf += chunk
        return buf

    def close(self):
        try:
            self.call(cmd="quit")
        except Exception:
            pass
        try:
            self.p.stdin.close()
        except Exception:
            pass
        self.p.wait(timeout=5)


def b64text(s):
    """碟内文本按 charset 原样吐出，这里只做「能不能当 ASCII 看」的粗解，够肉眼核对。"""
    if not s:
        return ""
    raw = base64.b64decode(s)
    try:
        return raw.decode("ascii")
    except UnicodeDecodeError:
        return f"<{len(raw)} 字节非 ASCII: {raw[:16].hex()}…>"


def find_isos(root):
    out = []
    for dp, _, fn in os.walk(root):
        for n in fn:
            if not n.lower().endswith((".iso", ".img")):
                continue
            p = os.path.join(dp, n)
            try:
                with open(p, "rb") as f:
                    f.seek(510 * 2048)
                    if f.read(8) == b"SACDMTOC":
                        out.append(p)
            except OSError:
                pass
    return sorted(out)


def run_one(exe, iso, track, ranges, quiet=False):
    h = Helper(exe)
    info = h.call(cmd="open", path=iso)
    if not info.get("ok"):
        print(f"  open 失败: {info.get('error')}")
        h.close()
        return None
    if not quiet:
        album = info["album"]
        print(f"  专辑: {b64text(album.get('title_b64'))} / {b64text(album.get('artist_b64'))} "
              f"(charset={album.get('charset')})")
        for a in info["areas"]:
            print(f"  区 {a['area']} {a['kind']}: {a['channels']} 声道, "
                  f"dst={a['dst']}, {len(a['tracks'])} 曲, charset={a['charset']}")
            for t in a["tracks"][:3]:
                print(f"      #{t['index']:>2} {t['seconds']:>8.3f}s  {b64text(t.get('title_b64'))}")
            if len(a["tracks"]) > 3:
                print(f"      …其余 {len(a['tracks']) - 3} 曲略")

    digests = {}
    for a in info["areas"]:
        st = h.call(cmd="stat", area=a["area"], track=track)
        if not st.get("ok"):
            print(f"  区 {a['area']} stat 失败: {st.get('error')}")
            continue
        if not quiet:
            print(f"  区 {a['area']} stat track={track}: dsf_size={st['dsf_size']} "
                  f"channels={st['channels']} frames={st['frames']} blocks={st['blocks']}")
        parts = []
        for off, ln in ranges:
            if off >= st["dsf_size"]:
                continue
            t0 = time.time()
            data = h.read(a["area"], track, off, ln)
            dt = time.time() - t0
            d = hashlib.sha256(data).hexdigest()[:16]
            parts.append((off, ln, len(data), d))
            if not quiet:
                print(f"      read off={off:<12} len={ln:<8} → {len(data):<8} 字节 "
                      f"sha256={d}…  {dt * 1000:.0f}ms")
        digests[a["area"]] = (st, parts)
    h.close()
    return digests


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default="build/sacd-helper")
    ap.add_argument("--iso")
    ap.add_argument("--track", type=int, default=1)
    ap.add_argument("--list", action="store_true", help="列出 MD_TEST_MEDIA 下的 SACD 镜像")
    ap.add_argument("--all", action="store_true", help="对 MD_TEST_MEDIA 下每张碟各跑一遍")
    ap.add_argument("--determinism", action="store_true", help="同一轨跑两遍，逐字节比对")
    ap.add_argument("--kill-test", action="store_true", help="播放中 kill -9 helper，看驱动侧行为")
    ap.add_argument("--seek-bench", action="store_true",
                    help="先顺序读一段把索引建起来，再在已索引区间做回退 seek 并统计耗时；"
                         "最后测一次未索引区间的前进 seek（追赶）")
    ap.add_argument("--area", type=int, default=None, help="只测某个区")
    args = ap.parse_args()

    media = os.environ.get("MD_TEST_MEDIA")
    if args.list or args.all:
        if not media:
            sys.exit("MD_TEST_MEDIA 没设置")
        isos = find_isos(media)
        if args.list:
            for p in isos:
                print(p)
            return
    else:
        isos = [args.iso] if args.iso else []
    if not isos:
        sys.exit("没有可测的镜像：给 --iso，或设好 MD_TEST_MEDIA 后用 --all")

    # 头、块边界、块中间、跨块、接近尾部——覆盖映射里所有分支
    ranges = [(0, 92), (92, 8192), (92 + 4096, 4096), (92 + 8192 * 100 + 123, 65536)]

    if args.seek_bench:
        import statistics
        for iso in isos:
            print(f"### {os.path.basename(iso)}")
            h = Helper(args.exe)
            info = h.call(cmd="open", path=iso)
            for a in info["areas"]:
                if args.area is not None and a["area"] != args.area:
                    continue
                st = h.call(cmd="stat", area=a["area"], track=args.track)
                size = st["dsf_size"]
                tag = f"区 {a['area']} {a['kind']} {a['channels']}ch dst={a['dst']}"
                # 1) 顺序读前 1/8，等价于「播了这么久」，索引随之建起来
                warm = 92
                limit = 92 + (size - 92) // 8
                t0 = time.time()
                while warm < limit:
                    n = min(1 << 20, limit - warm)
                    h.read(a["area"], args.track, warm, n)
                    warm += n
                seq = time.time() - t0
                played = (limit - 92) / (size - 92)
                print(f"  {tag}: 顺序读前 {played * 100:.0f}%（{(limit - 92) / 1e6:.0f} MB）耗时 {seq:.2f}s")
                # 2) 已索引区间内的回退 seek
                lat = []
                for i in range(12):
                    off = 92 + int((limit - 92) * ((11 - i) / 12.0))
                    t0 = time.time()
                    h.read(a["area"], args.track, off, 65536)
                    lat.append((time.time() - t0) * 1000)
                lat_sorted = sorted(lat)
                print(f"      已索引区间回退 seek ×{len(lat)}：中位 {statistics.median(lat):.0f}ms  "
                      f"最快 {lat_sorted[0]:.0f}ms  最慢 {lat_sorted[-1]:.0f}ms  "
                      f"{'✓ 达标（D-013 T2 口径 <100ms）' if statistics.median(lat) < 100 else '✗ 未达标'}")
                # 3) 未索引区间的前进 seek（追赶）
                far = size - 200000
                t0 = time.time()
                h.read(a["area"], args.track, far, 65536)
                first = (time.time() - t0) * 1000
                t0 = time.time()
                h.read(a["area"], args.track, far, 65536)
                again = (time.time() - t0) * 1000
                print(f"      未索引区间前进 seek 到 {far / 1e6:.0f} MB（{100 * far / size:.0f}%）："
                      f"首次 {first:.0f}ms（含索引追赶），同点再读 {again:.0f}ms")
            h.close()
            print()
        return

    if args.kill_test:
        iso = isos[0]
        print(f"### kill -9 隔离测试: {os.path.basename(iso)}")
        h = Helper(args.exe)
        info = h.call(cmd="open", path=iso)
        area = info["areas"][0]["area"]
        h.call(cmd="stat", area=area, track=1)
        first = h.read(area, 1, 0, 4096)
        print(f"  杀之前 read 正常: {len(first)} 字节")
        os.kill(h.p.pid, signal.SIGKILL)
        t0 = time.time()
        try:
            h.read(area, 1, 4096, 4096)
            print("  ✗ 杀之后 read 竟然成功了")
        except EOFError as e:
            print(f"  ✓ 杀之后 read 抛 EOFError（{e}），耗时 {time.time() - t0:.2f}s，没有挂死")
        except Exception as e:
            print(f"  ✓ 杀之后 read 抛 {type(e).__name__}: {e}，耗时 {time.time() - t0:.2f}s")
        print(f"  helper 退出码: {h.p.wait(timeout=5)}")
        return

    for iso in isos:
        print(f"### {iso}")
        first = run_one(args.exe, iso, args.track, ranges)
        if args.determinism and first:
            second = run_one(args.exe, iso, args.track, ranges, quiet=True)
            ok = True
            for area, (st1, parts1) in first.items():
                st2, parts2 = second[area]
                if st1["dsf_size"] != st2["dsf_size"]:
                    print(f"  ✗ 区 {area} 两次 stat 的 dsf_size 不同: {st1['dsf_size']} vs {st2['dsf_size']}")
                    ok = False
                if parts1 != parts2:
                    print(f"  ✗ 区 {area} 两次 read 的摘要不同")
                    ok = False
            print(f"  {'✓' if ok else '✗'} 确定性：两次 stat/read 逐字节{'一致' if ok else '不一致'}")
        print()


if __name__ == "__main__":
    main()
