"""Offline agreement of the offline VT alpha masks with the RT test's page-cache reads (docs/P4_OMM.md 11.2).

    py -3 -m tools.assets.vt_alpha_agree --pack <replay pack> --masks <dir with *.vtalpha + vt_alpha_manifest.json>

For every mask texel of every level the RT test can request (mip <= record P1.x), the pack's own data answers
what shaders/rt/rt_common.hlsli vt_alpha() reads at that texel centre with that mip requested: the indirection
entry (gAlphaTex[rec.tex] mip m), the page's cache coordinate (the same constants, float32 math), a bilinear
read of the tile cache's alpha (gAlphaCache[rec.cache], mip 0, clamp). Reported per mask and in total, weighted
by triangles: exact (|a - b| <= 0.5/255: BC3 alpha decodes identically), alpha-test agreement at the record's
cut-off, and where it differs whether the entry pointed at a coarser resident page (the expected cause) or not.
"""
import argparse
import json
import struct
import sys
from pathlib import Path

import numpy as np

REC = 208
F32 = np.float32


def manifest_images(pack):
    imgs = {}
    for line in (Path(pack) / "manifest.txt").read_text().splitlines():
        p = line.split()
        if p and p[0] == "image":
            s, b, e, fmt, w, h, layers, mips = map(int, p[1:9])
            imgs[(s, b, e)] = (fmt, w, h, layers, mips, Path(pack) / p[9])
    return imgs


def bc4_decode(blocks):
    """(N, 8) uint8 BC4/BC3-alpha blocks -> (N, 16) float32 alpha in [0, 1] (texel i = y * 4 + x)."""
    a0 = blocks[:, 0].astype(np.int32)
    a1 = blocks[:, 1].astype(np.int32)
    bits = np.zeros(len(blocks), np.uint64)
    for i in range(6):
        bits |= blocks[:, 2 + i].astype(np.uint64) << np.uint64(8 * i)
    idx = np.stack([((bits >> np.uint64(3 * k)) & np.uint64(7)).astype(np.int32) for k in range(16)], 1)
    pal = np.zeros((len(blocks), 8), np.float32)
    pal[:, 0], pal[:, 1] = a0, a1
    big = a0 > a1
    for k in range(1, 7):
        pal[:, k + 1] = np.where(big, ((7 - k) * a0 + k * a1) / 7.0, 0)
    for k in range(1, 5):
        pal[:, k + 1] = np.where(big, pal[:, k + 1], ((5 - k) * a0 + k * a1) / 5.0)
    pal[:, 6] = np.where(big, pal[:, 6], 0)
    pal[:, 7] = np.where(big, pal[:, 7], 255)
    return np.take_along_axis(pal, idx, 1) / 255.0


def mask_levels(path):
    d = Path(path).read_bytes()
    assert d[:8] == b"BG3VTA1\0", path
    w, h, n = struct.unpack_from("<3I", d, 8)
    out, off = [], 20
    for m in range(n):
        mw, mh = max(w >> m, 1), max(h >> m, 1)
        bw, bh = (mw + 3) // 4, (mh + 3) // 4
        blk = np.frombuffer(d, np.uint8, bw * bh * 8, off).reshape(-1, 8)
        off += bw * bh * 8
        tex = bc4_decode(blk).reshape(bh, bw, 4, 4).transpose(0, 2, 1, 3).reshape(bh * 4, bw * 4)[:mh, :mw]
        out.append(tex)
    return out


class Cache:
    """Alpha of a BC3 2D-array image file of the pack (mip 0, layer-major), bilinear, clamp to edge."""

    def __init__(self, fmt, w, h, layers, path):
        assert fmt in (137, 138), fmt
        self.w, self.h, self.bw, self.bh = w, h, (w + 3) // 4, (h + 3) // 4
        self.layer_bytes = self.bw * self.bh * 16
        self.mm = np.memmap(path, np.uint8, "r")

    def texel(self, layer, x, y):
        x = np.clip(x, 0, self.w - 1)
        y = np.clip(y, 0, self.h - 1)
        blk = (layer.astype(np.int64) * self.layer_bytes + ((y // 4).astype(np.int64) * self.bw + x // 4) * 16)
        raw = self.mm[(blk[:, None] + np.arange(8)[None, :])]
        dec = bc4_decode(raw)
        return dec[np.arange(len(x)), (y % 4) * 4 + (x % 4)]

    def bilinear(self, layer, c):
        fx = c[:, 0] * self.w - 0.5
        fy = c[:, 1] * self.h - 0.5
        x0, y0 = np.floor(fx).astype(np.int64), np.floor(fy).astype(np.int64)
        tx, ty = (fx - x0).astype(np.float32), (fy - y0).astype(np.float32)
        a = self.texel(layer, x0, y0); b = self.texel(layer, x0 + 1, y0)
        cc = self.texel(layer, x0, y0 + 1); d = self.texel(layer, x0 + 1, y0 + 1)
        return (a * (1 - tx) + b * tx) * (1 - ty) + (cc * (1 - tx) + d * tx) * ty


def vt_reads(rec, ind, cache, uv, mip):
    """vt_alpha() with the mip forced: (alpha, coarser) for tile uv (N, 2) in [0, 1)."""
    t = rec["t"]
    T = lambda m, c, r: F32(t[m * 16 + r * 4 + c])  # noqa: E731  vt_t(a, m, c, r)
    p0 = np.array(rec["p0"], F32)
    u = (uv.astype(F32) - np.floor(uv)) * p0[2:] + p0[:2]
    dims = np.array([T(0, 0, 3), T(0, 0, 3) * T(0, 1, 3)], F32)
    ifmt, iw, ih, _, imips, ipath = ind
    m = min(mip, imips - 1)
    ip = np.floor(u * dims * F32(0.5 ** m)).astype(np.int64)
    mw, mh = max(iw >> m, 1), max(ih >> m, 1)
    ip[:, 0] = np.clip(ip[:, 0], 0, mw - 1)
    ip[:, 1] = np.clip(ip[:, 1], 0, mh - 1)
    off = sum(max(iw >> k, 1) * max(ih >> k, 1) * 16 for k in range(m))
    mm = np.memmap(ipath, np.uint32, "r")
    e = mm[(off // 4) + (ip[:, 1] * mw + ip[:, 0]) * 4].astype(np.uint32)
    scale = (e & 15).astype(np.int64)
    sc = np.power(F32(2), scale.astype(F32))
    used = np.maximum(np.log2(np.maximum(dims[0], 1)) - scale, 0)
    coarser = used > m + 0.5
    s163 = np.stack([sc, sc * T(0, 1, 3)], 1)
    s168 = np.array([T(0, 0, 2), T(0, 1, 2)], F32)
    s175 = np.array([T(1, 0, 0), T(1, 1, 0)], F32)
    col = min(rec["channel"], 3)
    page = np.stack([((e >> 14) & 1023).astype(F32), ((e >> 4) & 1023).astype(F32)], 1)
    q = u * s163
    c = ((q - np.floor(q)) * s168 + page) * s175 + np.array([T(1, 2, col), T(1, 3, col)], F32)
    layer = ((e >> 24) & 127).astype(np.int64)
    return cache.bilinear(layer, c), coarser


def records(pack):
    b = (Path(pack) / "s1b0.bin").read_bytes()
    n = struct.unpack_from("<I", b, 0)[0]
    out = []
    for i in range(n):
        o = 16 + REC * i
        first, tris, kind, tex, cache, ch = struct.unpack_from("<6I", b, o)
        cut, cut0 = struct.unpack_from("<2f", b, o + 24)
        lo, hi = struct.unpack_from("<2I", b, o + 40)
        out.append({"tris": tris, "kind": kind, "tex": tex, "cache": cache, "channel": ch, "cut": cut,
                    "fs": (hi << 32) | lo, "p0": struct.unpack_from("<4f", b, o + 48),
                    "p1": struct.unpack_from("<4f", b, o + 64), "t": struct.unpack_from("<32f", b, o + 80)})
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--pack", required=True)
    ap.add_argument("--masks", required=True)
    a = ap.parse_args()
    imgs = manifest_images(a.pack)
    man = json.loads((Path(a.masks) / "vt_alpha_manifest.json").read_text())
    recs = records(a.pack)
    tot = {"n": 0.0, "exact": 0.0, "test": 0.0, "diff_coarser": 0.0, "diff_same": 0.0}
    for mk in man["masks"]:
        rs = [r for r in recs if r["kind"] == 1 and int(r["t"][15]) == mk["tileset"] and
              np.allclose(r["p0"], mk["p0"], 0, 0) and r["fs"] in {int(h, 16) for h in man["fs"]}]
        if not rs:
            print(f"{mk['file']}: no record"); continue
        r = rs[0]
        ind = imgs[(1, 2, r["tex"])]
        cfmt, cw, ch, cl, cm, cpath = imgs[(1, 3, r["cache"])]
        cache = Cache(cfmt, cw, ch, cl, cpath)
        levels = mask_levels(Path(a.masks) / mk["file"])
        top = min(int(r["p1"][0]), len(levels) - 1)
        line = []
        acc = {k: 0.0 for k in tot}
        for m in range(top + 1):
            ref = levels[m]
            hh, ww = ref.shape
            ys, xs = np.mgrid[0:hh, 0:ww]
            uv = np.stack([(xs.ravel() + 0.5) / ww, (ys.ravel() + 0.5) / hh], 1)
            got, coarser = vt_reads(r, ind, cache, uv, m)
            want = ref.ravel()
            exact = np.abs(got - want) <= 0.5 / 255
            test = (got >= r["cut"]) == (want >= r["cut"])
            w = 1.0 / (top + 1) / len(want)   # every requested level weighs the same
            acc["n"] += w * len(want); acc["exact"] += w * exact.sum(); acc["test"] += w * test.sum()
            acc["diff_coarser"] += w * (~test & coarser).sum(); acc["diff_same"] += w * (~test & ~coarser).sum()
            line.append(f"mip {m}: exact {100 * exact.mean():.2f}%, test {100 * test.mean():.2f}%, coarser pages "
                        f"{100 * coarser.mean():.1f}% (test differs there {100 * (~test & coarser).sum() / max(coarser.sum(), 1):.1f}%)")
        print(f"{mk['file']} ({mk['triangles']} tris, cut {r['cut']}): " + "; ".join(line), flush=True)
        for k in tot:
            tot[k] += acc[k] * mk["triangles"]
    n = tot["n"]
    print(f"triangle-weighted: exact {100 * tot['exact'] / n:.2f}%, alpha test agrees {100 * tot['test'] / n:.2f}%; "
          f"differs {100 * (n - tot['test']) / n:.2f}% (on coarser pages {100 * tot['diff_coarser'] / n:.2f}%, "
          f"same level {100 * tot['diff_same'] / n:.2f}%)")


if __name__ == "__main__":
    main()
