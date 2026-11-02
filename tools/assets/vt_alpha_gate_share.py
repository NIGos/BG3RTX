"""Option A share (docs/P4_OMM.md 11.1): how many RT queries could use a baked micromap exactly.

    py -3 -m tools.assets.vt_alpha_gate_share --pack <replay pack> --fs 738494714d50b4c7 [--samples 200000]

A micromap baked from mips 0..1 with the hard cut agrees with the RT test only where the test reads mip <= 1
(vt_alpha: mip = floor(log2(N g) - 0.5 + 0.5) for an isotropic footprint, so N g < 4; then k = 0). With
g = footprint x sqrt(area_uv / area_world) that is footprint < 4 x T, T = the triangle's world size of one
mask texel. Footprints of the passes' cones (shaders/gi): shadow rays pa (dist + t), GI rays 2 pa (dist + t),
dist = the receiver's camera distance, t >= 0 the hit distance along the ray. The best case t = 0 is an upper
bound of the share. Inputs from the pack: T per alpha triangle of the shader (InstInfo + pool/soup, uv at +16;
pool positions are object space, instances assumed unscaled), dist per pixel (rtOrigin, s0 b40), pa from the
camera CB rays and gDepth (s0 b0, s0 b5). Prints the T distribution and the share of pixel/triangle pairs under the bound.
"""
import argparse
import math
import struct
from pathlib import Path

import numpy as np

from tools.assets.vt_alpha_agree import manifest_images, records

KINFO_POOL, KINFO_ALPHA = 1 << 8, 1 << 10


def texel_sizes(pack, fs_set, rng, samples):
    recs = records(pack)
    ii = np.frombuffer((Path(pack) / "s1b7.bin").read_bytes(), np.uint32).reshape(-1, 4)
    pool = np.memmap(Path(pack) / "s1b8.bin", np.uint8, "r")
    soup_parts = []
    for line in (Path(pack) / "manifest.txt").read_text().splitlines():
        p = line.split()
        if p[:3] == ["range", "1", "6"]:
            soup_parts.append((int(p[3]), p[4], int(p[5]), int(p[6])))
    soup_file = np.memmap(Path(pack) / soup_parts[0][1], np.uint8, "r")

    def soup_at(off, n):
        for base, _, foff, nbytes in soup_parts:
            if base <= off and off + n <= base + nbytes:
                return soup_file[foff + off - base: foff + off - base + n]
        return None

    out, weight = [], []
    for inst in ii:
        base, meta, rec, tris = int(inst[0]), int(inst[1]), int(inst[2]), int(inst[3])
        if not meta & KINFO_ALPHA or rec < 16 or (rec - 16) % 208:
            continue
        r = recs[(rec - 16) // 208]
        if r["fs"] not in fs_set or r["kind"] != 1 or not tris:
            continue
        stride = meta & 0xFF
        n = min(tris, samples)
        prims = rng.choice(tris, n, replace=False) if tris > n else np.arange(tris)
        N = r["p0"][2] * r["t"][2]   # texture texels across: p0.z x tile-set width (m0 r0 c2)
        for prim in prims:
            o = base + int(prim) * 3 * stride
            raw = pool[o:o + 3 * stride] if meta & KINFO_POOL else soup_at(o, 3 * stride)
            if raw is None or len(raw) < 3 * stride:
                continue
            v = [np.frombuffer(bytes(raw[k * stride:k * stride + 24]), np.float32) for k in range(3)]
            p = [x[:3] for x in v]; uv = [x[4:6] for x in v]
            aw = 0.5 * np.linalg.norm(np.cross(p[1] - p[0], p[2] - p[0]))
            au = 0.5 * abs((uv[1][0] - uv[0][0]) * (uv[2][1] - uv[0][1]) - (uv[2][0] - uv[0][0]) * (uv[1][1] - uv[0][1]))
            if aw > 0 and au > 0 and np.isfinite(aw) and np.isfinite(au):
                out.append(math.sqrt(aw / au) / N)
                weight.append(tris / n)
    return np.array(out), np.array(weight)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--pack", required=True)
    ap.add_argument("--fs", nargs="+", required=True)
    ap.add_argument("--samples", type=int, default=4000, help="triangles sampled per instance")
    a = ap.parse_args()
    rng = np.random.default_rng(1)
    T, w = texel_sizes(a.pack, {int(h, 16) for h in a.fs}, rng, a.samples)
    order = np.argsort(T); cw = np.cumsum(w[order]) / w.sum()
    q = lambda f: T[order][np.searchsorted(cw, f)]  # noqa: E731
    print(f"texel world size T over {len(T)} sampled triangles: p10 {q(0.1) * 1e3:.2f} mm, median {q(0.5) * 1e3:.2f} mm, "
          f"p90 {q(0.9) * 1e3:.2f} mm")
    imgs = manifest_images(a.pack)
    # P = ray_at(uv) x gDepth (gi_trace_rt.hlsl), ray_at = the camera CB's frustum rays (s0 b0 bytes 112..159)
    # in ab space; ab is taken as uv (the viewport CB is not packed): magnitudes only, an estimate.
    cam = np.frombuffer((Path(a.pack) / "s0b0.bin").read_bytes(), np.float32)
    r0, r1, r2 = cam[28:31], cam[32:35], cam[36:39]
    fmt, W, H, _, _, path = imgs[(0, 5, 0)]
    depth = np.fromfile(path, np.float32).reshape(H, W)
    vs, us = np.mgrid[0:H, 0:W]
    u = (us + 0.5) / W; v = (vs + 0.5) / H
    ray = r0 * (1 - u - v)[..., None] + r1 * u[..., None] + r2 * v[..., None]
    dist = (np.linalg.norm(ray, axis=-1) * depth).ravel()
    pa_map = (np.linalg.norm(r1 - r0) / W) / np.linalg.norm(ray, axis=-1).ravel()
    ok = np.isfinite(dist) & (dist > 0) & (dist < 1e4)
    dist, pa_map = dist[ok], pa_map[ok]
    pa = float(np.median(pa_map))
    print(f"receiver distance over {len(dist)} pixels: p10 {np.percentile(dist, 10):.1f} m, median {np.median(dist):.1f} m, "
          f"p90 {np.percentile(dist, 90):.1f} m; pixel angle median {pa * 1e3:.3f} mrad")
    ds = rng.choice(dist, min(len(dist), 20000), replace=False)
    Ts = rng.choice(T, 20000, p=w / w.sum())
    for name, k in (("shadow", 1.0), ("GI", 2.0)):
        f0 = k * pa * ds
        share = np.mean(f0[:, None] < 4.0 * Ts[None, :2000])
        print(f"{name} rays: share of (pixel, foliage triangle) pairs with footprint < 4 T at t = 0 (upper bound): {100 * share:.3f}%")
    for t in (1.0, 5.0):
        f = pa * (ds + t)
        print(f"shadow rays, hit at t = {t} m: {100 * np.mean(f[:, None] < 4.0 * Ts[None, :2000]):.3f}%")


if __name__ == "__main__":
    main()
