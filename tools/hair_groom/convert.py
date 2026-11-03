"""Approximate BG3 hair cards -> strand groom (linear swept sphere input: points + radii).

A card is a connected triangle strip whose UV0 covers one tile of the coverage mask; texture
rows run root to tip (checked by the mask's root channel and by the head distance). The strands
painted in the tile (strands2d.trace) are placed on every card that uses the tile: each sample
(u, v) is located in the card's UV triangles, and its barycentrics give the 3-D point, the bone
weights and the world-space width. A strand is cut where a sample falls outside the card's UV.
"""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from . import geom, strands2d

EPS = 1e-4
# Largest strand radius kept (metres): the widest painted clump of the reference LOD0 conversion is 1.14 mm;
# wider samples come from near-degenerate UV triangles of LOD1 cards.
MAX_RADIUS = 0.002
# The layer's parser rejects radii >= 10 mm (common/hair_groom_asset.h): thinned LODs stay below.
MAX_THIN_RADIUS = 0.009


@dataclass
class Groom:
    points: np.ndarray    # (P, 3) float32 strand vertices, strands stored back to back
    radii: np.ndarray     # (P,) float32
    joints: np.ndarray    # (P, 4) int
    weights: np.ndarray   # (P, 4) float32
    offsets: np.ndarray   # (S + 1,) start of each strand in points
    card: np.ndarray      # (S,) card index of each strand
    root_param: np.ndarray  # (P,) mask root channel 0..1 (0 = root)
    triangle: np.ndarray  # (P,) card triangle each point is embedded in
    bary: np.ndarray      # (P, 3) its barycentrics: points = bary . deformed triangle vertices
    normal_offset: np.ndarray  # (P,) metres along the triangle normal (0 unless depth_offset is used)

    @property
    def strands(self):
        return len(self.offsets) - 1

    @property
    def segments(self):
        return len(self.points) - self.strands


def cards(tris, vertex_count):
    """Card index per triangle: connected components over shared vertices."""
    count, label = geom.connected_components(tris, vertex_count)
    return count, label[tris[:, 0]]


def detect_tile_grid(mesh, max_tiles=16):
    """Atlas tile grid (columns, rows) of a card mesh: the median UV extent of its cards. BG3 hair atlases
    tile one card per cell (Shadowheart: 8 x 2). Returns (grid, share of cards that fit one cell)."""
    count, card_of = cards(mesh.tris, len(mesh.positions))
    lo = np.full((count, 2), np.inf)
    hi = np.full((count, 2), -np.inf)
    uv = mesh.uv[mesh.tris].reshape(-1, 2)
    card = np.repeat(card_of, 3)
    np.minimum.at(lo, card, uv)
    np.maximum.at(hi, card, uv)
    span = np.median(hi - lo, axis=0)
    grid = tuple(int(np.clip(np.rint(1.0 / max(s, 1e-6)), 1, max_tiles)) for s in span)
    g = np.array(grid, float)
    fits = np.all(np.floor(lo * g + EPS) == np.floor(hi * g - EPS), axis=1)
    return grid, float(fits.mean())


def barycentric(uv_tri, points):
    """Barycentrics of points (N, 2) in one UV triangle (3, 2); rows of NaN if degenerate."""
    a, b, c = uv_tri
    e0, e1 = b - a, c - a
    det = e0[0] * e1[1] - e0[1] * e1[0]
    if abs(det) < 1e-12:
        return np.full((len(points), 3), np.nan)
    d = points - a
    l1 = (d[:, 0] * e1[1] - d[:, 1] * e1[0]) / det
    l2 = (e0[0] * d[:, 1] - e0[1] * d[:, 0]) / det
    return np.stack([1 - l1 - l2, l1, l2], 1)


def locate(uv, tris, points):
    """For each point: the first triangle containing it (or -1), its barycentrics, and how many
    triangles contain it strictly inside (> 1 means overlapping UVs within the card)."""
    hit = np.full(len(points), -1)
    bary = np.zeros((len(points), 3))
    order = np.argsort(points[:, 1])
    sorted_v = points[order, 1]
    uvt = uv[tris]
    v = uvt[:, :, 1]
    # Match scalar EPS arithmetic on both NumPy 1 (promotion) and 2 (weak scalar typing).
    dt = np.result_type(v.dtype.type(0) - EPS)
    bounds = np.searchsorted(sorted_v, np.stack([v.min(1).astype(dt) - EPS, v.max(1).astype(dt) + EPS], 1))
    n = bounds[:, 1] - bounds[:, 0]
    t = np.repeat(np.arange(len(tris)), n)
    idx = order[np.repeat(bounds[:, 0] - np.r_[0, np.cumsum(n)[:-1]], n) + np.arange(n.sum())]
    # Batch the same barycentric arithmetic over all candidate triangle/sample pairs. Reduce the
    # triangle indices to keep the first containing triangle, including overlapping card UVs.
    e0, e1 = uvt[:, 1] - uvt[:, 0], uvt[:, 2] - uvt[:, 0]
    det = e0[:, 0] * e1[:, 1] - e0[:, 1] * e1[:, 0]
    det = np.where(np.abs(det).astype(dt) < 1e-12, np.nan, det)
    d = points[idx] - uvt[t, 0]
    l1 = (d[:, 0] * e1[t, 1] - d[:, 1] * e1[t, 0]) / det[t]
    l2 = (e0[t, 0] * d[:, 1] - e0[t, 1] * d[:, 0]) / det[t]
    b = np.stack([1 - l1 - l2, l1, l2], 1)
    inside_count = np.bincount(idx[np.all(b > EPS, 1)], minlength=len(points))
    inside = np.all(b >= -EPS, 1)
    hit[:] = len(tris)
    np.minimum.at(hit, idx[inside], t[inside])
    hit[hit == len(tris)] = -1
    sel = inside & (hit[idx] == t)
    bary[idx[sel]] = b[sel]
    return hit, bary, inside_count


def blend_weights(joints, weights, tri_vertices, bary):
    """Barycentric mix of three vertices' 4-bone skins; keep the 4 largest, renormalised."""
    n = len(bary)
    j = joints[tri_vertices].reshape(n, 12)
    w = (weights[tri_vertices] * bary[:, :, None]).reshape(n, 12)
    dense = np.zeros((n, int(joints.max()) + 1))
    np.add.at(dense, (np.repeat(np.arange(n), 12), j.ravel()), w.ravel())
    top = np.argsort(-dense, axis=1)[:, :4]
    top_w = np.take_along_axis(dense, top, 1)
    top_w /= np.maximum(top_w.sum(1, keepdims=True), 1e-12)
    return top, top_w.astype(np.float32)


def tile_strands(mask, tile_grid, step, threshold=128):
    """{(tx, ty): [(u, v, width, slope, root, depth)]} sampled strands of each mask tile: atlas UV,
    width in UV u units, slope in pixels per row, root and depth channels 0..1."""
    h, w = mask.shape[:2]
    tw, th = w // tile_grid[0], h // tile_grid[1]
    out, stats = {}, {}
    for ty in range(tile_grid[1]):
        for tx in range(tile_grid[0]):
            tile = mask[ty * th:(ty + 1) * th, tx * tw:(tx + 1) * tw]
            traced, typical = strands2d.trace(tile[..., 3], threshold)
            redrawn = strands2d.redraw(traced, tile.shape[:2])
            covered = tile[..., 3] >= threshold
            stats[(tx, ty)] = {"strands": len(traced), "typical_px": typical,
                               "recall": float((redrawn & covered).sum() / max(covered.sum(), 1))}
            rows = []
            for s in traced:
                x, y, wd = strands2d.resample(s, step)
                slope = np.gradient(x, y) if len(x) > 1 else np.zeros_like(x)
                xi, yi = np.clip(np.rint(x).astype(int), 0, tw - 1), y.astype(int)
                rows.append(((tx * tw + x + 0.5) / w, (ty * th + y + 0.5) / h, wd / w, slope,
                             tile[yi, xi, 2] / 255.0, tile[yi, xi, 1] / 255.0))
            out[(tx, ty)] = rows
    return out, stats


def run_radii(hit, sid, width):
    """Mapped runs with at least two samples: point indices, lengths, radii and widthless count."""
    ok = hit >= 0
    brk = np.r_[True, (sid[1:] != sid[:-1]) | (ok[1:] != ok[:-1]), True]
    bounds = np.flatnonzero(brk)
    starts, lengths = bounds[:-1], np.diff(bounds)
    keep = ok[starts] & (lengths >= 2)
    starts, lengths = starts[keep], lengths[keep]
    run = np.repeat(np.arange(len(starts)), lengths)
    idx = np.repeat(starts - np.r_[0, np.cumsum(lengths)[:-1]], lengths) + np.arange(lengths.sum())
    r = width[idx] / 2
    # Collapsed world triangles have no measured width: borrow the run's median, or drop a
    # run with none (the layer refuses r <= 0). Near-degenerate UVs instead need a 3x median cap.
    measured = np.isfinite(r) & (r > 0)
    counts = np.bincount(run[measured], minlength=len(starts))
    keep = counts > 0
    # Sort measured radii by run then value once. Invalid samples take that median and cannot
    # move the run's median, so the 3x cap needs no second sort.
    order = np.lexsort((r[measured], run[measured]))
    sr = r[measured][order]
    end = np.cumsum(counts)
    median = np.zeros(len(starts), r.dtype)
    mid = end[keep] - counts[keep] // 2 - 1
    hi = end[keep] - (counts[keep] + 1) // 2
    values = sr[hi].copy()
    even = counts[keep] % 2 == 0
    values[even] = (sr[mid[even]] + values[even]) / 2
    median[keep] = values
    r = np.where(measured, r, median[run])
    r = np.minimum(np.minimum(r, 3 * median[run]), MAX_RADIUS)
    point_keep = keep[run]
    return idx[point_keep], lengths[keep], r[point_keep], int((~keep).sum())


def convert(mesh, mask, tile_grid=(8, 2), step=16, threshold=128, max_fibre_diameter=None,
            depth_offset=None, head_centre=None):
    """Groom + report of one hair mesh with its ID_Depth_Root_Alpha mask (H, W, 4 uint8).

    max_fibre_diameter: a painted strand wider than this (metres, at its widest sample on the card)
      becomes k = ceil(width / max) parallel fibres of width / k side by side across the painted
      width: coverage is kept and no fibre is thicker than a real hair.
    depth_offset: metres per unit of the mask's depth channel (the material's PixelDepthOffset);
      each point moves that far along the card normal toward head_centre. Off when None.
    """
    count, card_of = cards(mesh.tris, len(mesh.positions))
    per_tile, tile_stats = tile_strands(mask, tile_grid, step, threshold)
    H, W = mask.shape[:2]
    pts, rad, jnt, wgt, root, offsets, card_ids, emb_t, emb_b, emb_n = [], [], [], [], [], [], [], [], [], []
    report = {"cards": int(count), "triangles": int(len(mesh.tris)), "tiles": {f"{k[0]},{k[1]}": v for k, v in tile_stats.items()},
              "cards_spanning_tiles": 0, "uv_overlap_samples": 0, "samples": 0, "unmapped_samples": 0,
              "mixed_winding_cards": 0, "split_strands": 0, "fibres_from_splits": 0}
    for c in range(count):
        tri_ids = np.flatnonzero(card_of == c)
        tris = mesh.tris[tri_ids]
        uv = mesh.uv
        lo, hi = uv[tris].reshape(-1, 2).min(0), uv[tris].reshape(-1, 2).max(0)
        # Every tile the card's UV box touches (most styles: exactly one); samples outside the card are dropped.
        t0 = np.clip(np.floor(lo * tile_grid + EPS).astype(int), 0, np.array(tile_grid) - 1)
        t1 = np.clip(np.floor(hi * tile_grid - EPS).astype(int), 0, np.array(tile_grid) - 1)
        if np.any(t0 != t1):
            report["cards_spanning_tiles"] += 1
        e0 = uv[tris[:, 1]] - uv[tris[:, 0]]
        e1 = uv[tris[:, 2]] - uv[tris[:, 0]]
        sign = np.sign(e0[:, 0] * e1[:, 1] - e0[:, 1] * e1[:, 0])
        if (sign > 0).any() and (sign < 0).any():
            report["mixed_winding_cards"] += 1
        rows = [r for tx in range(t0[0], t1[0] + 1) for ty in range(t0[1], t1[1] + 1) for r in per_tile.get((tx, ty), [])]
        if not rows:
            continue
        u, v, wd, slope, rp, dp = (np.concatenate([r[k] for r in rows]) for k in range(6))
        sid = np.repeat(np.arange(len(rows)), [len(r[0]) for r in rows])
        hit, bary, inside = locate(uv, tris, np.stack([u, v], 1))
        report["samples"] += len(u)
        report["unmapped_samples"] += int((hit < 0).sum())
        report["uv_overlap_samples"] += int((inside > 1).sum())
        # World width: the texture-space cross-section (width * cos(slope), along u)
        # pushed through the triangle's UV -> position Jacobian.
        cos = 1 / np.sqrt(1 + slope ** 2)
        # The Jacobian only depends on the triangle, not the sample placed on it.
        scale = np.linalg.norm(jacobian_u(mesh, tris), axis=1)
        width = wd * cos * scale[np.maximum(hit, 0)]
        if max_fibre_diameter:
            widest = np.zeros(len(rows))
            np.maximum.at(widest, sid, np.where(hit >= 0, width, 0))
            per_strand = np.maximum(1, np.ceil(widest / max_fibre_diameter - 1e-9)).astype(int)
            report["split_strands"] += int((per_strand > 1).sum())
            report["fibres_from_splits"] += int(per_strand[per_strand > 1].sum())
            k = per_strand[sid]
            # Fibre i of k sits (i - (k-1)/2) * width/k across the painted strand, perpendicular
            # to it in texture pixels; the copies are located again, so the embedding stays exact.
            rep = np.repeat(np.arange(len(u)), k)
            i = np.arange(len(rep)) - np.repeat(np.cumsum(k) - k, k)
            across = (i - (k[rep] - 1) / 2) * wd[rep] * W * cos[rep] / k[rep]
            fibre = sid[rep] * int(per_strand.max()) + i
            order = np.lexsort((rep, fibre))
            rep, across, sid = rep[order], across[order], fibre[order]
            u = u[rep] + across * cos[rep] / W
            v = v[rep] - across * slope[rep] * cos[rep] / H
            width = (width / k)[rep]
            rp, dp = rp[rep], dp[rep]
            hit, bary, _ = locate(uv, tris, np.stack([u, v], 1))
        tv = tris[np.maximum(hit, 0)]
        p = np.einsum("nk,nkd->nd", bary, mesh.positions[tv])
        normal_offset = np.zeros(len(u))
        if depth_offset and head_centre is not None:
            pos = mesh.positions[tv].astype(np.float64)
            n = np.cross(pos[:, 1] - pos[:, 0], pos[:, 2] - pos[:, 0])
            n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)
            inward = np.sign(np.einsum("nd,nd->n", n, head_centre - p))
            n *= np.where(inward == 0, 1, inward)[:, None]
            normal_offset = dp * depth_offset
            p = p + n * normal_offset[:, None]
        j, w = blend_weights(mesh.joints, mesh.weights, tv, bary)
        # Runs of consecutive mapped samples of one painted strand (or fibre) become groom strands.
        idx, lengths, r, dropped = run_radii(hit, sid, width)
        if dropped:
            report["strands_without_width"] = report.get("strands_without_width", 0) + dropped
        if not len(lengths):
            continue
        pts.append(p[idx]); rad.append(r); jnt.append(j[idx]); wgt.append(w[idx])
        root.append(rp[idx]); offsets.append(lengths); card_ids.append(np.full(len(lengths), c, int))
        emb_t.append(tri_ids[hit[idx]]); emb_b.append(bary[idx]); emb_n.append(normal_offset[idx])
    groom = Groom(np.concatenate(pts).astype(np.float32), np.concatenate(rad).astype(np.float32),
                  np.concatenate(jnt), np.concatenate(wgt), np.r_[0, np.cumsum(np.concatenate(offsets))], np.concatenate(card_ids),
                  np.concatenate(root).astype(np.float32), np.concatenate(emb_t),
                  np.concatenate(emb_b).astype(np.float32), np.concatenate(emb_n).astype(np.float32))
    return groom, report


def select(groom, keep):
    """The groom restricted to the strands where keep (S,) is True."""
    lengths = np.diff(groom.offsets)
    point_keep = np.repeat(keep, lengths)
    return Groom(groom.points[point_keep], groom.radii[point_keep], groom.joints[point_keep],
                 groom.weights[point_keep], np.r_[0, np.cumsum(lengths[keep])], groom.card[keep],
                 groom.root_param[point_keep], groom.triangle[point_keep], groom.bary[point_keep],
                 groom.normal_offset[point_keep])


def dedupe(groom, distance=0.0005, share=0.8):
    """Drop strands that repeat a strand of another (stacked) card: more than `share` of their points
    lie within `distance` of one single other-card strand that is kept. Stacked cards tiled with the
    same painted strands put the same fibre twice in one place; the lower strand index is kept.
    Returns (groom, dropped strand count)."""
    strand_of = np.repeat(np.arange(groom.strands), np.diff(groom.offsets))
    pairs = geom.pairs_within(groom.points, distance)
    # Same-card neighbours never contribute; discard them before making both directions.
    other_card = groom.card[strand_of[pairs[:, 0]]] != groom.card[strand_of[pairs[:, 1]]]
    pairs = pairs[other_card]
    pairs = np.concatenate([pairs, pairs[:, ::-1]])
    b = strand_of[pairs[:, 1]]
    # Each point counts once per neighbouring strand.
    n = groom.strands
    key = np.sort(pairs[:, 0] * np.int64(n) + b)
    # These keys are mostly unique; sorting avoids NumPy's costly large hash table here.
    key = key[np.r_[True, key[1:] != key[:-1]]] if len(key) else key
    pair_key, counts = np.unique(strand_of[key // n] * np.int64(n) + key % n, return_counts=True)
    dup = counts > share * np.diff(groom.offsets)[pair_key // n]
    keep = np.ones(groom.strands, bool)
    for key in pair_key[dup]:
        s, other = divmod(int(key), n)
        if keep[s] and keep[other] and other < s:
            keep[s] = False
    return select(groom, keep), int((~keep).sum())


def jacobian_u(mesh, tv):
    """dP/du of each triangle (rows of tv): world displacement per unit of UV u."""
    p, t = mesh.positions[tv].astype(np.float64), mesh.uv[tv].astype(np.float64)
    dp1, dp2 = p[:, 1] - p[:, 0], p[:, 2] - p[:, 0]
    du1, du2 = t[:, 1] - t[:, 0], t[:, 2] - t[:, 0]
    det = du1[:, 0] * du2[:, 1] - du1[:, 1] * du2[:, 0]
    det = np.where(np.abs(det) < 1e-12, np.nan, det)
    return (dp1 * du2[:, 1:2] - dp2 * du1[:, 1:2]) / det[:, None]


def thin(groom, keep_fraction, seed=0x9E3779B9):
    """Distance LOD: keep a deterministic `keep_fraction` of the strands and widen their radii by
    1 / keep_fraction, so the projected area (2 r x length, summed) and hence the expected
    coverage of a pixel much wider than a strand stay the same. Returns the thinned groom."""
    if keep_fraction >= 1:
        return groom
    ids = np.arange(groom.strands, dtype=np.uint64)
    h = (ids * np.uint64(seed) + np.uint64(0x632BE59BD9B4E019)) & np.uint64(0xFFFFFFFF)
    keep = (h.astype(np.float64) / 2.0 ** 32) < keep_fraction
    out = select(groom, keep)
    out.radii = np.minimum(out.radii / keep_fraction, MAX_THIN_RADIUS).astype(np.float32)
    return out
