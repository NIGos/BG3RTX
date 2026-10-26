"""numpy-only image operations (no scipy/OpenCV on this machine): box filters,
binary morphology, connected components and region measurements."""
import numpy as np


def box(a, r):
    """Mean over a (2r+1)^2 window, edges clamped; separable cumulative sums."""
    if r <= 0:
        return a.astype(np.float32, copy=False)
    a = a.astype(np.float32, copy=False)
    k = 2 * r + 1
    p = np.pad(a, ((r + 1, r), (0, 0)), mode="edge")
    c = np.cumsum(p, axis=0, dtype=np.float64)
    v = (c[k:] - c[:-k]) / k
    p = np.pad(v, ((0, 0), (r + 1, r)), mode="edge")
    c = np.cumsum(p, axis=1, dtype=np.float64)
    return ((c[:, k:] - c[:, :-k]) / k).astype(np.float32)


def gauss(a, sigma):
    """Three box passes approximate a Gaussian of this sigma."""
    if sigma <= 0.3:
        return a.astype(np.float32, copy=False)
    r = max(1, int(round((np.sqrt(4 * sigma * sigma + 1) - 1) / 2)))
    for _ in range(3):
        a = box(a, r)
    return a


def erode(m, r):
    return box(m.astype(np.float32), r) > 1.0 - 1e-4 if r > 0 else m


def dilate(m, r):
    return box(m.astype(np.float32), r) > 1e-4 if r > 0 else m


def opening(m, r):
    return dilate(erode(m, r), r)


def grad_mag(a):
    gy, gx = np.gradient(a.astype(np.float32))
    return np.hypot(gx, gy), gx, gy


def label(mask):
    """4-connected components. Returns (labels int32, 0 = background; count).
    Row runs, run overlaps between consecutive rows as graph edges, then min-label
    propagation with pointer jumping over the (small) run graph."""
    mask = np.asarray(mask, bool)
    h, w = mask.shape
    out = np.zeros((h, w), np.int32)
    p = np.zeros((h, w + 2), np.int8)
    p[:, 1:-1] = mask
    d = np.diff(p, axis=1)
    sr, sc = np.nonzero(d == 1)      # run starts (row-major order)
    er, ec = np.nonzero(d == -1)     # run ends (exclusive)
    nr = len(sr)
    if nr == 0:
        return out, 0
    row_first = np.searchsorted(sr, np.arange(h + 1))
    ea, eb = [], []
    for r in range(h - 1):
        a0, a1 = row_first[r], row_first[r + 1]
        b0, b1 = row_first[r + 1], row_first[r + 2]
        if a0 == a1 or b0 == b1:
            continue
        s_a, e_a = sc[a0:a1], ec[a0:a1]
        s_b, e_b = sc[b0:b1], ec[b0:b1]
        lo = np.searchsorted(e_b, s_a, "right")
        hi = np.searchsorted(s_b, e_a, "left")
        cnt = np.maximum(hi - lo, 0)
        if cnt.sum() == 0:
            continue
        ia = np.repeat(np.arange(a0, a1), cnt)
        off = np.arange(cnt.sum()) - np.repeat(np.cumsum(cnt) - cnt, cnt)
        ea.append(ia)
        eb.append(b0 + np.repeat(lo, cnt) + off)
    lab = np.arange(nr, dtype=np.int64)
    if ea:
        ea, eb = np.concatenate(ea), np.concatenate(eb)
        while True:
            prev = lab.copy()
            m = np.minimum(lab[ea], lab[eb])
            np.minimum.at(lab, ea, m)
            np.minimum.at(lab, eb, m)
            for _ in range(8):
                lab = lab[lab]
            if np.array_equal(lab, prev):
                break
    roots, inv = np.unique(lab, return_inverse=True)
    lens = ec - sc
    starts = sr.astype(np.int64) * w + sc
    idx = np.repeat(starts - np.cumsum(lens) + lens, lens) + np.arange(lens.sum())
    out.ravel()[idx] = np.repeat(inv.astype(np.int32) + 1, lens)
    return out, len(roots)


def regions(labels, n, min_area):
    """Yields (label id, area, bbox (y0, y1, x0, x1)) for components of at least min_area px."""
    if n == 0:
        return []
    flat = labels.ravel()
    area = np.bincount(flat, minlength=n + 1)
    ys, xs = np.nonzero(labels)
    lab = labels[ys, xs]
    keep = [i for i in range(1, n + 1) if area[i] >= min_area]
    if not keep:
        return []
    ymin = np.full(n + 1, 1 << 30); ymax = np.full(n + 1, -1)
    xmin = np.full(n + 1, 1 << 30); xmax = np.full(n + 1, -1)
    np.minimum.at(ymin, lab, ys); np.maximum.at(ymax, lab, ys)
    np.minimum.at(xmin, lab, xs); np.maximum.at(xmax, lab, xs)
    return [(i, int(area[i]), (int(ymin[i]), int(ymax[i]) + 1, int(xmin[i]), int(xmax[i]) + 1)) for i in keep]


def window(bbox, shape, pad):
    y0, y1, x0, x1 = bbox
    return (max(0, y0 - pad), min(shape[0], y1 + pad), max(0, x0 - pad), min(shape[1], x1 + pad))


def polygonality(gx, gy, sel):
    """How much of the edge energy along `sel` falls into 4 of 16 orientation bins
    (straight polygon edges: ~1; round blobs: ~0)."""
    w = np.hypot(gx[sel], gy[sel])
    if w.sum() <= 0:
        return 0.0
    ang = (np.arctan2(gy[sel], gx[sel]) % np.pi) / np.pi * 16
    hist = np.bincount(np.minimum(ang.astype(int), 15), weights=w, minlength=16)
    top = np.sort(hist)[-4:].sum() / hist.sum()
    return float(np.clip((top - 0.25) / 0.75, 0, 1))
def maxf(a, r):
    """Max over a (2r+1)^2 window (separable shifts)."""
    if r <= 0:
        return a
    out = a.copy()
    for s in range(1, r + 1):
        out[s:] = np.maximum(out[s:], a[:-s]); out[:-s] = np.maximum(out[:-s], a[s:])
    b = out.copy()
    for s in range(1, r + 1):
        out[:, s:] = np.maximum(out[:, s:], b[:, :-s]); out[:, :-s] = np.maximum(out[:, :-s], b[:, s:])
    return out
