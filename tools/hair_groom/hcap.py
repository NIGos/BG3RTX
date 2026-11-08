""".hcap: one hair style's converter input, without GR2 (docs/RTX_HAIR_ROADMAP.md 2c, 2d), and the Python reference
pipeline the native converter (tools/hairconv, bg3rtx_hairconv.exe) is checked against.

Layout (little endian; version 1):
  'HCAP', version 1, index count N, vertex count V, mask width W, mask height H, flags, grid columns, grid rows,
  material hash u64, head centre f32[3], head radii f32[3]
  indices u32[N] (native vertex numbering), positions f32[V*3], uv f32[V*2], mask RGBA8[W*H*4]
flags: 1 = head proxy given (else the converter fits it to the strand roots), 2 = grid given (LOD1: its LOD0's grid)
The layer's capture (2c) stores per-corner soup; capture.from_capture turns that into the vertex arrays written here.

python -m tools.hair_groom.hcap export --cache <batch cache>/groom --out <dir> [--limit N] [--curly K]
    cached GR2 meshes + a synthetic mask (no game texture is on the dev PC) -> .hcap files (dev input)
python -m tools.hair_groom.hcap reference <file.hcap>... --out <dir>
    the batch's process() steps on an .hcap -> <index id>.hgrm (v3) + <name>.json (verdict, counters)
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import struct
import sys
import time
from pathlib import Path

import numpy as np

MAGIC = b"HCAP"
VERSION = 1
HEAD = struct.Struct("<4s8IQ6f")
FLAG_HEAD, FLAG_GRID = 1, 2


def write(path, tris, positions, uv, mask, head=None, grid=None, material=0):
    tris = np.ascontiguousarray(np.asarray(tris).ravel(), "<u4")
    positions = np.ascontiguousarray(positions, "<f4").reshape(-1, 3)
    uv = np.ascontiguousarray(uv, "<f4").reshape(-1, 2)
    mask = np.ascontiguousarray(mask, np.uint8)
    h, w = mask.shape[:2]
    flags = (FLAG_HEAD if head is not None else 0) | (FLAG_GRID if grid is not None else 0)
    hc, hr = (head if head is not None else (np.zeros(3), np.zeros(3)))
    g = grid or (0, 0)
    with open(path, "wb") as f:
        f.write(HEAD.pack(MAGIC, VERSION, len(tris), len(positions), w, h, flags, int(g[0]), int(g[1]), material,
                          *[float(x) for x in hc], *[float(x) for x in hr]))
        f.write(tris.tobytes()); f.write(positions.tobytes()); f.write(uv.tobytes()); f.write(mask.tobytes())


def read(path):
    raw = Path(path).read_bytes()
    magic, ver, n, v, w, h, flags, gx, gy, material, *hh = HEAD.unpack_from(raw, 0)
    if magic != MAGIC or ver != VERSION:
        raise ValueError(f"{path}: not an .hcap v1")
    at = HEAD.size
    tris = np.frombuffer(raw, "<u4", n, at).astype(np.int64).reshape(-1, 3); at += 4 * n
    pos = np.frombuffer(raw, "<f4", 3 * v, at).reshape(v, 3); at += 12 * v
    uv = np.frombuffer(raw, "<f4", 2 * v, at).reshape(v, 2); at += 8 * v
    mask = np.frombuffer(raw, np.uint8, w * h * 4, at).reshape(h, w, 4); at += w * h * 4
    if at != len(raw):
        raise ValueError(f"{path}: {len(raw) - at} trailing bytes")
    from . import mesh
    m = mesh.Mesh(positions=pos.copy(), uv=uv.copy(), joints=np.zeros((v, 4), np.int64), weights=np.zeros((v, 4), np.float32),
                  tris=tris, joint_names=[], joint_positions=np.zeros((0, 3), np.float32))
    head = (np.array(hh[:3], float), np.array(hh[3:], float)) if flags & FLAG_HEAD else None
    grid = (gx, gy) if flags & FLAG_GRID else None
    return m, mask.copy(), head, grid, material


def curly_mask(size=(1024, 1024), n=180, seed=3):
    """A synthetic curly atlas (root channel not along the rows): the 2-D tracer's input."""
    H, W = size
    m = np.zeros((H, W, 4), np.uint8)
    m[..., 1] = 128
    rng = np.random.default_rng(seed)
    for k in range(n):
        cx, cy = rng.uniform(40, W - 40), rng.uniform(40, H - 40)
        r0, a0 = rng.uniform(8, 30), rng.uniform(0, 2 * np.pi)
        t = np.linspace(0, rng.uniform(2, 5) * np.pi, 400)
        x = cx + (r0 + 3 * t) * np.cos(a0 + t)
        y = cy + (r0 + 3 * t) * np.sin(a0 + t) * 0.6 + 4 * t
        ok = (x >= 1) & (x < W - 2) & (y >= 1) & (y < H - 2)
        x, y, tt = x[ok], y[ok], t[ok]
        for dx in (0, 1):
            for dy in (0, 1):
                xi, yi = (x + dx).astype(int), (y + dy).astype(int)
                m[yi, xi, 3] = 255
                m[yi, xi, 2] = (255 * tt / max(tt.max(), 1e-6)).astype(np.uint8)
                m[yi, xi, 0] = (k * 53) % 255
    return m


def export(cache_dir, out_dir, limit, curly=0, log=print):
    """Dev input: cached meshes (bind pose, native indices, Head_M proxy) + a synthetic mask with the style's strand
    counts (capture.synthetic_mask; the first `curly` files get a curly atlas instead)."""
    from . import capture
    files = sorted(glob.glob(str(Path(cache_dir) / "*.npz")), key=os.path.getsize)
    pick = [files[int(i)] for i in np.linspace(0, len(files) - 1, limit)] if len(files) > limit else files
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    out = []
    for i, p in enumerate(pick):
        got = capture.load(p)
        if got is None:
            continue
        (groom, hair, rep, dropped, grid, fit, root_corr, head), rep_raw = got
        mask = curly_mask() if i < curly else capture.synthetic_mask(rep_raw, tuple(int(x) for x in grid), size=(1024, 1024))
        name = out_dir / (Path(p).stem[:16] + (".curly" if i < curly else "") + ".hcap")
        write(name, hair.tris, hair.positions, hair.uv, mask, head=(head[0], head[1]))
        out.append(str(name))
        log(f"[hcap] {name.name}: {len(hair.tris)} triangles, mask {mask.shape[1]}x{mask.shape[0]}", flush=True)
    return out


def reference(path, out_dir, accept_review=False, timings=None, always_write=False):
    """batch.process() on an .hcap (the dev reference of bg3rtx_hairconv): grid, root check, convert, dedupe, head,
    orientation / reorient, validation renders, verdict, v3 write. Returns the record."""
    from . import analysis, asset, batch, capture, convert, render
    t = timings if timings is not None else {}

    def lap(name, t0):
        t[name] = t.get(name, 0.0) + time.perf_counter() - t0
        return time.perf_counter()
    s = time.perf_counter()
    hair, mask, head, grid_given, _ = read(path)
    rec = {"name": Path(path).stem}
    s = lap("read", s)
    grid, fit = convert.detect_tile_grid(hair)
    if grid_given:
        grid = grid_given
    root_corr = analysis.root_channel_check(mask, grid)
    s = lap("grid", s)
    groom, rep = convert.convert(hair, mask, tile_grid=grid)
    if rep.get("tracer") == "2d":
        root_corr = rep["strand_root_corr"]
    s = lap("trace", s)
    groom, dropped = convert.dedupe(groom)
    s = lap("dedupe", s)
    if head is None:
        head = capture.head_from_roots(groom.points, groom.root_param, groom.offsets)
    bad_root = bool(root_corr) and np.median(root_corr) < batch.ROOT_REJECT
    geometry_root = bad_root and head[1][0] > 0.01
    if geometry_root:
        decided = analysis.orientation(groom, head[0])["strands_decided"]
        groom, reversed_ = analysis.reorient(groom, head[0])
        rec.update(root_source="geometry", root_reversed_strands=reversed_, root_decided_strands=decided)
    orient = analysis.orientation(groom, head[0]) if head[1][0] > 0.01 else {}
    s = lap("orientation", s)
    unmapped = rep["unmapped_samples"] / max(rep["samples"], 1)
    rec.update(tile_grid=list(grid), cards=rep["cards"], triangles=rep["triangles"], cards_in_one_tile=round(fit, 4),
               mixed_winding_cards=rep["mixed_winding_cards"], uv_overlap_samples=rep["uv_overlap_samples"],
               unmapped_share=round(unmapped, 5), root_row_corr_median=round(float(np.median(root_corr)), 3) if root_corr else None,
               strands=groom.strands, segments=groom.segments, dedupe_dropped=dropped,
               reversed_cards=orient.get("cards_mostly_reversed"), tracer=rep.get("tracer", "rows"))
    ious, cover, light = [], [], []
    for _name, (view, light_dir) in batch.bbox_views(hair.positions, 0.002).items():
        fibre_cards, plain_cards = render.card_fragment_builders(hair.positions, hair.uv, hair.tris, mask)
        _, cov_c = render.render(fibre_cards, view, light_dir, head)
        img_c, cov_p = render.render(plain_cards, view, light_dir, head)
        img_g, cov_g = render.render(lambda v: render.strand_fragments(groom.points, groom.radii, groom.offsets, groom.root_param, v),
                                     view, light_dir, head)
        px = (cov_c > 0.02) | (cov_g > 0.02)
        ious.append(float(((cov_c > 0.5) & (cov_g > 0.5)).sum() / max(((cov_c > 0.5) | (cov_g > 0.5)).sum(), 1)))
        cover.append(float(cov_g[px].mean() - cov_c[px].mean()) if px.any() else 0.0)
        pxr = (cov_p > 0.02) | (cov_g > 0.02)
        light.append(float(img_g[pxr].mean() / max(img_c[pxr].mean(), 1e-9) - 1) if pxr.any() else 0.0)
    rec.update(iou=[round(x, 3) for x in ious], coverage_delta=[round(x, 3) for x in cover],
               radiance_delta=[round(x, 3) for x in light])
    s = lap("validation", s)
    rec["verdict"], rec["reasons"] = verdict(groom, rep, root_corr, bad_root, geometry_root,
                                             rec.get("root_decided_strands", 0), ious, cover, light, orient)
    if always_write or rec["verdict"] == "pass" or (rec["verdict"] == "needs-review" and accept_review):
        out_dir = Path(out_dir)
        out_dir.mkdir(parents=True, exist_ok=True)
        dst = out_dir / f"{asset.index_id(hair.tris):016x}.hgrm"
        cid, _ = asset.content_id(hair.positions, hair.tris)
        rec["asset_bits"] = asset.write(dst, cid, hair, [(k, convert.thin(groom, k)) for k in asset.LODS])
        rec["asset"] = dst.name
    lap("write", s)
    rec["seconds"] = {k: round(v, 3) for k, v in t.items()}
    return rec


def verdict(groom, rep, root_corr, bad_root, geometry_root, decided, ious, cover, light, orient):
    """batch.process()'s verdict rules (the same thresholds)."""
    from . import batch
    reasons, review = [], []
    if groom.strands == 0:
        reasons.append("no strands")
    if bad_root and not geometry_root:
        reasons.append(f"root channel does not run along the rows (median corr {np.median(root_corr):.2f})")
    if geometry_root and decided < groom.strands / 2:
        review.append(f"root from geometry (mask root channel does not run along the rows, median corr {np.median(root_corr):.2f})")
    if min(ious) < batch.IOU_REJECT:
        reasons.append(f"silhouette IoU {min(ious):.2f}")
    if max(abs(x) for x in cover) > batch.COVER_REVIEW:
        review.append(f"coverage differs by {max(cover, key=abs):+.2f}")
    if max(abs(x) for x in light) > batch.RADIANCE_REVIEW:
        review.append(f"mean radiance differs by {max(light, key=abs):+.0%}")
    if rep["mixed_winding_cards"] > batch.MIXED_REVIEW * max(rep["cards"], 1):
        review.append(f"{rep['mixed_winding_cards']} cards with mirrored UV triangles")
    if min(ious) < batch.IOU_PASS:
        review.append(f"silhouette IoU {min(ious):.2f}")
    if orient.get("cards_decided") and orient["cards_mostly_reversed"] > batch.REVERSED_REVIEW * orient["cards_decided"]:
        review.append(f"{orient['cards_mostly_reversed']} cards with the root away from the head")
    return ("reject" if reasons else "needs-review" if review else "pass"), reasons + review


FIXTURE = Path(__file__).resolve().parents[2] / "tests" / "fixtures" / "hairconv"


def fixture():
    """tests/hairconv_test.cpp's fixture: synthetic cards (no game data) as .hcap, the Python reference's v3 groom and
    record. Rows of 8 cards across a 2048 x 512 atlas of wavy strands; a head proxy behind the cards."""
    from . import mesh
    n, rows, length, width = 8, 6, 0.30, 0.08
    pos, uv, tris = [], [], []
    for c in range(n):
        base = len(pos)
        for r in range(rows + 1):
            v = r / rows
            for u in (0.0, 1.0):
                pos.append([c * 0.09 + (u - 0.5) * width, 1.7 - v * length, 0.06 * np.sin(c) + 0.1 * v * v])
                uv.append([(c + u) / n, v])
        for r in range(rows):
            i = base + 2 * r
            tris += [[i, i + 1, i + 2], [i + 1, i + 3, i + 2]]
    H, W = 128, 512
    m = np.zeros((H, W, 4), np.uint8)
    m[..., 1] = 128
    rng = np.random.default_rng(11)
    for tx in range(n):
        for k in range(6):
            x0 = tx * 64 + 4 + k * 10 + rng.uniform(0, 2)
            ys = np.arange(2, H - 2)
            xs = x0 + 1.5 * np.sin(ys / 17.0 + k)
            for dx in range(3):
                xi = np.clip((xs + dx).astype(int), 0, W - 1)
                m[ys, xi, 3] = 255
                m[ys, xi, 2] = (ys * 255 // H).astype(np.uint8)
    FIXTURE.mkdir(parents=True, exist_ok=True)
    src = FIXTURE / "cards.hcap"
    write(src, np.array(tris), np.array(pos, np.float32), np.array(uv, np.float32), m,
          head=(np.array([0.175, 1.62, 0.09]), np.array([0.066, 0.089, 0.083])))
    for old in FIXTURE.glob("*.hgrm"):
        old.unlink()
    rec = reference(src, FIXTURE, always_write=True)
    rec.pop("seconds", None)
    (FIXTURE / "cards.json").write_text(json.dumps(rec, indent=1) + "\n", encoding="utf-8")
    print(f"[hcap] fixture {src}: {rec['verdict']}, {rec['strands']} strands, asset {rec.get('asset')}")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("fixture")
    e = sub.add_parser("export"); e.add_argument("--cache", required=True); e.add_argument("--out", required=True)
    e.add_argument("--limit", type=int, default=24); e.add_argument("--curly", type=int, default=0)
    r = sub.add_parser("reference"); r.add_argument("files", nargs="+"); r.add_argument("--out", required=True)
    r.add_argument("--accept-review", action="store_true")
    a = ap.parse_args(argv)
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    from tools.low_priority import lower_priority
    lower_priority()
    if a.cmd == "fixture":
        fixture()
        return 0
    if a.cmd == "export":
        export(a.cache, a.out, a.limit, a.curly)
        return 0
    for f in a.files:
        rec = reference(f, a.out, a.accept_review)
        Path(a.out).mkdir(parents=True, exist_ok=True)
        (Path(a.out) / (Path(f).stem + ".json")).write_text(json.dumps(rec, indent=1), encoding="utf-8")
        print(json.dumps({k: rec[k] for k in ("name", "verdict", "strands", "seconds") if k in rec}), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
