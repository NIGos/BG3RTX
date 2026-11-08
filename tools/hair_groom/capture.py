"""Runtime capture as the converter's source (docs/RTX_HAIR_ROADMAP.md 2c): a hair style's card mesh rebuilt from what
the layer sees when the game draws it, instead of the GR2 (granny2.dll / LSLib cannot ship).

A capture is the draw's index range (copied from the index buffer, host-mapped or by a GPU copy), its transform
feedback soup (per corner, in index order: post-skin, post-wind world position at byte 0, UV0 at byte 16; stride 88,
NATIVE_HAIR.md "Capture and query implementation") and the bound mask texture (set 1 binding 4 of the CHAR_Hair
material, mip 0, read back raw and BCn-decoded). from_capture() turns it into the mesh.Mesh the converter takes:
vertex v's position and UV come from any corner that references v (the vertex shader is deterministic, so every
corner of one vertex carries the same values). Without indices (weld=True) corners with identical position and UV
become one vertex. Skeleton data is not captured: joints/weights are zero (only the old bench groom used them) and
the head proxy comes from head_from_roots() instead of the Head_M joint.

python -m tools.hair_groom.capture equivalence --cache <batch cache>/groom [--limit N]
python -m tools.hair_groom.capture stages --cache <batch cache>/groom [--limit N] [--state <state.json>]
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import sys
import time
from pathlib import Path

import numpy as np

from . import mesh as meshmod

SOUP_STRIDE = 88       # bytes per captured corner (layer opt-in hair stream)
SOUP_UV = 16           # UV0 offset in the stride


def soup_from_mesh(m, pose=None):
    """What the layer would capture for a draw of `m` (test helper): per corner positions (posed) and UVs, and the
    index range. pose: f(positions) -> posed positions, the skinning + wind the game applied."""
    pos = m.positions if pose is None else pose(m.positions.astype(np.float64)).astype(np.float32)
    idx = np.asarray(m.tris, np.int64).ravel()
    return pos[idx], m.uv[idx].astype(np.float32), idx


def from_capture(corner_pos, corner_uv, indices=None):
    """mesh.Mesh from a capture. indices given: the native vertex numbering (cards, index id exact). None: weld
    corners with bit-identical position and UV."""
    corner_pos = np.ascontiguousarray(corner_pos, np.float32)
    corner_uv = np.ascontiguousarray(corner_uv, np.float32)
    n = len(corner_pos)
    if n % 3:
        raise ValueError("not a triangle list")
    if indices is None:
        key = np.concatenate([corner_pos.view(np.uint32), corner_uv.view(np.uint32)], 1)
        _, first, inverse = np.unique(key, axis=0, return_index=True, return_inverse=True)
        indices = inverse.ravel()
        vcount = len(first)
        src = first
    else:
        indices = np.asarray(indices, np.int64)
        vcount = int(indices.max()) + 1
        src = np.full(vcount, -1, np.int64)
        src[indices[::-1]] = np.arange(n)[::-1]   # first corner of each vertex
        if (src < 0).any():                         # vertices no triangle uses: never read by the converter
            src[src < 0] = 0
    positions = corner_pos[src]
    uv = corner_uv[src]
    return meshmod.Mesh(positions=positions, uv=uv, joints=np.zeros((vcount, 4), np.int64),
                        weights=np.zeros((vcount, 4), np.float32), tris=np.asarray(indices, np.int64).reshape(-1, 3),
                        joint_names=[], joint_positions=np.zeros((0, 3), np.float32))


def head_from_roots(points, root_param, offsets):
    """Head proxy without a skeleton: least-squares sphere through the strand roots (root channel ~0 lie on the
    scalp). Returns (centre, radii) as batch.head_proxy; radii 0 when too few roots."""
    roots = points[np.asarray(offsets[:-1], np.int64)].astype(np.float64)
    roots = roots[np.asarray(root_param)[np.asarray(offsets[:-1], np.int64)] < 0.2] if len(roots) else roots
    if len(roots) < 16:
        return np.array([0.0, 1e4, 0.0]), np.array([1e-3, 1e-3, 1e-3])
    a = np.c_[2 * roots, np.ones(len(roots))]
    b = (roots ** 2).sum(1)
    sol, *_ = np.linalg.lstsq(a, b, rcond=None)
    centre = sol[:3]
    r = float(np.sqrt(max(sol[3] + centre @ centre, 1e-6)))
    return centre, np.array([r, r, r])


def rigid_pose(seed=1):
    rng = np.random.default_rng(seed)
    q, _ = np.linalg.qr(rng.normal(size=(3, 3)))
    if np.linalg.det(q) < 0:
        q[:, 0] *= -1
    t = rng.normal(size=3)
    return lambda p: p @ q.T + t


def bend_pose(seed=2, amount=0.02):
    """Rigid + a smooth bend (stand-in for skinning and wind): displacement up to `amount` x the mesh size."""
    rigid = rigid_pose(seed)

    def f(p):
        c = p.mean(0)
        s = max(np.ptp(p, 0).max(), 1e-6)
        d = (p - c) / s
        bent = p + amount * s * np.stack([np.sin(3 * d[:, 1]), np.sin(2 * d[:, 2]) * 0.5, np.sin(2.5 * d[:, 0])], 1)
        return rigid(bent)
    return f


def synthetic_mask(rep, grid, size=(2048, 2048), seed=0):
    """A stand-in ID_Depth_Root_Alpha mask (no game texture is on this PC): per tile of `grid` the style's own strand
    count and typical width from the batch report, strands running down the rows, root channel 0 at the top."""
    H, W = size
    gx, gy = int(grid[0]), int(grid[1])
    tw, th = W // gx, H // gy
    m = np.zeros((H, W, 4), np.uint8)
    rng = np.random.default_rng(seed)
    tiles = rep.get("tiles", {})
    for tx in range(gx):
        for ty in range(gy):
            info = tiles.get(f"{tx},{ty}", {"strands": 0, "typical_px": 3.0})
            n, w = int(info.get("strands", 0)), max(1, int(round(info.get("typical_px", 3.0))))
            if not n:
                continue
            per_col = max(1, int(np.ceil(n * 2 * w / max(tw, 1))))   # strands stacked down a column when they do not fit
            cols = int(np.ceil(n / per_col))
            seg = th // per_col
            for k in range(n):
                col, row = k % cols, k // cols
                x0 = tx * tw + (col + 0.5) * tw / cols
                y_a, y_b = ty * th + row * seg + 2, ty * th + (row + 1) * seg - 2
                ys = np.arange(y_a, y_b)
                xs = x0 + 1.5 * np.sin((ys - y_a) / 40.0 + rng.uniform(0, 6))
                for dx in range(w):
                    xi = np.clip((xs - w / 2 + dx).astype(int), 0, W - 1)
                    m[ys, xi, 3] = 255
                    m[ys, xi, 2] = ((ys - y_a) * 255 // max(1, y_b - y_a)).astype(np.uint8)
                    m[ys, xi, 0] = (k * 37) % 255
    m[..., 1] = 128
    return m


def load(path):
    from . import batch
    got = batch.load_groom_cache(Path(path))
    if got is None:
        return None
    with np.load(path, allow_pickle=False) as z:
        rep = json.loads(str(z["report"].item()))
    return got, rep


def equivalence(cache_dir, limit=3, log=print):
    """GR2 mesh vs a capture of it (rigid pose and a bent pose, with and without indices): same groom embedding,
    same index id and UV samples. The mask is the same image on both sides (synthetic: no game texture here)."""
    from . import asset, convert
    files = sorted(glob.glob(str(Path(cache_dir) / "*.npz")), key=os.path.getsize)
    pick = [files[int(i)] for i in np.linspace(0, len(files) - 1, limit)] if len(files) > limit else files
    out = []
    for p in pick:
        got = load(p)
        if got is None:
            continue
        (groom0, hair, rep, *_rest), rep_raw = got[0], got[1]
        grid = tuple(int(x) for x in got[0][4])
        mask = synthetic_mask(rep_raw, grid, size=(1024, 1024))
        g_ref, _ = convert.convert(hair, mask, tile_grid=grid, tracer="rows")
        g_ref, _ = convert.dedupe(g_ref)
        row = {"file": os.path.basename(p)[:16], "triangles": int(len(hair.tris)), "points": int(len(g_ref.points))}
        for name, pose, with_idx in (("rigid+indices", rigid_pose(), True), ("bent+indices", bend_pose(), True),
                                     ("rigid+weld", rigid_pose(), False)):
            cp, cu, idx = soup_from_mesh(hair, pose)
            cap = from_capture(cp, cu, idx if with_idx else None)
            g, _ = convert.convert(cap, mask, tile_grid=grid, tracer="rows")
            g, _ = convert.dedupe(g)
            same = len(g.points) == len(g_ref.points)
            res = {"strands_equal": bool(same and np.array_equal(g.offsets, g_ref.offsets)),
                   "triangle_equal": bool(same and np.array_equal(g.triangle, g_ref.triangle)),
                   "bary_max_diff": float(np.abs(g.bary - g_ref.bary).max()) if same else None,
                   "radius_rel_max": float(np.abs(g.radii / g_ref.radii - 1).max()) if same else None,
                   "index_id_equal": asset.index_id(cap.tris) == asset.index_id(hair.tris),
                   "uv_samples_equal": bool(np.array_equal(asset.uv_samples(cap), asset.uv_samples(hair))),
                   "points": int(len(g.points)), "vertices": int(len(cap.positions)), "vertices_gr2": int(len(hair.positions))}
            row[name] = res
        out.append(row)
        log(json.dumps(row), flush=True)
    return out


def stages(cache_dir, limit=12, state_path=None, log=print):
    """Seconds per converter stage on cached meshes (GR2 decode excluded: it needs the paks) with a synthetic mask
    of the style's own strand counts: grid detection, tracing (convert), dedupe, head/orientation, validation renders,
    LODs + v3 write. Compared with the batch's recorded seconds per style (which include the pak read and GR2 decode)."""
    import tempfile
    from . import analysis, asset, batch, convert, render
    files = sorted(glob.glob(str(Path(cache_dir) / "*.npz")), key=os.path.getsize)
    pick = [files[int(i)] for i in np.linspace(0, len(files) - 1, limit)] if len(files) > limit else files
    state = json.loads(Path(state_path).read_text(encoding="utf-8")) if state_path else {}
    by_asset = {r.get("asset"): r for r in state.values() if r.get("asset")}
    rows = []
    for p in pick:
        got = load(p)
        if got is None:
            continue
        (_, hair, _rep, _d, grid0, *_r), rep_raw = got[0], got[1]
        mask = synthetic_mask(rep_raw, tuple(int(x) for x in grid0))
        t = {}
        s = time.perf_counter(); grid, _fit = convert.detect_tile_grid(hair); t["grid"] = time.perf_counter() - s
        s = time.perf_counter(); analysis.root_channel_check(mask, grid); t["root_check"] = time.perf_counter() - s
        s = time.perf_counter(); g, _ = convert.convert(hair, mask, tile_grid=grid, tracer="rows"); t["trace"] = time.perf_counter() - s
        s = time.perf_counter(); g, _ = convert.dedupe(g); t["dedupe"] = time.perf_counter() - s
        s = time.perf_counter(); head = head_from_roots(g.points, g.root_param, g.offsets); analysis.orientation(g, head[0]); t["orientation"] = time.perf_counter() - s
        s = time.perf_counter()
        for _name, (view, light_dir) in batch.bbox_views(hair.positions, 0.002).items():
            fibre_cards, plain_cards = render.card_fragment_builders(hair.positions, hair.uv, hair.tris, mask)
            render.render(fibre_cards, view, light_dir, head)
            render.render(plain_cards, view, light_dir, head)
            render.render(lambda v: render.strand_fragments(g.points, g.radii, g.offsets, g.root_param, v), view, light_dir, head)
        t["validation"] = time.perf_counter() - s
        with tempfile.TemporaryDirectory() as d:
            s = time.perf_counter()
            cid, _ = asset.content_id(hair.positions, hair.tris)
            asset.write(Path(d) / "x.hgrm", cid, hair, [(k, convert.thin(g, k)) for k in asset.LODS])
            t["lods_write"] = time.perf_counter() - s
        iid = f"{asset.index_id(hair.tris):016x}.hgrm"
        row = {"file": iid, "points": int(len(g.points)), "triangles": int(len(hair.tris)),
               "batch_seconds": by_asset.get(iid, {}).get("seconds"), **{k: round(v, 2) for k, v in t.items()}}
        row["total"] = round(sum(t.values()), 2)
        rows.append(row)
        log(json.dumps(row), flush=True)
    return rows


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("cmd", choices=("equivalence", "stages"))
    ap.add_argument("--cache", required=True)
    ap.add_argument("--limit", type=int, default=3)
    ap.add_argument("--state")
    ap.add_argument("--out")
    a = ap.parse_args(argv)
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    from tools.low_priority import lower_priority
    lower_priority()
    res = equivalence(a.cache, a.limit) if a.cmd == "equivalence" else stages(a.cache, a.limit, a.state)
    if a.out:
        Path(a.out).write_text(json.dumps(res, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
