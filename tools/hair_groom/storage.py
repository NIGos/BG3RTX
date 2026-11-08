"""Groom storage study (docs/RTX_HAIR_ROADMAP.md 2b): size breakdown of the .hgrm files and a compact encoding.

python -m tools.hair_groom.storage scan [--dir data/assetdb/hair_grooms]
python -m tools.hair_groom.storage codec --sample N [--cache <batch report dir>/cache/groom]

Read-only on the groom files. `scan` reads the LOD headers only. `codec` reads whole files, encodes them with
`encode` (the prototype "HGRZ" layout below), decodes, and reports the ratio, the position error in mm against the
strand radius (the card mesh comes from the batch's groom cache, matched by index id) and the decode time.

HGRZ prototype (all streams little endian, each one zstd-compressed on its own):
  - LOD 0 only. The thinned LODs are recomputed at load: convert.thin keeps strand s when
    hash(s) < keep (nested sets), radius min(r / keep, MAX_THIN_RADIUS) with keep a power of two: bit-exact.
  - strand point counts (u8, 255 = escape to u16 in an extra stream) instead of segment_first_point:
    the segments are every point but each strand's last one (bench_data.segment_starts): lossless.
  - triangle: zigzag delta along the point order (u32 varint-free: int32 delta, byte-shuffled).
  - bary: `bary_bits` fixed point of the 2nd and 3rd weight over [-BARY_PAD, 1 + BARY_PAD], wrapped delta along
    the strand; `bits_for` picks the fewest bits under POS_THRESHOLD_M for a card mesh (32 = lossless f32).
  - radius: 16-bit log2 code (2048 steps per octave), wrapped delta along the strand.
  - root: u8 delta along the strand.
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import struct
import sys
import time
from dataclasses import dataclass
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
SEED = 0x9E3779B9           # convert.thin's default
MAX_THIN_RADIUS = 0.009     # convert.MAX_THIN_RADIUS
LOG_STEPS = 2048            # radius code steps per octave (half step 0.017% relative)
LOG_MIN = -32.0             # log2(radius metres) floor 0.23 nm .. 1 m (the files hold 17 nm .. 2 mm)
BARY_PAD = 1.0 / 512        # barycentrics are quantised over [-pad, 1 + pad]: the files hold -1e-4 .. 1.0001
                            # (parse_lod accepts down to -1e-3); clipping them at 0 moved points by up to 11 um
PIXEL_M = 2 * 0.6 * np.tan(np.radians(10.0)) / 2160   # one 4K pixel at the closest dialogue framing (2b)
POS_THRESHOLD_M = PIXEL_M / 255                        # one 8-bit output level at full contrast: 0.38 um


@dataclass
class Lod:
    keep: float
    triangle: np.ndarray   # (P,) u32
    bary: np.ndarray       # (P, 2) f32
    radius: np.ndarray     # (P,) f32
    root: np.ndarray       # (P,) u8
    segment: np.ndarray    # (S,) u32

    @property
    def points(self):
        return len(self.triangle)


@dataclass
class Hgrm:
    header: bytes          # 40 bytes, plus the UV samples for version 2
    lods: list


def read(path):
    raw = Path(path).read_bytes()
    ver, = struct.unpack_from("<I", raw, 4)
    lods_n, = struct.unpack_from("<I", raw, 32)
    at = 40 + (256 if ver == 2 else 0)
    header = raw[:at]
    lods = []
    for _ in range(lods_n):
        keep, p, s, _z = struct.unpack_from("<f3I", raw, at)
        at += 16
        tri = np.frombuffer(raw, "<u4", p, at); at += 4 * p
        bary = np.frombuffer(raw, "<f4", 2 * p, at).reshape(p, 2); at += 8 * p
        rad = np.frombuffer(raw, "<f4", p, at); at += 4 * p
        root = np.frombuffer(raw, "u1", p, at); at += p
        seg = np.frombuffer(raw, "<u4", s, at); at += 4 * s
        lods.append(Lod(keep, tri, bary, rad, root, seg))
    if at != len(raw):
        raise ValueError(f"{path}: {len(raw) - at} trailing bytes")
    return Hgrm(header, lods)


def write_bytes(h):
    out = [h.header]
    for l in h.lods:
        out.append(struct.pack("<f3I", l.keep, l.points, len(l.segment), 0))
        out += [np.asarray(l.triangle, "<u4").tobytes(), np.asarray(l.bary, "<f4").tobytes(),
                np.asarray(l.radius, "<f4").tobytes(), np.asarray(l.root, "u1").tobytes(), np.asarray(l.segment, "<u4").tobytes()]
    return b"".join(out)


# --- strands ---------------------------------------------------------------------------------------------------
def strand_offsets(points, segment):
    """Offsets (S+1) from segment_first_point: a strand's last point is the one that starts no segment."""
    starts = np.zeros(points, bool)
    starts[np.asarray(segment, np.int64)] = True
    last = np.flatnonzero(~starts)
    return np.concatenate([[0], last + 1]).astype(np.int64)


def segment_starts(offsets):
    last = np.zeros(int(offsets[-1]), bool)
    last[np.asarray(offsets[1:]) - 1] = True
    return np.flatnonzero(~last).astype(np.uint32)


def thin_mask(strands, keep, seed=SEED):
    """convert.thin's strand selection."""
    ids = np.arange(strands, dtype=np.uint64)
    h = (ids * np.uint64(seed) + np.uint64(0x632BE59BD9B4E019)) & np.uint64(0xFFFFFFFF)
    return (h.astype(np.float64) / 2.0 ** 32) < keep


def thin(l0, keep):
    """A thinned LOD from LOD 0, as convert.thin builds it (select, then widen the radii)."""
    off = strand_offsets(l0.points, l0.segment)
    sel = thin_mask(len(off) - 1, keep)
    lengths = np.diff(off)
    pts = np.repeat(sel, lengths)
    new_off = np.concatenate([[0], np.cumsum(lengths[sel])])
    rad = np.minimum(l0.radius[pts] / np.float32(keep), np.float32(MAX_THIN_RADIUS)).astype(np.float32)
    return Lod(np.float32(keep), l0.triangle[pts], l0.bary[pts], rad, l0.root[pts], segment_starts(new_off))


def lods_equal(a, b):
    return (a.keep == b.keep and np.array_equal(a.triangle, b.triangle) and np.array_equal(a.bary, b.bary)
            and np.array_equal(a.radius, b.radius) and np.array_equal(a.root, b.root) and np.array_equal(a.segment, b.segment))


# --- quantisers ------------------------------------------------------------------------------------------------
def quant_bary(bary, bits):
    scale = (1 << bits) - 1
    x = (np.asarray(bary, np.float64) + BARY_PAD) / (1 + 2 * BARY_PAD)
    return np.clip(np.rint(x * scale), 0, scale).astype(np.uint32)


def bits_for(positions, tris, triangles_used, threshold=POS_THRESHOLD_M):
    """Fewest barycentric bits whose worst position error stays under `threshold` on this card mesh (bind pose).

    A point moves by d1 (v1 - v0) + d2 (v2 - v0) with |d1|, |d2| <= step / 2, so by at most step / 2 x (|e1| + |e2|);
    step = (1 + 2 pad) / (2^bits - 1). The batch has the mesh when it writes the file, so the bound holds by
    construction (skinning keeps edge lengths within the pose's stretch)."""
    t = tris[np.unique(np.asarray(triangles_used, np.int64))]
    p = np.asarray(positions, np.float64)
    reach = (np.linalg.norm(p[t[:, 1]] - p[t[:, 0]], axis=1) + np.linalg.norm(p[t[:, 2]] - p[t[:, 0]], axis=1)).max()
    need = reach * (1 + 2 * BARY_PAD) / (2 * threshold)
    return int(min(max(np.ceil(np.log2(need + 1)), 8), 24))


def dequant_bary(q, bits):
    return (q.astype(np.float64) / ((1 << bits) - 1) * (1 + 2 * BARY_PAD) - BARY_PAD).astype(np.float32)


def quant_radius(r):
    code = np.rint((np.log2(np.asarray(r, np.float64)) - LOG_MIN) * LOG_STEPS)
    return np.clip(code, 0, 65535).astype(np.uint16)


def dequant_radius(c):
    return np.exp2(c.astype(np.float64) / LOG_STEPS + LOG_MIN).astype(np.float32)


def _delta(a, off):
    """Delta along each strand (first point of a strand against 0), int64."""
    a = np.asarray(a, np.int64)
    d = np.diff(a, prepend=0)
    d[off[:-1]] = a[off[:-1]]
    return d


def _undelta(d, off):
    d = np.asarray(d, np.int64).copy()
    lengths = np.diff(off)
    # Cumulative sum restarted at every strand start.
    c = np.cumsum(d)
    base = np.repeat(c[off[:-1]] - d[off[:-1]], lengths)
    return c - base


def _zigzag(d):
    return ((d << 1) ^ (d >> 63)).astype(np.uint64)


def _unzigzag(z):
    z = z.astype(np.int64)
    return (z >> 1) ^ -(z & 1)


def _wrap_delta(q, off, bits):
    """Delta along each strand of `bits`-bit codes, wrapped to [-2^(bits-1), 2^(bits-1)) and zigzagged: fits `bits` bits."""
    half = 1 << (bits - 1)
    d = (_delta(q, off) + half) % (1 << bits) - half
    return _zigzag(d)


def _unwrap_delta(z, off, bits):
    return _undelta(_unzigzag(np.asarray(z, np.uint64)), off) & ((1 << bits) - 1)


def _shuffle(z, width):
    """Byte planes of `width`-byte little-endian values (zstd finds the zero high bytes)."""
    b = np.ascontiguousarray(z.astype(f"<u{width}")).view("u1").reshape(-1, width)
    return np.ascontiguousarray(b.T).tobytes()


def _unshuffle(raw, width, n):
    b = np.frombuffer(raw, "u1").reshape(width, n).T
    return np.ascontiguousarray(b).view(f"<u{width}").ravel()


# --- codec -----------------------------------------------------------------------------------------------------
MAGIC = b"HGRZ"


def encode(h, bary_bits=16, lossless_radius=False, level=19, codec="zstd"):
    """HGRZ bytes of LOD 0 (the thinned LODs must equal thin(LOD 0, keep): checked by the caller)."""
    l0 = h.lods[0]
    off = strand_offsets(l0.points, l0.segment)
    lengths = np.diff(off)
    streams = []
    small = np.minimum(lengths, 255).astype(np.uint8)
    streams.append(small.tobytes())
    streams.append(lengths[lengths >= 255].astype("<u2").tobytes())
    streams.append(_shuffle(_zigzag(_delta(l0.triangle, off)), 4))
    if bary_bits >= 32:
        streams.append(np.asarray(l0.bary, "<f4").tobytes())
    else:
        width = 2 if bary_bits <= 16 else 4
        for k in range(2):
            streams.append(_shuffle(_wrap_delta(quant_bary(l0.bary[:, k], bary_bits), off, bary_bits), width))
    if lossless_radius:
        streams.append(_shuffle(_zigzag(_delta(np.asarray(l0.radius, "<f4").view("<u4"), off)), 4))
    else:
        streams.append(_shuffle(_wrap_delta(quant_radius(l0.radius), off, 16), 2))
    streams.append((_delta(l0.root, off) & 0xFF).astype(np.uint8).tobytes())   # mod-256 delta
    keeps = [float(l.keep) for l in h.lods]
    head = MAGIC + struct.pack("<4I", 1, bary_bits, int(lossless_radius), len(keeps)) + struct.pack(f"<{len(keeps)}f", *keeps)
    head += struct.pack("<3I", l0.points, len(lengths), len(h.header)) + h.header
    comp = _compressor(codec, level)
    body = []
    for s in streams:
        c = comp(s)
        body.append(struct.pack("<2Q", len(s), len(c)) + c)
    return head + struct.pack("<I", len(body)) + b"".join(body)


def _compressor(codec, level):
    if codec == "zstd":
        import zstandard
        return zstandard.ZstdCompressor(level=level).compress
    if codec == "lz4":
        import lz4.frame
        return lambda b: lz4.frame.compress(b, compression_level=level)
    if codec == "none":
        return lambda b: b
    raise ValueError(codec)


def _decompressor(codec):
    if codec == "zstd":
        import zstandard
        d = zstandard.ZstdDecompressor()
        return lambda b, n: d.decompress(b, max_output_size=n)
    if codec == "lz4":
        import lz4.frame
        return lambda b, n: lz4.frame.decompress(b)
    return lambda b, n: b


def decode(raw, codec="zstd"):
    """Hgrm with every LOD (the thinned ones rebuilt)."""
    if raw[:4] != MAGIC:
        raise ValueError("not HGRZ")
    _v, bary_bits, lossless_radius, nk = struct.unpack_from("<4I", raw, 4)
    at = 20
    keeps = struct.unpack_from(f"<{nk}f", raw, at); at += 4 * nk
    points, strands, hl = struct.unpack_from("<3I", raw, at); at += 12
    header = raw[at:at + hl]; at += hl
    n, = struct.unpack_from("<I", raw, at); at += 4
    dec = _decompressor(codec)
    streams = []
    for _ in range(n):
        size, csize = struct.unpack_from("<2Q", raw, at); at += 16
        streams.append(dec(raw[at:at + csize], size)); at += csize
    it = iter(streams)
    lengths = np.frombuffer(next(it), "u1").astype(np.int64)
    big = np.frombuffer(next(it), "<u2").astype(np.int64)
    lengths[lengths == 255] = big
    off = np.concatenate([[0], np.cumsum(lengths)])
    tri = _undelta(_unzigzag(_unshuffle(next(it), 4, points)), off).astype(np.uint32)
    if bary_bits >= 32:
        bary = np.frombuffer(next(it), "<f4").reshape(points, 2)
    else:
        width = 2 if bary_bits <= 16 else 4
        bary = np.stack([dequant_bary(_unwrap_delta(_unshuffle(next(it), width, points), off, bary_bits), bary_bits)
                         for _ in range(2)], 1)
    if lossless_radius:
        rad = _undelta(_unzigzag(_unshuffle(next(it), 4, points)), off).astype(np.uint32).view("<f4")
    else:
        rad = dequant_radius(_unwrap_delta(_unshuffle(next(it), 2, points), off, 16))
    root = (_undelta(np.frombuffer(next(it), "u1"), off) & 0xFF).astype(np.uint8)
    l0 = Lod(np.float32(keeps[0]), tri, bary, rad, root, segment_starts(off))
    return Hgrm(header, [l0] + [thin(l0, k) for k in keeps[1:]])


# --- error -----------------------------------------------------------------------------------------------------
def position_error(l0, bary_q, positions, tris):
    """|p(bary) - p(bary_q)| in metres per point in the bind pose, and the point's radius."""
    t = tris[np.asarray(l0.triangle, np.int64)]
    v0, v1, v2 = (positions[t[:, k]].astype(np.float64) for k in range(3))
    d = np.asarray(bary_q, np.float64) - np.asarray(l0.bary, np.float64)
    e = d[:, :1] * (v1 - v0) + d[:, 1:2] * (v2 - v0)
    return np.linalg.norm(e, axis=1)


def tangent_error_deg(l0, bary_q, positions, tris):
    """Angle between each segment's direction with exact and quantised barycentrics (bind pose), degrees."""
    def pts(b):
        t = tris[np.asarray(l0.triangle, np.int64)]
        v0, v1, v2 = (positions[t[:, k]].astype(np.float64) for k in range(3))
        b = np.asarray(b, np.float64)
        return v0 + b[:, :1] * (v1 - v0) + b[:, 1:2] * (v2 - v0)
    s = np.asarray(l0.segment, np.int64)
    p, q = pts(l0.bary), pts(bary_q)
    a, b = p[s + 1] - p[s], q[s + 1] - q[s]
    cos = np.einsum("ij,ij->i", a, b) / np.maximum(np.linalg.norm(a, axis=1) * np.linalg.norm(b, axis=1), 1e-30)
    return np.degrees(np.arccos(np.clip(cos, -1, 1))), np.linalg.norm(a, axis=1)


def screen_threshold(distance_m, fov_v_deg, height_px):
    """Size in metres of one pixel at `distance_m` (vertical field of view, image height in pixels)."""
    return 2 * distance_m * np.tan(np.radians(fov_v_deg) / 2) / height_px


# --- commands --------------------------------------------------------------------------------------------------
def scan(directory):
    rows = []
    for p in sorted(glob.glob(str(Path(directory) / "*.hgrm"))):
        with open(p, "rb") as f:
            h = f.read(40)
            ver, = struct.unpack_from("<I", h, 4)
            lods_n, = struct.unpack_from("<I", h, 32)
            at = 40 + (256 if ver == 2 else 0)
            lods = []
            for _ in range(lods_n):
                f.seek(at)
                keep, pts, segs, _z = struct.unpack("<f3I", f.read(16))
                lods.append((round(keep, 4), pts, segs))
                at += 16 + 17 * pts + 4 * segs
        rows.append({"name": os.path.basename(p), "size": os.path.getsize(p), "lods": lods})
    return rows


def components(rows):
    """Bytes per array over all files and LODs."""
    c = {"triangle": 0, "bary": 0, "radius": 0, "root": 0, "segment": 0, "headers": 0}
    for r in rows:
        c["headers"] += r["size"] - sum(16 + 17 * p + 4 * s for _, p, s in r["lods"])
        for _, p, s in r["lods"]:
            c["headers"] += 16
            c["triangle"] += 4 * p; c["bary"] += 8 * p; c["radius"] += 4 * p; c["root"] += p; c["segment"] += 4 * s
    return c


def index_cache(cache_dir, wanted, log=print):
    """{index id: npz path} for the groom cache files whose card mesh has an index id in `wanted` (reads mesh_tris only)."""
    from . import asset
    out = {}
    files = sorted(glob.glob(str(Path(cache_dir) / "*.npz")))
    t0 = time.monotonic()
    for i, p in enumerate(files):
        try:
            with np.load(p, allow_pickle=False) as z:
                iid = f"{asset.index_id(z['mesh_tris'].astype(np.int64)):016x}"
        except Exception:  # noqa: BLE001
            continue
        if iid in wanted:
            out[iid] = p
            if len(out) == len(wanted):
                break
        if time.monotonic() - t0 > 30:
            log(f"[hair storage] cache index {i + 1}/{len(files)}, {len(out)} matched", flush=True)
            t0 = time.monotonic()
    return out


def codec_study(directory, cache_dir, sample, bits_list=(14, 16, 18), log=print):
    rows = scan(directory)
    rows.sort(key=lambda r: r["size"])
    # Spread over the size distribution: every k-th file.
    pick = [rows[int(i)] for i in np.linspace(0, len(rows) - 1, sample)]
    wanted = {r["name"][:-5] for r in pick}
    meshes = index_cache(cache_dir, wanted, log) if cache_dir else {}
    results = []
    t_last = time.monotonic()
    for i, r in enumerate(pick):
        path = Path(directory) / r["name"]
        h = read(path)
        l0 = h.lods[0]
        derived = all(lods_equal(l, thin(l0, l.keep)) for l in h.lods[1:])
        rec = {"name": r["name"], "size": r["size"], "points": int(l0.points), "derived_lods": bool(derived)}
        # Lossless (no quantisation) and the lossy variants.
        raw = encode(h, bary_bits=32, lossless_radius=True)
        t = time.perf_counter(); back = decode(raw); rec["decode_ms_lossless"] = (time.perf_counter() - t) * 1e3
        rec["lossless_ok"] = derived and all(lods_equal(a, b) for a, b in zip(back.lods, h.lods))
        rec["bytes_lossless"] = len(raw)
        import zstandard
        rec["bytes_zstd_raw"] = len(zstandard.ZstdCompressor(level=9).compress(path.read_bytes()))   # baseline: zstd on the file
        mesh = None
        if r["name"][:-5] in meshes:
            with np.load(meshes[r["name"][:-5]], allow_pickle=False) as z:
                mesh = (z["mesh_positions"].astype(np.float64), z["mesh_tris"].astype(np.int64))
        variants = [(f"q{b}", b) for b in bits_list]
        if mesh is not None:
            variants.append(("qauto", bits_for(mesh[0], mesh[1], l0.triangle)))
        for name, bits in variants:
            t = time.perf_counter(); raw = encode(h, bary_bits=bits); te = (time.perf_counter() - t) * 1e3
            t = time.perf_counter(); back = decode(raw); dt = (time.perf_counter() - t) * 1e3
            q = back.lods[0]
            rel_r = np.abs(q.radius.astype(np.float64) / l0.radius - 1)
            s_ = q.segment.astype(np.int64)
            degenerate = int(np.sum((q.triangle[s_] == q.triangle[s_ + 1]) & np.all(q.bary[s_] == q.bary[s_ + 1], axis=1)))
            e = {"bytes": len(raw), "decode_ms": dt, "encode_ms": te, "radius_rel_max": float(rel_r.max()),
                 "degenerate_segments": degenerate,
                 "root_exact": bool(np.array_equal(q.root, l0.root)), "tri_exact": bool(np.array_equal(q.triangle, l0.triangle))}
            if mesh is not None:
                err = position_error(l0, q.bary, *mesh)
                ratio = err / l0.radius
                ang, seglen = tangent_error_deg(l0, q.bary, *mesh)
                long_ = seglen >= PIXEL_M
                # Length-weighted p99: a segment's share of the drawn hair is its length.
                order_ = np.argsort(ang)
                cw = np.cumsum(seglen[order_]) / max(seglen.sum(), 1e-30)
                e.update(pos_mm_max=float(err.max() * 1e3), pos_mm_p99=float(np.percentile(err, 99) * 1e3),
                         pos_over_threshold=float((err > POS_THRESHOLD_M).mean()),
                         pos_over_radius_max=float(ratio.max()), pos_over_radius_p99=float(np.percentile(ratio, 99)),
                         tangent_deg_max=float(ang.max()), tangent_deg_p99=float(np.percentile(ang, 99)),
                         tangent_deg_max_long=float(ang[long_].max()) if long_.any() else 0.0,
                         tangent_deg_p99_lenw=float(ang[order_][np.searchsorted(cw, 0.99)]),
                         short_segment_share=float(1 - long_.mean()),
                         radius_mm_median=float(np.median(l0.radius) * 1e3), segment_mm_median=float(np.median(seglen) * 1e3))
            e["bits"] = bits
            rec[name] = e
        lz = encode(h, bary_bits=16, codec="lz4", level=12)
        t = time.perf_counter(); decode(lz, codec="lz4"); rec["lz4_q16"] = {"bytes": len(lz), "decode_ms": (time.perf_counter() - t) * 1e3}
        results.append(rec)
        if time.monotonic() - t_last > 30 or i == len(pick) - 1:
            log(f"[hair storage] {i + 1}/{len(pick)} files, {sum(x['size'] for x in results) / 1e9:.2f} GB read", flush=True)
            t_last = time.monotonic()
    return results


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("scan"); s.add_argument("--dir", default=str(ROOT / "data" / "assetdb" / "hair_grooms"))
    s.add_argument("--out")
    c = sub.add_parser("codec"); c.add_argument("--dir", default=str(ROOT / "data" / "assetdb" / "hair_grooms"))
    c.add_argument("--cache"); c.add_argument("--sample", type=int, default=24); c.add_argument("--out")
    a = ap.parse_args(argv)
    if a.cmd == "scan":
        rows = scan(a.dir)
        tot = sum(r["size"] for r in rows)
        print(f"{len(rows)} files, {tot / 1e9:.2f} GB")
        for k, v in components(rows).items():
            print(f"  {k:9s} {v / 1e9:6.2f} GB {v / tot:6.1%}")
        if a.out:
            Path(a.out).write_text(json.dumps(rows), encoding="utf-8")
    else:
        res = codec_study(a.dir, a.cache, a.sample)
        if a.out:
            Path(a.out).write_text(json.dumps(res, indent=1), encoding="utf-8")
        print(json.dumps(summarise(res), indent=1))


def summarise(res):
    tot = sum(r["size"] for r in res)
    out = {"files": len(res), "bytes": tot, "zstd_raw_ratio": tot / sum(r["bytes_zstd_raw"] for r in res),
           "lossless_ratio": tot / sum(r["bytes_lossless"] for r in res), "lossless_all_ok": all(r["lossless_ok"] for r in res),
           "derived_lods_all": all(r["derived_lods"] for r in res)}
    for k in sorted({k for r in res for k in r if k.startswith("q")}):
        rs = [r for r in res if k in r]   # qauto: only the files with a card mesh
        e = [r[k] for r in rs]
        pts = sum(r["points"] for r in rs)
        out[k] = {"files": len(rs), "ratio": sum(r["size"] for r in rs) / sum(x["bytes"] for x in e),
                  "decode_ms_per_mpoint": sum(x["decode_ms"] for x in e) / pts * 1e6,
                  "radius_rel_max": max(x["radius_rel_max"] for x in e),
                  "bits": sorted({x["bits"] for x in e}),
                  "degenerate_segments": sum(x["degenerate_segments"] for x in e),
                  "encode_ms_per_mpoint": sum(x["encode_ms"] for x in e) / pts * 1e6}
        with_mesh = [x for x in e if "pos_mm_max" in x]
        if with_mesh:
            out[k].update(meshes=len(with_mesh), pos_um_max=max(x["pos_mm_max"] for x in with_mesh) * 1e3,
                          pos_um_p99_max=max(x["pos_mm_p99"] for x in with_mesh) * 1e3,
                          pos_over_threshold_max=max(x["pos_over_threshold"] for x in with_mesh),
                          pos_over_radius_max=max(x["pos_over_radius_max"] for x in with_mesh),
                          tangent_deg_max=max(x["tangent_deg_max"] for x in with_mesh),
                          tangent_deg_p99_max=max(x["tangent_deg_p99"] for x in with_mesh),
                          tangent_deg_max_long=max(x["tangent_deg_max_long"] for x in with_mesh),
                          tangent_deg_p99_lenw_max=max(x["tangent_deg_p99_lenw"] for x in with_mesh),
                          short_segment_share_max=max(x["short_segment_share"] for x in with_mesh))
    out["threshold_um"] = POS_THRESHOLD_M * 1e6
    lz = [r["lz4_q16"] for r in res]
    out["lz4_q16"] = {"ratio": tot / sum(x["bytes"] for x in lz),
                      "decode_ms_per_mpoint": sum(x["decode_ms"] for x in lz) / sum(r["points"] for r in res) * 1e6}
    out["decode_ms_lossless_per_mpoint"] = sum(r["decode_ms_lossless"] for r in res) / sum(r["points"] for r in res) * 1e6
    return out


if __name__ == "__main__":
    sys.exit(main())
