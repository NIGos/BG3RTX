"""Trace the strands painted in a hair-card coverage mask, in texture space.

BG3 CHAR_Hair masks (ID_Depth_Root_Alpha_MSKA) paint individual strands running down the
texture rows: alpha is coverage, blue is the root (0) to tip (1) gradient, red a per-strand ID.
Each row is cut into covered runs; a run is a strand cross-section, a run wider than
1.5 typical widths is a clump and is cut into round(width / typical) strands. Runs are linked from
row to row by the nearest centre predicted from the recent slope (one to one, at most
`max_dx` pixels apart), so a strand follows the wave the artist painted. A strand ends after `max_gap` rows
without a match; pieces cut by crossings are then stitched back (stitch).
"""
from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np


@dataclass
class Strand2D:
    x: list = field(default_factory=list)  # pixel column of the centre (float)
    y: list = field(default_factory=list)  # pixel row
    w: list = field(default_factory=list)  # covered width in pixels (alpha >= threshold)
    gap: int = 0


def row_runs(covered):
    """(start, end) inclusive index pairs of the True runs of a 1-D bool array."""
    padded = np.concatenate(([False], covered, [False])).astype(np.int8)
    edges = np.flatnonzero(np.diff(padded))
    return edges[0::2], edges[1::2] - 1


def cross_sections(covered, typical):
    """Strand centres and widths in one row; wide runs are clumps of touching strands."""
    starts, ends = row_runs(covered)
    centres, widths = [], []
    for s, e in zip(starts, ends):
        width = e - s + 1
        count = max(1, int(round(width / typical))) if width > 1.5 * typical else 1
        part = width / count
        for k in range(count):
            centres.append(s - 0.5 + part * (k + 0.5))
            widths.append(part)
    return np.array(centres, float), np.array(widths, float)


def typical_width(alpha, threshold):
    """Median run width of the mask: the painted single-strand width."""
    widths = []
    for row in alpha[:: max(1, alpha.shape[0] // 64)]:
        s, e = row_runs(row >= threshold)
        widths.extend((e - s + 1).tolist())
    return float(np.median(widths)) if widths else 1.0


def predict(strand, history=4):
    """Expected centre in the next row: last centre plus the recent sideways slope."""
    if len(strand.x) < 2:
        return strand.x[-1]
    k = min(history, len(strand.x) - 1)
    slope = (strand.x[-1] - strand.x[-1 - k]) / max(strand.y[-1] - strand.y[-1 - k], 1)
    return strand.x[-1] + slope * (1 + strand.gap)


def trace(alpha, threshold=128, max_dx=2.5, max_gap=3, min_rows=12, typical=None):
    """Strand centrelines of one card tile (rows = root to tip). alpha: (H, W) uint8."""
    typical = typical or typical_width(alpha, threshold)
    active, done = [], []
    for y in range(alpha.shape[0]):
        cx, cw = cross_sections(alpha[y] >= threshold, typical)
        taken = np.zeros(len(cx), bool)
        if active and len(cx):
            last = np.array([predict(s) for s in active])
            dist = np.abs(last[:, None] - cx[None, :])
            order = np.argsort(dist, axis=None)
            used = np.zeros(len(active), bool)
            for flat in order:
                i, j = divmod(int(flat), len(cx))
                if dist[i, j] > max_dx:
                    break
                if used[i] or taken[j]:
                    continue
                used[i] = taken[j] = True
                s = active[i]
                s.x.append(cx[j]); s.y.append(y); s.w.append(cw[j]); s.gap = 0
        survivors = []
        for s in active:
            if s.y[-1] != y:
                s.gap += 1
                if s.gap > max_gap:
                    done.append(s)
                    continue
            survivors.append(s)
        active = survivors
        for j in np.flatnonzero(~taken):
            active.append(Strand2D([cx[j]], [y], [cw[j]]))
    done.extend(active)
    return [s for s in stitch(done) if len(s.y) >= min_rows], typical


def stitch(strands, max_gap=24, max_dx=3.0, history=8):
    """Join a strand that ends to one that starts shortly below it on its predicted path.

    Where two painted strands cross, their runs merge, one track survives and the other
    restarts below the crossing; this reconnects such pieces (nearest prediction first)."""
    order = sorted(range(len(strands)), key=lambda i: strands[i].y[0])
    starts = [(strands[i].y[0], strands[i].x[0], i) for i in order]
    start_y = np.array([s[0] for s in starts])
    used = set()
    nxt = {}
    ends = sorted(range(len(strands)), key=lambda i: strands[i].y[-1])
    for i in ends:
        s = strands[i]
        k = min(history, len(s.x) - 1)
        slope = (s.x[-1] - s.x[-1 - k]) / max(s.y[-1] - s.y[-1 - k], 1) if k else 0.0
        lo, hi = np.searchsorted(start_y, [s.y[-1] + 1, s.y[-1] + max_gap + 1])
        best, best_d = None, max_dx
        for y0, x0, j in starts[lo:hi]:
            if j == i or j in used:
                continue
            d = abs(x0 - (s.x[-1] + slope * (y0 - s.y[-1])))
            if d < best_d:
                best, best_d = j, d
        if best is not None:
            nxt[i] = best
            used.add(best)
    out = []
    for i in order:
        if i in used:
            continue
        chain = Strand2D(list(strands[i].x), list(strands[i].y), list(strands[i].w))
        j = i
        while j in nxt:
            j = nxt[j]
            chain.x += strands[j].x; chain.y += strands[j].y; chain.w += strands[j].w
        out.append(chain)
    return out


def resample(strand, step):
    """Points every `step` rows from root to tip, always keeping the last row (the tip)."""
    y = np.asarray(strand.y, float)
    keep = np.unique(np.r_[np.searchsorted(y, np.arange(y[0], y[-1], step)), len(y) - 1])
    return np.asarray(strand.x, float)[keep], y[keep], np.asarray(strand.w, float)[keep]


def redraw(strands, shape):
    """Coverage mask painted back from traced strands (each row: centre +- width / 2)."""
    out = np.zeros(shape, bool)
    for s in strands:
        for x, y, w in zip(s.x, s.y, s.w):
            lo, hi = int(np.floor(x - w / 2 + 0.5)), int(np.floor(x + w / 2 - 0.5))
            out[y, max(lo, 0):min(hi, shape[1] - 1) + 1] = True
    return out
