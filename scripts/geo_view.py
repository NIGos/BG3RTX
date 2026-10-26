"""Track D: inspect a captured G-buffer geometry frame (captures/<ts>/geo/scene.bin).

    py scripts\\geo_view.py captures\\<ts>               summary + top-down preview (geo/topdown.png)
    py scripts\\geo_view.py captures\\<ts> --obj         also write geo/scene.obj (world space, one group per draw)
    py scripts\\geo_view.py captures\\<ts> --obj --max-tris 2000000

scene.bin layout (little endian, written by layer/geocapture.cpp):
    u32 magic 'BG3G', version, vertex_count, draw_count, width, height, flags (1 = world valid), 0
    f32 camera_ubo[64]          set 0 / binding 2 of the G-buffer VS: _m0, _m1, _m2 (ViewProj, row-major) ...
    f32 viewport[6], u32 pad[2]
    draw[draw_count]            u32 first_vertex, vertex_count; u64 vs, fs; u32 pipe, kind, count, instances, skip, pad
    f32 clip[vertex_count][4]   gl_Position as written by the game (y already negated)
    f32 world[vertex_count][3]  camera-relative world position (if flags & 1)
Vertices are a triangle soup: every 3 consecutive vertices are one triangle.
"""
import argparse
import os
import struct
import sys
import zlib

import numpy as np

DRAW = np.dtype([("first", "<u4"), ("count", "<u4"), ("vs", "<u8"), ("fs", "<u8"), ("pipe", "<u4"),
                 ("kind", "<u4"), ("n", "<u4"), ("inst", "<u4"), ("skip", "<u4"), ("pad", "<u4")])


def load(path):
    raw = open(path, "rb").read()
    hdr = struct.unpack_from("<8I", raw, 0)
    if hdr[0] != 0x47334742:
        sys.exit("not a scene.bin")
    nvert, ndraw, w, h, flags = hdr[2], hdr[3], hdr[4], hdr[5], hdr[6]
    ubo = np.frombuffer(raw, "<f4", 64, 32)
    vp = np.frombuffer(raw, "<f4", 6, 288)
    off = 320
    draws = np.frombuffer(raw, DRAW, ndraw, off)
    off += ndraw * DRAW.itemsize
    clip = np.frombuffer(raw, "<f4", nvert * 4, off).reshape(-1, 4)
    off += nvert * 16
    world = np.frombuffer(raw, "<f4", nvert * 3, off).reshape(-1, 3) if flags & 1 else None
    return dict(w=w, h=h, ubo=ubo, vp=vp, draws=draws, clip=clip, world=world)


def write_png(path, rgb):
    h, w, _ = rgb.shape
    rows = b"".join(b"\0" + rgb[y].tobytes() for y in range(h))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


def topdown(world, path, size=1024):
    """Orthographic view from above (world Y up): brightness = height of the highest triangle centroid."""
    c = world.reshape(-1, 3, 3).mean(axis=1)
    c = c[np.isfinite(c).all(axis=1)]
    lo, hi = np.percentile(c, 1, axis=0), np.percentile(c, 99, axis=0)
    ext = max(hi[0] - lo[0], hi[2] - lo[2], 1e-3)
    x = ((c[:, 0] - lo[0]) / ext * (size - 1)).astype(int)
    z = ((c[:, 2] - lo[2]) / ext * (size - 1)).astype(int)
    ok = (x >= 0) & (x < size) & (z >= 0) & (z < size)
    hgt = np.full((size, size), -np.inf, np.float32)
    np.maximum.at(hgt, (z[ok], x[ok]), c[ok, 1])
    v = np.where(np.isfinite(hgt), (hgt - lo[1]) / max(hi[1] - lo[1], 1e-3), 0)
    g = (np.clip(v, 0, 1) * 215 + np.isfinite(hgt) * 40).astype(np.uint8)
    write_png(path, np.stack([g, g, g], -1))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("capture")
    ap.add_argument("--obj", action="store_true")
    ap.add_argument("--max-tris", type=int, default=0)
    a = ap.parse_args()
    geo = os.path.join(a.capture, "geo")
    s = load(os.path.join(geo, "scene.bin"))
    d = s["draws"]
    print(f"{len(d)} draws, {int((d['count'] > 0).sum())} with triangles, {len(s['clip']) // 3} triangles, "
          f"{len(np.unique(d['vs']))} vertex shaders, {s['w']}x{s['h']}")
    if s["world"] is None:
        print("no world positions (camera UBO not captured)")
        return
    wv = s["world"]
    print("world bbox", wv.min(0), wv.max(0))
    topdown(wv, os.path.join(geo, "topdown.png"))
    print("wrote", os.path.join(geo, "topdown.png"))
    if a.obj:
        with open(os.path.join(geo, "scene.obj"), "w") as f:
            f.write("# BG3RTX G-buffer geometry, camera-relative world space\n")
            budget = a.max_tris or 1 << 62
            for v in wv:
                f.write(f"v {v[0]:.4f} {v[1]:.4f} {v[2]:.4f}\n")
            for i, r in enumerate(d):
                if not r["count"] or budget <= 0:
                    continue
                f.write(f"g d{i}_vs{int(r['vs']):016x}\n")
                n = min(int(r["count"]) // 3, budget)
                budget -= n
                base = int(r["first"]) + 1
                for t in range(n):
                    k = base + 3 * t
                    f.write(f"f {k} {k + 1} {k + 2}\n")
        print("wrote", os.path.join(geo, "scene.obj"))


if __name__ == "__main__":
    main()
