"""Offline micromap coverage at mip 0 (option B, docs/P4_OMM.md 11): what a 4-state bake from the VT masks knows.

    py -3 -m tools.assets.omm_mask_coverage --pack <replay pack> --masks <data/assetdb/vt_alpha> --fs 738494714d50b4c7
        [--samples 400] [--levels 0 1 2 3 4 5]

With rt_alpha_fine_lod=1 the RT test of a masked record reads mip 0 bilinearly with the hard cut, for every ray.
For each sampled triangle of the shader (InstInfo + per-draw soup / pool; uv at +16, wrapped like the mask view)
and subdivision level L, every one of the 4^L micro-triangles (uniform barycentric grid, EXT order irrelevant here)
gets the texels a bilinear read anywhere inside it can touch: its uv bounding box, floor(u W - 0.5) .. +1 per axis.
Known opaque: every such texel >= cut + 1/255; known transparent: every one < cut - 1/255 (the margin bounds the
BC4 decode precision of the hardware against the reference decode); else unknown. Counts via summed-area tables of
the strict solid / clear masks (tiled 3 x 3 for the wrap). Reported per L, triangle-weighted, with the micromap
size (2 bits per micro-triangle) for the shader's triangles in the pack.
"""
import argparse
import json
import math
from pathlib import Path

import numpy as np

from tools.assets.vt_alpha_agree import mask_levels, records

KINFO_POOL, KINFO_ALPHA = 1 << 8, 1 << 10


def soup_reader(pack):
    parts = []
    for line in (Path(pack) / "manifest.txt").read_text().splitlines():
        p = line.split()
        if p[:3] == ["range", "1", "6"]:
            parts.append((int(p[3]), p[4], int(p[5]), int(p[6])))
    f = np.memmap(Path(pack) / parts[0][1], np.uint8, "r")

    def at(off, n):
        for base, _, foff, nbytes in parts:
            if base <= off and off + n <= base + nbytes:
                return f[foff + off - base: foff + off - base + n]
        return None
    return at


def sat(mask):
    t = np.tile(mask, (3, 3)).astype(np.int32)
    s = np.zeros((t.shape[0] + 1, t.shape[1] + 1), np.int64)
    s[1:, 1:] = t.cumsum(0).cumsum(1)
    return s


def box_count(s, x0, y0, x1, y1):   # inclusive texel boxes, already shifted into the tiled range
    return s[y1 + 1, x1 + 1] - s[y0, x1 + 1] - s[y1 + 1, x0] + s[y0, x0]


def micro_bary(level):
    """(4^L, 3, 2) barycentric (b1, b2) corners of the uniform subdivision of a triangle."""
    n = 1 << level
    tris = []
    for i in range(n):
        for j in range(n - i):
            a, b, c = (i, j), (i + 1, j), (i, j + 1)
            tris.append((a, b, c))
            if j < n - i - 1:
                tris.append(((i + 1, j), (i + 1, j + 1), (i, j + 1)))
    return np.array(tris, np.float64) / n


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--pack", required=True)
    ap.add_argument("--masks", required=True)
    ap.add_argument("--fs", nargs="+", required=True)
    ap.add_argument("--samples", type=int, default=400, help="triangles sampled per instance")
    ap.add_argument("--levels", type=int, nargs="+", default=[0, 1, 2, 3, 4, 5])
    a = ap.parse_args()
    fs = {int(h, 16) for h in a.fs}
    man = json.loads((Path(a.masks) / "vt_alpha_manifest.json").read_text())
    masks = {}
    for m in man["masks"]:
        mip0 = mask_levels(Path(a.masks) / m["file"])[0]
        masks[(m["tileset"], tuple(np.float32(m["p0"])))] = mip0
    recs = records(a.pack)
    ii = np.frombuffer((Path(a.pack) / "s1b7.bin").read_bytes(), np.uint32).reshape(-1, 4)
    pool = np.memmap(Path(a.pack) / "s1b8.bin", np.uint8, "r")
    soup = soup_reader(a.pack)
    rng = np.random.default_rng(1)
    grids = {L: micro_bary(L) for L in a.levels}
    acc = {L: np.zeros(3) for L in a.levels}   # opaque, transparent, unknown (triangle-weighted micro fractions)
    sats, total_tris, sampled = {}, 0, 0
    for base, meta, rec, tris in ii:
        base, meta, rec, tris = int(base), int(meta), int(rec), int(tris)
        if not meta & KINFO_ALPHA or rec < 16 or (rec - 16) % 208 or not tris:
            continue
        r = recs[(rec - 16) // 208]
        if r["fs"] not in fs or r["kind"] != 1:
            continue
        key = (int(r["t"][15]), tuple(np.float32(r["p0"])))
        if key not in masks:
            continue
        mask = masks[key]
        H, W = mask.shape
        cut = np.float32(r["cut"])
        if key not in sats:
            sats[key] = (sat(mask >= cut + 1 / 255), sat(mask < cut - 1 / 255))
        s_solid, s_clear = sats[key]
        total_tris += tris
        stride = meta & 0xFF
        n = min(tris, a.samples)
        prims = rng.choice(tris, n, replace=False) if tris > n else np.arange(tris)
        w = tris / n
        for prim in prims:
            o = base + int(prim) * 3 * stride
            raw = pool[o:o + 3 * stride] if meta & KINFO_POOL else soup(o, 3 * stride)
            if raw is None or len(raw) < 3 * stride:
                continue
            uv = np.array([np.frombuffer(bytes(raw[k * stride + 16:k * stride + 24]), np.float32) for k in range(3)], np.float64)
            if not np.all(np.isfinite(uv)):
                continue
            sampled += 1
            # One wrap shift for the whole triangle keeps its uv contiguous; the tiled tables cover +-1 period.
            shift = np.floor(uv.min(0))
            uvl = uv - shift
            if np.any(uvl.max(0) >= 2.0):
                for L in a.levels:
                    acc[L][2] += w
                continue
            for L, g in grids.items():
                # micro corners: uv0 + b1 (uv1 - uv0) + b2 (uv2 - uv0)
                c = uvl[0] + g[..., 0:1] * (uvl[1] - uvl[0]) + g[..., 1:2] * (uvl[2] - uvl[0])
                lo, hi = c.min(1), c.max(1)
                x0 = np.floor(lo[:, 0] * W - 0.5).astype(np.int64) + W
                x1 = np.floor(hi[:, 0] * W - 0.5).astype(np.int64) + 1 + W
                y0 = np.floor(lo[:, 1] * H - 0.5).astype(np.int64) + H
                y1 = np.floor(hi[:, 1] * H - 0.5).astype(np.int64) + 1 + H
                ok = (x0 >= 0) & (y0 >= 0) & (x1 < 3 * W) & (y1 < 3 * H)
                area = (x1 - x0 + 1) * (y1 - y0 + 1)
                x0c, x1c = np.clip(x0, 0, 3 * W - 1), np.clip(x1, 0, 3 * W - 1)
                y0c, y1c = np.clip(y0, 0, 3 * H - 1), np.clip(y1, 0, 3 * H - 1)
                opq = ok & (box_count(s_solid, x0c, y0c, x1c, y1c) == area)
                trn = ok & (box_count(s_clear, x0c, y0c, x1c, y1c) == area)
                m = len(g)
                acc[L] += w * np.array([opq.sum(), trn.sum(), m - opq.sum() - trn.sum()]) / m
    norm = sum(acc[a.levels[0]])
    print(f"[omm coverage] {sampled} sampled triangles of {total_tris} (shader {', '.join(a.fs)}), masks at mip 0, hard cut")
    for L in a.levels:
        o, t, u = acc[L] / norm
        mb = total_tris * (4 ** L) * 2 / 8 / 1048576
        print(f"[omm coverage] level {L} ({4 ** L} micro-triangles): known opaque {100 * o:.2f}%, known transparent "
              f"{100 * t:.2f}%, unknown {100 * u:.2f}%; micromaps {mb:.1f} MB for {total_tris} triangles")


if __name__ == "__main__":
    main()
