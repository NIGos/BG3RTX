"""Rank what blocks native hit materials, per TLAS instance, on a replay pack (offline, no game).

    bin\\frame_replay.exe --pack <pack> --out <dir> --define HM_BLOCKERS=1
    py -3 tools/hit_material_blockers.py --pack <pack> --stats <dir>/rtstats.bin [--top 25]

HM_BLOCKERS (shaders/rt/native_hit_material.hlsli hit_material_count) counts every hit-material
lookup of the pass by TLAS instance (= InstInfo index) and outcome: qualified, base without a
record, row, other. This tool joins those counts with the pack's InstInfo (s1b7), alpha records
(s1b0: the depth-pass FS hash of an alpha instance), hit-record rows (s1b12) and geometry (soup
s1b6 ranges, mesh pool s1b8 with the instance matrices) and prints the blockers by capture class
and by instance, with each instance's extent, area and distance (what the instance is: terrain,
character part, prop). The capture class is exact (InstInfo bits, layer/bvhsplit.cpp); the shape
label is a descriptor only (extent and area), never used as a decision.
"""
import argparse
import collections
import mmap
import os
import struct
from pathlib import Path

import numpy as np

SLOT0 = 448 * 64          # HM_BLOCKERS raw slot base (uint32 index)
MAX_INST = 1024
KINDS = ("qualified", "base", "row", "other")
POOL, LEGACY, ALPHA, EMISSIVE, HAIR, OPAQUE, FADE, POOL_ROW = (1 << 8, 1 << 9, 1 << 10, 1 << 11, 1 << 12,
                                                               1 << 13, 1 << 14, 1 << 15)


def read_counts(path, instances):
    raw = np.fromfile(path, dtype="<u4")
    need = SLOT0 + MAX_INST * 4
    if raw.size < need:
        raise SystemExit(f"{path}: {raw.size} counters, HM_BLOCKERS needs {need}")
    return raw[SLOT0:SLOT0 + 4 * min(instances, MAX_INST)].reshape(-1, 4).astype(np.int64)


def capture_class(meta, rec, alpha_rows, hit, frame):
    """Exact capture class of one instance (InstInfo meta / rec, the pack's record tables)."""
    if meta & LEGACY:
        return "legacy soup", ""
    pool = bool(meta & POOL)
    if meta & ALPHA and meta & POOL and meta & POOL_ROW:
        return "pool alpha, pool row", f"slot {(meta >> 16) - 1}"   # rt_hit_material_pool_alpha
    if meta & ALPHA:
        fs = ""
        if rec >= 16 and (rec - 16) % 208 == 0 and alpha_rows is not None:
            r = (rec - 16) // 208
            kind = struct.unpack_from("<I", alpha_rows, 16 + r * 208 + 8)[0]
            lo, hi = struct.unpack_from("<2I", alpha_rows, 16 + r * 208 + 40)
            fs = f"{hi:08x}{lo:08x} kind {kind}"
            row = 16 + r * 256
            h0 = struct.unpack_from("<4I", hit, row) if len(hit) >= row + 32 else (0,) * 4
            h1 = struct.unpack_from("<4I", hit, row + 16) if len(hit) >= row + 32 else (0,) * 4
            has_row = h0[0] == r + 1 and h1[3] == frame
            return ("pool" if pool else "soup") + (" alpha, row" if has_row else " alpha, no row"), fs
        return ("pool" if pool else "soup") + " alpha, bad record", fs
    if pool:
        return ("pool with row" if meta & POOL_ROW else "pool without row"), ""
    if meta & OPAQUE:
        return "soup opaque record", ""
    return "soup without record" + (" (hair)" if meta & HAIR else ""), ""


class Geometry:
    """Camera-relative positions of an instance's triangles (soup ranges or pool + matrix)."""

    def __init__(self, pack):
        self.pack = pack
        self.ranges = []
        self.soup = self.pool = None
        for line in open(os.path.join(pack, "manifest.txt"), encoding="utf-8", errors="replace"):
            w = line.split()
            if len(w) == 7 and w[0] == "range" and w[1] == "1" and w[2] == "6":
                self.ranges.append((int(w[3]), w[4], int(w[5]), int(w[6])))
        if self.ranges:
            with open(os.path.join(pack, self.ranges[0][1]), "rb") as f:
                self.soup = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        pool = os.path.join(pack, "s1b8.bin")
        if os.path.exists(pool):
            with open(pool, "rb") as f:
                self.pool = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        self.inst = np.fromfile(os.path.join(pack, "instances.bin"), dtype=np.uint8).reshape(-1, 64)

    def close(self):
        for m in (self.soup, self.pool):
            if m is not None:
                m.close()

    def positions(self, i, base, meta, tris):
        stride, n = meta & 255, tris * 3
        if not stride or not tris:
            return None
        if meta & POOL:
            if self.pool is None or base + n * stride > len(self.pool):
                return None
            raw = np.frombuffer(self.pool, dtype=np.uint8, count=n * stride, offset=base).reshape(n, stride)
            m = self.inst[i, :48].copy().view("<f4").reshape(3, 4)
            return raw[:, :12].copy().view("<f4").reshape(n, 3) @ m[:, :3].T + m[:, 3]
        for start, _, foff, size in self.ranges:
            if start <= base and base + n * stride <= start + size:
                raw = np.frombuffer(self.soup, dtype=np.uint8, count=n * stride, offset=foff + base - start)
                return raw.reshape(n, stride)[:, :12].copy().view("<f4").reshape(n, 3)
        return None

    def describe(self, i, base, meta, tris):
        p = self.positions(i, base, meta, tris)
        if p is None or not np.isfinite(p).all():
            return {"extent": None, "area": 0.0, "distance": 0.0, "shape": "unknown"}
        lo, hi = p.min(0), p.max(0)
        t = p.reshape(-1, 3, 3)
        area = float(0.5 * np.linalg.norm(np.cross(t[:, 1] - t[:, 0], t[:, 2] - t[:, 0]), axis=1).sum())
        ext = hi - lo
        return {"extent": ext, "area": area, "distance": float(np.linalg.norm((lo + hi) / 2)), "shape": shape(ext, area)}


def shape(ext, area):
    """Descriptor only: what the instance looks like (y is up in the lighting space)."""
    horizontal, height = max(ext[0], ext[2]), ext[1]
    if horizontal >= 64 and height < 0.5 * horizontal and area >= 1000:
        return "terrain-like (>= 64 m flat patch)"
    if max(ext) <= 2.5:
        return "small (<= 2.5 m: character part, prop)"
    return "object"


def rank(pack, stats, top=25):
    info = np.fromfile(os.path.join(pack, "s1b7.bin"), dtype="<u4").reshape(-1, 4)
    alpha_path, hit_path = os.path.join(pack, "s1b0.bin"), os.path.join(pack, "s1b12.bin")
    alpha_rows = Path(alpha_path).read_bytes() if os.path.exists(alpha_path) else None
    hit = Path(hit_path).read_bytes() if os.path.exists(hit_path) else b""
    frame = struct.unpack_from("<I", hit, 0)[0] if len(hit) >= 16 else 0
    counts = read_counts(stats, len(info))
    geo = Geometry(pack)
    total = int(counts.sum())
    rows, by_class, by_shape = [], collections.defaultdict(lambda: np.zeros(4, np.int64)), collections.defaultdict(lambda: np.zeros(4, np.int64))
    for i, c in enumerate(counts):
        if not c.sum():
            continue
        base, meta, rec, tris = (int(x) for x in info[i])
        cls, fs = capture_class(meta, rec, alpha_rows, hit, frame)
        d = geo.describe(i, base, meta, tris) if c[1] + c[2] + c[3] else {"extent": None, "area": 0.0, "distance": 0.0, "shape": "-"}
        rows.append((i, cls, fs, tris, c, d))
        by_class[cls] += c
        if c[1] + c[2]:
            by_shape[(cls, d["shape"])] += c
    geo.close()
    out = [f"lookups {total} (qualified {int(counts[:, 0].sum())}, base {int(counts[:, 1].sum())}, "
           f"row {int(counts[:, 2].sum())}, other {int(counts[:, 3].sum())})", "", "by capture class (share of all lookups):"]
    pct = lambda v: 100.0 * v / max(total, 1)
    for cls, c in sorted(by_class.items(), key=lambda kv: -int(kv[1].sum())):
        out.append(f"  {cls:30s} {pct(c.sum()):5.1f}%  qualified {pct(c[0]):5.1f}  base {pct(c[1]):5.1f}  row {pct(c[2]):5.1f}  other {pct(c[3]):5.1f}")
    out += ["", "blocked (base + row) by class and shape:"]
    for (cls, sh), c in sorted(by_shape.items(), key=lambda kv: -int(kv[1][1] + kv[1][2])):
        out.append(f"  {cls:30s} {sh:40s} {pct(c[1] + c[2]):5.1f}%")
    # Alpha instances by their depth-pass FS (asset DB StaticDepth / StaticInstancedDepth variants name the
    # material templates that share it, hence the G-buffer pair to build next).
    by_fs, n_fs = collections.Counter(), collections.Counter()
    for i, cls, fs, tris, c, d in rows:
        if fs and c[1] + c[2]:
            by_fs[(cls, fs.split()[0])] += int(c[1] + c[2])
            n_fs[(cls, fs.split()[0])] += 1
    out += ["", "blocked alpha instances by depth FS:"]
    for (cls, fs), v in by_fs.most_common(top):
        out.append(f"  {cls:24s} {fs} {pct(v):5.1f}%  instances {n_fs[(cls, fs)]}")
    out += ["", f"top {top} instances by blocked lookups:"]
    for i, cls, fs, tris, c, d in sorted(rows, key=lambda r: -int(r[4][1] + r[4][2]))[:top]:
        ext = "x".join(f"{v:.1f}" for v in d["extent"]) if d["extent"] is not None else "?"
        out.append(f"  inst {i:4d} {pct(c[1] + c[2]):5.2f}% {cls:24s} {fs:26s} tris {tris:7d} extent {ext:>18s} m "
                   f"area {d['area']:9.1f} m2 at {d['distance']:6.1f} m  {d['shape']}")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--pack", required=True)
    ap.add_argument("--stats", required=True, help="rtstats.bin of an HM_BLOCKERS=1 replay")
    ap.add_argument("--top", type=int, default=25)
    a = ap.parse_args()
    print(rank(a.pack, a.stats, a.top))


if __name__ == "__main__":
    main()
