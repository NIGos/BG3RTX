"""Numbers and consistency checks of a converted groom (docs/RTX_HAIR_GROOM_EXPERIMENT.md)."""
from __future__ import annotations

import numpy as np
from .geom import pairs_within

# Bytes per element of a GPU layout (assumptions stated in the doc).
LSS_POINT = 16          # float3 position + float radius
LSS_INDEX = 4           # uint32 per segment (list indexing)
SKIN_POINT = 8          # 4 x uint8 joints + 4 x unorm8 weights
CARD_VERTEX_CAPTURED = 88  # the layer's captured hair vertex stream (common/native_hair.h)


def root_channel_check(mask, tile_grid=(8, 2), threshold=128):
    """Correlation of mask row and root channel over covered texels of each tile (> 0: rows run root to tip)."""
    h, w = mask.shape[:2]
    th, tw = h // tile_grid[1], w // tile_grid[0]
    out = []
    for ty in range(tile_grid[1]):
        for tx in range(tile_grid[0]):
            tile = mask[ty * th:(ty + 1) * th, tx * tw:(tx + 1) * tw]
            yy, xx = np.nonzero(tile[..., 3] >= threshold)
            if len(yy) > 10:
                out.append(float(np.corrcoef(yy, tile[yy, xx, 2])[0, 1]))
    return out


def _orientation_flags(groom, head_centre, margin=0.005):
    first, last = groom.offsets[:-1], groom.offsets[1:] - 1
    d_root = np.linalg.norm(groom.points[first] - head_centre, axis=1)
    d_tip = np.linalg.norm(groom.points[last] - head_centre, axis=1)
    chord = np.linalg.norm(groom.points[last] - groom.points[first], axis=1)
    return (d_tip < d_root - margin,
            np.abs(d_tip - d_root) > np.maximum(margin, chord / np.sqrt(2.0)))


def reorient(groom, head_centre):
    """Copy a groom with radial strands pointing away from the head and arc-length root parameters.

    Tangential strands keep their order. A collapsed strand uses evenly spaced parameters.
    The converter's groom (including its cached arrays) is left untouched.
    """
    from dataclasses import replace

    reversed_, decided = _orientation_flags(groom, head_centre)
    flip = reversed_ & decided
    lengths = np.diff(groom.offsets)
    index = np.arange(len(groom.points))
    opposite = np.repeat(groom.offsets[:-1] + groom.offsets[1:] - 1, lengths) - index
    index = np.where(np.repeat(flip, lengths), opposite, index)
    arrays = {name: getattr(groom, name)[index].copy() for name in
              ("points", "radii", "joints", "weights", "root_param", "triangle", "bary", "normal_offset")}
    if groom.strands:
        step = np.r_[0.0, np.linalg.norm(np.diff(arrays["points"].astype(float), axis=0), axis=1)]
        step[groom.offsets[:-1]] = 0
        arc = np.cumsum(step)
        arc -= np.repeat(arc[groom.offsets[:-1]], lengths)
        total = np.repeat(arc[groom.offsets[1:] - 1], lengths)
        fallback = (np.arange(len(index)) - np.repeat(groom.offsets[:-1], lengths)) / np.repeat(lengths - 1, lengths)
        arrays["root_param"] = np.divide(arc, total, out=fallback, where=total > 0).astype(groom.root_param.dtype)
    return replace(groom, **arrays), int(flip.sum())


def orientation(groom, head_centre, margin=0.005):
    """Strands whose tip is closer to the head centre than the root (by > margin) are reversed. Only strands more
    radial than tangential decide (|d_tip - d_root| > chord cos 45): a strand lying along the scalp has root and tip at
    the same distance from the head whichever way it runs (Halsin 2026-11-03: 369 of 842 cards flagged by scalp strands
    with a radial share of 0.20, the groom's images within 1-3% of the cards)."""
    reversed_, decided = _orientation_flags(groom, head_centre, margin)
    per_card = {}
    for c, r, ok in zip(groom.card, reversed_, decided):
        if ok:
            per_card.setdefault(int(c), []).append(bool(r))
    cards_reversed = sum(np.mean(v) > 0.5 for v in per_card.values())
    return {"strands_reversed": int(reversed_.sum()), "strands_decided": int(decided.sum()),
            "cards_mostly_reversed": int(cards_reversed), "cards_decided": len(per_card)}


def layered_duplicates(groom, distance=0.0005):
    """Share of strand points with a point of another card closer than `distance`
    (layered cards that repeat or touch the same strands)."""
    strand_of = np.repeat(np.arange(groom.strands), np.diff(groom.offsets))
    card_of_point = groom.card[strand_of]
    pairs = pairs_within(groom.points, distance)
    other = pairs[card_of_point[pairs[:, 0]] != card_of_point[pairs[:, 1]]]
    touched = np.zeros(len(groom.points), bool)
    touched[other.ravel()] = True
    per_strand = np.bincount(strand_of, touched) / np.diff(groom.offsets)
    return {"points_near_other_card": float(touched.mean()),
            "strands_mostly_duplicated": int((per_strand > 0.8).sum())}


def summary(groom, hair, mask, head_centre):
    lengths = np.add.reduceat(np.r_[np.linalg.norm(np.diff(groom.points, axis=0), axis=1), 0],
                              groom.offsets[:-1])
    lengths -= np.r_[np.linalg.norm(np.diff(groom.points, axis=0), axis=1), 0][groom.offsets[1:] - 1]
    points, segments = len(groom.points), groom.segments
    root_corr = root_channel_check(mask)
    return {
        "strands": int(groom.strands), "segments": int(segments), "points": int(points),
        "card_vertices": int(len(hair.positions)), "card_triangles": int(len(hair.tris)),
        "strand_length_mm_p5_p50_p95": (np.percentile(lengths, [5, 50, 95]) * 1000).round(1).tolist(),
        "total_length_m": float(lengths.sum()),
        "radius_mm_p5_p50_p95": (np.percentile(groom.radii, [5, 50, 95]) * 1000).round(3).tolist(),
        "root_channel_row_corr_min_median": [min(root_corr), float(np.median(root_corr))],
        "orientation": orientation(groom, head_centre),
        "layered": layered_duplicates(groom),
        "memory_bytes": {
            "lss_positions_radii": points * LSS_POINT, "lss_indices": segments * LSS_INDEX,
            "skin": points * SKIN_POINT,
            "per_frame_refit_input": points * LSS_POINT,
            "cards_captured_vertices": len(hair.positions) * CARD_VERTEX_CAPTURED,
            "cards_indices": len(hair.tris) * 12},
    }
