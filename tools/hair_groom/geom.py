"""numpy-only geometry helpers (the asset is generated on the player's PC: no scipy dependency)."""
from __future__ import annotations

import itertools

import numpy as np


def connected_components(tris, vertex_count):
    """(count, label per vertex) of the vertex graph of a triangle list (min-label propagation)."""
    tris = np.asarray(tris, np.int64)
    a = np.r_[tris[:, 0], tris[:, 1], tris[:, 2]]
    b = np.r_[tris[:, 1], tris[:, 2], tris[:, 0]]
    parent = np.arange(vertex_count, dtype=np.int64)
    while True:
        pa, pb = parent[a], parent[b]
        m = np.minimum(pa, pb)
        if not np.any((pa != m) | (pb != m)):
            break
        np.minimum.at(parent, pa, m)
        np.minimum.at(parent, pb, m)
        while True:   # pointer jumping to the roots
            nxt = parent[parent]
            if np.array_equal(nxt, parent):
                break
            parent = nxt
    roots, label = np.unique(parent, return_inverse=True)
    return len(roots), label


# The 13 neighbour cells of one half space plus the cell itself: every unordered cell pair once.
_OFFSETS = [o for o in itertools.product((-1, 0, 1), repeat=3) if o > (0, 0, 0)]


def pairs_within(points, distance):
    """(M, 2) index pairs (i != j, each unordered pair once) of points closer than `distance`."""
    p = np.asarray(points, np.float64)
    if len(p) < 2:
        return np.zeros((0, 2), np.int64)
    cell = np.floor((p - p.min(0)) / distance).astype(np.int64)
    span = cell.max(0) + 3
    key = lambda c: ((c[:, 0] + 1) * span[1] + (c[:, 1] + 1)) * span[2] + (c[:, 2] + 1)  # noqa: E731
    k = key(cell)
    order = np.argsort(k, kind="stable")
    sk = k[order]
    out = []
    for off in [(0, 0, 0)] + _OFFSETS:
        target = key(cell + np.array(off))
        lo = np.searchsorted(sk, target, "left")
        hi = np.searchsorted(sk, target, "right")
        n = hi - lo
        i = np.repeat(np.arange(len(p)), n)
        if not len(i):
            continue
        j = order[np.repeat(lo, n) + (np.arange(n.sum()) - np.repeat(np.cumsum(n) - n, n))]
        keep = (i < j) if off == (0, 0, 0) else (i != j)
        i, j = i[keep], j[keep]
        close = np.einsum("nd,nd->n", p[i] - p[j], p[i] - p[j]) < distance * distance
        out.append(np.stack([i[close], j[close]], 1))
    return np.concatenate(out) if out else np.zeros((0, 2), np.int64)
