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
from scipy.sparse import coo_matrix
from scipy.sparse.csgraph import connected_components

from . import strands2d

EPS = 1e-4


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

    @property
    def strands(self):
        return len(self.offsets) - 1

    @property
    def segments(self):
        return len(self.points) - self.strands


def cards(tris, vertex_count):
    """Card index per triangle: connected components over shared vertices."""
    rows = np.r_[tris[:, 0], tris[:, 1], tris[:, 2]]
    cols = np.r_[tris[:, 1], tris[:, 2], tris[:, 0]]
    graph = coo_matrix((np.ones(len(rows)), (rows, cols)), shape=(vertex_count, vertex_count))
    count, label = connected_components(graph, directed=False)
    return count, label[tris[:, 0]]


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
    inside_count = np.zeros(len(points), int)
    order = np.argsort(points[:, 1])
    sorted_v = points[order, 1]
    for t, tri in enumerate(tris):
        uvt = uv[tri]
        lo, hi = np.searchsorted(sorted_v, [uvt[:, 1].min() - EPS, uvt[:, 1].max() + EPS])
        if lo == hi:
            continue
        idx = order[lo:hi]
        b = barycentric(uvt, points[idx])
        inside = np.all(b >= -EPS, 1)
        inside_count[idx] += np.all(b > EPS, 1)
        sel = inside & (hit[idx] < 0)
        hit[idx[sel]] = t
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
    """{(tx, ty): [(u, v, width_px, slope, root)]} sampled strands of each mask tile, atlas UV."""
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
                             tile[yi, xi, 2] / 255.0))
            out[(tx, ty)] = rows
    return out, stats


def convert(mesh, mask, tile_grid=(8, 2), step=16, threshold=128):
    """Groom + report of one hair mesh with its ID_Depth_Root_Alpha mask (H, W, 4 uint8)."""
    count, card_of = cards(mesh.tris, len(mesh.positions))
    per_tile, tile_stats = tile_strands(mask, tile_grid, step, threshold)
    pts, rad, jnt, wgt, root, offsets, card_ids, emb_t, emb_b = [], [], [], [], [], [0], [], [], []
    report = {"cards": int(count), "triangles": int(len(mesh.tris)), "tiles": {f"{k[0]},{k[1]}": v for k, v in tile_stats.items()},
              "cards_spanning_tiles": 0, "uv_overlap_samples": 0, "samples": 0, "unmapped_samples": 0,
              "mixed_winding_cards": 0}
    for c in range(count):
        tri_ids = np.flatnonzero(card_of == c)
        tris = mesh.tris[tri_ids]
        uv = mesh.uv
        lo, hi = uv[tris].reshape(-1, 2).min(0), uv[tris].reshape(-1, 2).max(0)
        tile = tuple(np.floor((lo + hi) / 2 * tile_grid).astype(int))
        if np.any(np.floor(lo * tile_grid + EPS) != np.floor(hi * tile_grid - EPS)):
            report["cards_spanning_tiles"] += 1
        e0 = uv[tris[:, 1]] - uv[tris[:, 0]]
        e1 = uv[tris[:, 2]] - uv[tris[:, 0]]
        sign = np.sign(e0[:, 0] * e1[:, 1] - e0[:, 1] * e1[:, 0])
        if (sign > 0).any() and (sign < 0).any():
            report["mixed_winding_cards"] += 1
        rows = per_tile.get(tile, [])
        if not rows:
            continue
        u, v, wd, slope, rp = (np.concatenate([r[k] for r in rows]) for k in range(5))
        sid = np.repeat(np.arange(len(rows)), [len(r[0]) for r in rows])
        hit, bary, inside = locate(uv, tris, np.stack([u, v], 1))
        report["samples"] += len(u)
        report["unmapped_samples"] += int((hit < 0).sum())
        report["uv_overlap_samples"] += int((inside > 1).sum())
        tv = tris[np.maximum(hit, 0)]
        p = np.einsum("nk,nkd->nd", bary, mesh.positions[tv])
        # World width: the texture-space cross-section (width * cos(slope), along u)
        # pushed through the triangle's UV -> position Jacobian.
        width = wd / np.sqrt(1 + slope ** 2) * np.linalg.norm(jacobian_u(mesh, tv), axis=1)
        j, w = blend_weights(mesh.joints, mesh.weights, tv, bary)
        # Runs of consecutive mapped samples of one painted strand become groom strands.
        ok = (hit >= 0).astype(np.int8)
        brk = np.r_[True, sid[1:] != sid[:-1]]
        starts = np.flatnonzero(ok.astype(bool) & (brk | np.r_[True, ok[:-1] == 0]))
        for a0 in starts:
            b0 = a0 + 1
            while b0 < len(u) and ok[b0] and not brk[b0]:
                b0 += 1
            if b0 - a0 < 2:
                continue
            pts.append(p[a0:b0]); rad.append(width[a0:b0] / 2); jnt.append(j[a0:b0]); wgt.append(w[a0:b0])
            root.append(rp[a0:b0]); offsets.append(offsets[-1] + b0 - a0); card_ids.append(c)
            emb_t.append(tri_ids[hit[a0:b0]]); emb_b.append(bary[a0:b0])
    groom = Groom(np.concatenate(pts).astype(np.float32), np.concatenate(rad).astype(np.float32),
                  np.concatenate(jnt), np.concatenate(wgt), np.array(offsets), np.array(card_ids),
                  np.concatenate(root).astype(np.float32), np.concatenate(emb_t),
                  np.concatenate(emb_b).astype(np.float32))
    return groom, report


def jacobian_u(mesh, tv):
    """dP/du of each triangle (rows of tv): world displacement per unit of UV u."""
    p, t = mesh.positions[tv].astype(np.float64), mesh.uv[tv].astype(np.float64)
    dp1, dp2 = p[:, 1] - p[:, 0], p[:, 2] - p[:, 0]
    du1, du2 = t[:, 1] - t[:, 0], t[:, 2] - t[:, 0]
    det = du1[:, 0] * du2[:, 1] - du1[:, 1] * du2[:, 0]
    det = np.where(np.abs(det) < 1e-12, np.nan, det)
    return (dp1 * du2[:, 1:2] - dp2 * du1[:, 1:2]) / det[:, None]
