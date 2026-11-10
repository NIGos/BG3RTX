"""Per-groom coverage gate audit (docs/RTX_HAIR_PRIMARY.md 5u): the gate's two strand counts against the card count by
pixel footprint, from each style's converter input (.hcap: card mesh, UVs, mask) and its groom (.hgrm v3).

python -m tools.hair_groom.coverage_audit --report-dir <hairconv report dir> --hcap-dir <dir> --grooms-dir <dir>
    --out <dir> [--jobs 4] [--limit N]

The test card of primary.footprint_report, on the real card: the card seen frontally in its own texture space. One
stratified sample per half texel (the ray of one sub-pixel jitter); a sample counts when a card triangle covers it.
Card marker: the dithered native alpha test keeps a sample with the mask alpha filtered to the footprint (box mips,
bilinear, f texels per pixel), so its expectation is that alpha. Strand: the sample lies within a strand's radius of a
strand segment (points = barycentric . card triangle in UV, radius converted to texels by the triangle's world/UV area
scale). Per footprint f: C = sum alpha_f, S = sum alpha_f * strand (inside the card markers, the old count), U = sum
strand (every accepted strand pixel, rt_hair_coverage_union). U does not depend on f. Ignored: the 3-D view (card
overlap along a ray, the grazing factor), anisotropic UV stretch: texture-space U/C overestimates the 3-D ratio
(median 1.18 against 0.99 over 898 grooms), so the gate's U/C comes from the converter's per-view `coverage_ratio`
and only the relative S/C fall with the footprint from here. Cached per style (key: input sizes and mtimes);
a progress line every 30 s; BelowNormal priority.
"""
from __future__ import annotations

import argparse
import json
import math
import time
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

import numpy as np

FOOTPRINTS = (1, 2, 4, 8, 16)
K_COVER_MIN = 0.8913102349775665
K_COVER_MAX = 1.0752640637962045 + 0.01923694227747121
GRID = 1   # samples per texel and axis (2 for a finer strand edge: 4x the time)


def _mips(alpha):
    out = [alpha.astype(np.float32)]
    while min(out[-1].shape) > 1 and len(out) <= int(math.log2(max(FOOTPRINTS))):
        a = out[-1]
        h, w = a.shape[0] // 2 * 2, a.shape[1] // 2 * 2
        out.append(a[:h, :w].reshape(h // 2, 2, w // 2, 2).mean((1, 3)))
    return out


def _bilinear(m, x, y):
    h, w = m.shape
    x, y = x - 0.5, y - 0.5
    x0, y0 = np.floor(x).astype(np.int64), np.floor(y).astype(np.int64)
    fx, fy = (x - x0).astype(np.float32), (y - y0).astype(np.float32)
    at = lambda i, j: m[np.mod(j, h), np.mod(i, w)]
    return ((at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx) * (1 - fy) + (at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx) * fy)


def measure(hcap_path, groom_path, seed=0):
    from PIL import Image, ImageDraw
    from . import hcap, storage
    mesh, mask, _head, _grid, _mat = hcap.read(hcap_path)
    groom = storage.decode_v3(Path(groom_path).read_bytes())
    l0 = groom.lods[0]
    th, tw = mask.shape[:2]
    gw, gh = tw * GRID, th * GRID
    uvg = mesh.uv.astype(np.float64) * np.array([gw, gh])
    tris = mesh.tris
    img = Image.new("L", (gw, gh), 0)
    d = ImageDraw.Draw(img)
    for t in tris:
        d.polygon([tuple(uvg[t[0]]), tuple(uvg[t[1]]), tuple(uvg[t[2]])], fill=1)
    region = np.asarray(img, bool)
    # Strand points in grid units and their radius in grid units (the card triangle's world-per-grid-unit scale).
    tri = l0.triangle.astype(np.int64)
    b = l0.bary.astype(np.float64)
    w0 = 1.0 - b[:, 0] - b[:, 1]
    tv = tris[tri]
    puv = uvg[tv[:, 0]] * w0[:, None] + uvg[tv[:, 1]] * b[:, :1] + uvg[tv[:, 2]] * b[:, 1:2]
    P, T = mesh.positions[tris].astype(np.float64), uvg[tris]
    aw = 0.5 * np.linalg.norm(np.cross(P[:, 1] - P[:, 0], P[:, 2] - P[:, 0]), axis=1)
    e1, e2 = T[:, 1] - T[:, 0], T[:, 2] - T[:, 0]
    au = 0.5 * np.abs(e1[:, 0] * e2[:, 1] - e1[:, 1] * e2[:, 0])
    scale = np.sqrt(aw / np.maximum(au, 1e-30))          # metres per grid unit
    rg = l0.radius.astype(np.float64) / np.maximum(scale[tri], 1e-30)
    i0 = l0.segment.astype(np.int64)
    a, c = puv[i0], puv[i0 + 1]
    r = 0.5 * (rg[i0] + rg[i0 + 1])
    ok = np.isfinite(r) & (r < 64) & np.all(np.isfinite(a), 1) & np.all(np.isfinite(c), 1)
    a, c, r = a[ok], c[ok], r[ok]
    rng = np.random.default_rng(seed)
    jit = rng.random((2, gh, gw), dtype=np.float32)       # one stratified sample per cell
    hit = np.zeros((gh, gw), bool)
    x0 = np.clip(np.floor(np.minimum(a[:, 0], c[:, 0]) - r).astype(np.int64), 0, gw - 1)
    x1 = np.clip(np.floor(np.maximum(a[:, 0], c[:, 0]) + r).astype(np.int64), 0, gw - 1)
    y0 = np.clip(np.floor(np.minimum(a[:, 1], c[:, 1]) - r).astype(np.int64), 0, gh - 1)
    y1 = np.clip(np.floor(np.maximum(a[:, 1], c[:, 1]) + r).astype(np.int64), 0, gh - 1)
    nx, ny = x1 - x0 + 1, y1 - y0 + 1
    cells = nx * ny
    start = 0
    while start < len(cells):   # chunks of about 8M cell tests
        end = start + max(1, int(np.searchsorted(np.cumsum(cells[start:]), 8_000_000)))
        s = np.arange(start, end)
        n = cells[s]
        idx = np.repeat(s, n)
        k = np.arange(n.sum()) - np.repeat(np.cumsum(n) - n, n)
        cx = x0[idx] + k % nx[idx]
        cy = y0[idx] + k // nx[idx]
        px, py = cx + jit[0, cy, cx], cy + jit[1, cy, cx]
        ax, ay, bx, by = a[idx, 0], a[idx, 1], c[idx, 0], c[idx, 1]
        dx, dy = bx - ax, by - ay
        L = dx * dx + dy * dy
        t = np.clip(((px - ax) * dx + (py - ay) * dy) / np.maximum(L, 1e-30), 0, 1)
        dd = (ax + t * dx - px) ** 2 + (ay + t * dy - py) ** 2
        m = dd <= r[idx] ** 2
        hit[cy[m], cx[m]] = True
        start = end
    ys, xs = np.nonzero(region)
    sx, sy = (xs + jit[0, ys, xs]) / GRID, (ys + jit[1, ys, xs]) / GRID   # texel coordinates of the samples
    h = hit[ys, xs]
    mips = _mips(mask[..., 3].astype(np.float32) / 255.0)
    out = {"tris": int(len(tris)), "mask": [int(tw), int(th)], "samples": int(len(xs)), "U": float(h.sum()),
           "strand_alpha": float(_bilinear(mips[0], puv[:, 0] / GRID, puv[:, 1] / GRID).mean()), "fp": {}}
    for f in FOOTPRINTS:
        lv = min(int(round(math.log2(f))), len(mips) - 1)
        al = _bilinear(mips[lv], sx / 2 ** lv, sy / 2 ** lv)
        out["fp"][str(f)] = {"C": float(al.sum()), "S": float(al[h].sum())}
    return out


def _key(*paths):
    return "|".join(f"{p.name}:{p.stat().st_size}:{int(p.stat().st_mtime)}" for p in paths)


def _job(name, hcap_path, groom_path):
    try:
        return name, _key(Path(hcap_path), Path(groom_path)), measure(hcap_path, groom_path), None
    except Exception as e:   # one style's failure is reported, the run goes on
        return name, None, None, f"{type(e).__name__}: {e}"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--report-dir", type=Path, required=True)
    ap.add_argument("--hcap-dir", type=Path, required=True)
    ap.add_argument("--grooms-dir", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--limit", type=int, default=0)
    args = ap.parse_args()
    from ..low_priority import lower_priority
    lower_priority()
    args.out.mkdir(parents=True, exist_ok=True)
    cache_path = args.out / "coverage_audit.json"
    cache = json.loads(cache_path.read_text()) if cache_path.exists() else {}
    records = {}
    for line in (args.report_dir / "all_state.json").read_text(encoding="utf-8").splitlines():
        parts = line.split("\t")
        if len(parts) == 3:
            records[parts[0]] = json.loads(parts[2])
    todo = []
    for name, rec in records.items():
        hp = args.hcap_dir / (name.replace(":", "_") + ".hcap")
        gp = args.grooms_dir / str(rec.get("asset", ""))
        if not rec.get("asset") or not hp.exists() or not gp.exists():
            continue
        c = cache.get(name)
        if c and c.get("key") == _key(hp, gp):
            continue
        todo.append((name, str(hp), str(gp)))
    if args.limit:
        todo = todo[:args.limit]
    print(f"[coverage audit] {len(records)} styles, {len(cache)} cached, {len(todo)} to measure", flush=True)
    t0 = last = time.monotonic()
    done = fails = 0
    with ProcessPoolExecutor(args.jobs) as ex:
        for fut in as_completed([ex.submit(_job, *t) for t in todo]):
            name, key, res, err = fut.result()
            done += 1
            if err:
                fails += 1
                print(f"[coverage audit] FAIL {name}: {err}", flush=True)
            else:
                cache[name] = {"key": key, **res}
            now = time.monotonic()
            if now - last >= 30 or done == len(todo):
                rate = done / max(now - t0, 1e-9)
                print(f"[coverage audit] {done}/{len(todo)}, {rate * 60:.1f}/min, ETA {(len(todo) - done) / max(rate, 1e-9):.0f} s, "
                      f"failures {fails}", flush=True)
                cache_path.write_text(json.dumps(cache))
                last = now
    cache_path.write_text(json.dumps(cache))
    out = []
    for name, rec in records.items():
        c = cache.get(name)
        row = {"name": name, "verdict": rec.get("verdict"), "reasons": rec.get("reasons"), "indices": rec.get("indices"),
               "asset": rec.get("asset"), "ratio_3d": rec.get("coverage_ratio"), "delta_3d": rec.get("coverage_delta")}
        if c:   # texture space: U/C (no view term: an upper estimate) and the S/C fall with the footprint
            row["uc_tex"] = c["U"] / max(c["fp"]["1"]["C"], 1e-9)
            row["sc_tex"] = {f: v["S"] / max(v["C"], 1e-9) for f, v in c["fp"].items()}
            row["strand_alpha"] = c["strand_alpha"]
        out.append(row)
    (args.out / "coverage_table.json").write_text(json.dumps(out, indent=0))
    # The gate's quantity is the converter's 3-D composited ratio (coverage_bound.py's basis), per view.
    bad = [r for r in out if r["ratio_3d"] and any(x < K_COVER_MIN or x > K_COVER_MAX for x in r["ratio_3d"])]
    print(f"[coverage audit] {len(out)} styles, {len(cache)} measured in texture space; 3-D ratio outside "
          f"[{K_COVER_MIN:.4f}, {K_COVER_MAX:.4f}] in some view: {len(bad)}", flush=True)
    return bool(fails)


if __name__ == "__main__":
    raise SystemExit(main())
