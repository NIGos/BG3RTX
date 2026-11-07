"""2-D curve tracer for curly hair atlases (strands2d traces down the rows; this follows each strand's own direction).

Some atlases paint curls that loop back (HAIR_Afro_B_MSKA): the texture rows do not run root to tip, so the row
tracer of strands2d cannot follow them (its root channel check fails). Here a strand is followed along its own
direction: from a seed, each step looks at the mask across the current direction (a profile of +-K pixels), takes the
covered run nearest the predicted centre and recentres on it. Where that run is wider than 1.5 strand widths (the
clump rule of strands2d: a crossing, two strands touching, a painted clump) the strand keeps its direction and follows
the pixels of its own ID: the mask's R channel is constant along a painted strand up to noise, which each strand
measures on itself (running median absolute deviation, 3 sigma). A strand ends after `max_gap` pixels without
coverage (a crossing hides it for about the width of the strand on top). Seeds are taken root first (B ascending)
among unclaimed pixels and traced both ways; each strand is then ordered root to tip by the B channel (its
correlation with arc length, also the strand-wise root check that replaces the row check for these atlases).
"""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from .strands2d import row_runs, typical_width

# Edges along a profile are quantised to whole texels and the predicted centre lags a curl by up to half a texel:
# an edge within 1.5 texels of where the strand's edge is expected is the strand's edge.
EDGE_TOL = 1.5
# Texels between the two centres whose chord gives a strand's direction (centre noise +-0.5 texel: ~5 degrees).
CHORD = 6


@dataclass
class Curve2D:
    x: np.ndarray        # dense centreline, pixels (float), root to tip
    y: np.ndarray
    w: np.ndarray        # covered width across the strand, pixels
    root_corr: float     # correlation of the root channel with the arc length (>= 0 after ordering)


def blur(img, sigma):
    """Separable Gaussian blur (float32, edges clamped)."""
    r = max(1, int(3 * sigma))
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2).astype(np.float32)
    k /= k.sum()
    out = img.astype(np.float32)
    for axis in (0, 1):
        pad = [(0, 0), (0, 0)]
        pad[axis] = (r, r)
        p = np.pad(out, pad, mode="edge")
        acc = np.zeros_like(out)
        n = out.shape[axis]
        for i, kv in enumerate(k):
            acc += kv * (p[i:i + n] if axis == 0 else p[:, i:i + n])
        out = acc
    return out


def orientation_field(alpha, sigma):
    """Unit strand direction (dx, dy) per pixel from the structure tensor of the coverage (defined mod 180 degrees)."""
    a = alpha.astype(np.float32) / 255.0
    gy, gx = np.gradient(a)
    jxx, jxy, jyy = blur(gx * gx, sigma), blur(gx * gy, sigma), blur(gy * gy, sigma)
    theta = 0.5 * np.arctan2(2 * jxy, jxx - jyy) + np.pi / 2   # across the dominant gradient
    return np.cos(theta).astype(np.float32), np.sin(theta).astype(np.float32)


def root_row_correlation(mask, tile_grid, threshold=128):
    """Median over tiles of the correlation of the root channel with the row (the row tracer's assumption; the same
    numbers as analysis.root_channel_check). None without covered tiles."""
    h, w = mask.shape[:2]
    th, tw = h // tile_grid[1], w // tile_grid[0]
    out = []
    for ty in range(tile_grid[1]):
        for tx in range(tile_grid[0]):
            tile = mask[ty * th:(ty + 1) * th, tx * tw:(tx + 1) * tw]
            yy, xx = np.nonzero(tile[..., 3] >= threshold)
            if len(yy) > 10:
                out.append(float(np.corrcoef(yy, tile[yy, xx, 2])[0, 1]))
    return float(np.median(out)) if out else None


def typical_map(alpha, tile_grid, threshold=128):
    """Painted single-strand width per tile (strands2d.typical_width of each tile: the row tracer's clump unit)."""
    h, w = alpha.shape
    gx, gy = tile_grid if tile_grid else (1, 1)
    th, tw = h // gy, w // gx
    out = np.empty(alpha.shape, np.float32)
    for ty in range(gy):
        for tx in range(gx):
            y1 = h if ty == gy - 1 else (ty + 1) * th
            x1 = w if tx == gx - 1 else (tx + 1) * tw
            out[ty * th:y1, tx * tw:x1] = typical_width(alpha[ty * th:y1, tx * tw:x1], threshold)
    return out


def _median(v):
    s = np.sort(v)
    n = len(s)
    return float(s[n // 2]) if n % 2 else 0.5 * float(s[n // 2 - 1] + s[n // 2])


class Tracer:
    """Arrays are padded by P pixels so a profile never leaves them (the centre always stays inside the mask)."""

    def __init__(self, rgba, threshold=128, typical=None, max_gap=None, min_len=12, tile_grid=None):
        alpha = rgba[..., 3]
        self.H, self.W = alpha.shape
        if typical is None:
            typical = typical_map(alpha, tile_grid, threshold)
        self.typ = np.broadcast_to(np.asarray(typical, np.float32), alpha.shape)
        self.max_gap_arg = max_gap
        self.min_len = min_len
        self.K = int(np.ceil(max(4.0, 3.0 * float(self.typ.max()))))
        self.P = P = self.K + 2
        self.offs = np.arange(-self.K, self.K + 1, dtype=np.float64)
        self.cov = np.pad(alpha >= threshold, P)
        self.rid = np.pad(rgba[..., 0].astype(np.float32), P)
        self.root = rgba[..., 2]
        self.fx, self.fy = orientation_field(alpha, max(1.0, float(np.median(self.typ))))
        self.owner = np.full(self.cov.shape, -1, np.int32)
        self.owner_step = np.zeros(self.cov.shape, np.int32)
        self.tried = np.zeros(self.cov.shape, bool)
        self.log = []
        self.set_typical(float(np.median(self.typ)))

    def set_typical(self, t):
        self.t = t
        # A crossing hides the strand underneath for about the width of the one on top: two widths, at least 3 px.
        self.max_gap = self.max_gap_arg if self.max_gap_arg is not None else int(np.ceil(max(3.0, 2.0 * t)))

    def profile(self, qx, qy, nx, ny):
        """Covered texels across (nx, ny) through (qx, qy); padded pixel coordinates."""
        px = np.rint(qx + self.P + self.offs * nx).astype(np.intp)
        py = np.rint(qy + self.P + self.offs * ny).astype(np.intp)
        return self.cov[py, px], px, py

    def narrow(self, run_w, w):
        """A run of one strand (else a crossing, touching strands or a clump: the row tracer's 1.5 widths)."""
        return run_w <= 1.5 * max(w, self.t) + 1

    @staticmethod
    def id_tol(hist):
        """(median, tolerance) of this strand's ID: 3 sigma of its own noise (MAD), at least 8 of 255 (the noise of
        one sample: the ID medians of the 1-2 px strands of HAIR_Afro_B vary by up to 8 between neighbours)."""
        h = np.asarray(hist[-32:])
        med = _median(h)
        return med, max(8.0, 3.0 * 1.4826 * _median(np.abs(h - med)))

    def band(self, centre, width):
        lo = max(int(np.floor(self.K + centre - width / 2 + 0.5)), 0)
        return lo, min(max(int(np.floor(self.K + centre + width / 2 - 0.5)), lo), len(self.offs) - 1)

    def foreign(self, band, sid, step):
        o = self.owner[band]
        if not len(o):
            return False
        st = self.owner_step[band]
        f = (o >= 0) & ((o != sid) | (np.abs(st - step) > 2 * self.K + 2))
        return 2 * int(np.count_nonzero(f)) > len(o)

    def march(self, sid, x, y, dx, dy, w, rid_hist, step):
        """Follow one direction from (x, y); returns the centres (x, y, width, own) after the start.

        own: recentred on this strand's own pixels (not coasting through a crossing or a clump)."""
        out_x, out_y, out_w, out_own = [], [], [], []
        gap = coast_run = wide_run = 0
        k0 = self.K
        cx, cy = x, y
        max_steps = 4 * (self.H + self.W)
        med, tol = self.id_tol(rid_hist)
        fresh = 0
        hx, hy = [x], [y]   # centres recentred on the strand (for its direction and curvature)
        omega = 0.0          # turn per texel (radians): a curl keeps turning through a crossing
        max_turn = 1.0 / max(w, 2.0)   # a curl's radius is at least about the strand's width
        last_fit = True
        while len(out_x) < max_steps:
            if omega:
                co, so = np.cos(omega), np.sin(omega)
                dx, dy = dx * co - dy * so, dx * so + dy * co
                if not last_fit:
                    omega *= 0.8   # unseen (crossing, gap): the curvature estimate fades, ~5 texels of turn at most
            last_fit = False
            qx, qy = cx + dx, cy + dy
            if not (0 <= qx < self.W - 0.5 and 0 <= qy < self.H - 0.5):
                break
            nx, ny = -dy, dx
            cov, pxc, pyc = self.profile(qx, qy, nx, ny)
            reach = 0.75 * w + 1.5   # a larger sideways move belongs to another strand
            s, e = row_runs(cov)
            best, best_d = None, None
            for a, b in zip(s.tolist(), e.tolist()):
                if a - k0 <= reach and k0 - b <= reach:
                    d = abs((a + b) / 2 - k0)
                    if best is None or d < best_d:
                        best, best_d = (a, b), d
            if best is None:
                gap += 1
                if gap > self.max_gap:
                    break
                cx, cy = qx, qy
                continue
            a, b = best
            # Run edges relative to the predicted centre (pixel edges: +-0.5 around the end texels).
            le, re = a - k0 - 0.5, b - k0 + 0.5
            half = w / 2
            mid, run_w = (le + re) / 2, re - le
            left, right = abs(le + half) <= EDGE_TOL, abs(re - half) <= EDGE_TOL
            centre, width, fits, clean = None, w, True, False
            if (left and right) or (run_w <= w + EDGE_TOL and le < 0 < re):
                # The strand alone (possibly drifting or tapering): recentre on it, the width follows it.
                centre, width, clean = mid, run_w, True
            elif le < -0.5 and re > 0.5:
                # Covered where the strand should be, but the run is wider or spills to one side: a crossing, a
                # touching strand or a clump. Follow this strand's own ID pixels when they are known; else keep the
                # direction (a strand runs straight through a crossing), for at most max_gap texels.
                if len(rid_hist) >= 8:
                    ids = self.rid[pyc[a:b + 1], pxc[a:b + 1]]
                    s2, e2 = row_runs(np.abs(ids - med) <= tol)
                    for a2, b2 in zip((s2 + a).tolist(), (e2 + a).tolist()):
                        m2, w2 = (a2 + b2) / 2 - k0, b2 - a2 + 1.0
                        if abs(m2) <= EDGE_TOL and w2 <= w + EDGE_TOL:
                            centre, width = m2, w2
                            break
                if centre is None:
                    centre, fits = 0.0, False
            if centre is None:
                # Coverage beside the strand, not on it: as no coverage.
                gap += 1
                if gap > self.max_gap:
                    break
                cx, cy = qx, qy
                continue
            lo, hi = self.band(centre, width)
            band = (pyc[lo:hi + 1], pxc[lo:hi + 1])
            if len(rid_hist) >= 8 and abs(_median(self.rid[band]) - med) > tol:
                # Another strand's ID where this strand should be: it lies on top here; keep the direction.
                centre, width, fits = 0.0, w, False
                lo, hi = self.band(centre, width)
                band = (pyc[lo:hi + 1], pxc[lo:hi + 1])
            # Another strand's pixels (or this strand's own from a loop ago): a crossing, or a strand already traced.
            # Passing through lasts as long as a crossing (max_gap); longer means a duplicate: stop.
            own = fits and not self.foreign(band, sid, step)
            coast_run = 0 if own else coast_run + 1
            if getattr(self, "debug", None) is not None:
                self.debug.append((sid, step, round(qx, 1), round(qy, 1), round(dx, 2), round(dy, 2), le, re, round(w, 1),
                                   fits, own, round(centre, 1), len(rid_hist), round(med), round(tol)))
            if coast_run > self.max_gap:
                break
            lo, hi = self.band(centre, width)
            # Claim the band here and half a step back: rounded profiles one texel apart skip texels on diagonals.
            _, kx, ky = self.profile(qx - 0.5 * dx, qy - 0.5 * dy, nx, ny)
            by = np.r_[pyc[lo:hi + 1], ky[lo:hi + 1]]
            bx = np.r_[pxc[lo:hi + 1], kx[lo:hi + 1]]
            sel = self.cov[by, bx] & (self.owner[by, bx] < 0)
            if sel.any():
                by, bx = by[sel], bx[sel]
                self.owner[by, bx] = sid
                self.owner_step[by, bx] = step
                self.log.append((by, bx))
            if own:
                rid_hist.append(_median(self.rid[pyc[lo:hi + 1], pxc[lo:hi + 1]]))
                fresh += 1
                if fresh >= 4 or len(rid_hist) <= 8:
                    med, tol = self.id_tol(rid_hist)
                    fresh = 0
                w = 0.8 * w + 0.2 * width
            px_, py_ = qx + centre * nx, qy + centre * ny
            # New direction: the chord over the last CHORD recentred texels (centres are quantised to half texels: a
            # longer chord averages that out) turned on by the curvature it lags, blended with the structure tensor.
            iy, ix = min(max(int(round(py_)), 0), self.H - 1), min(max(int(round(px_)), 0), self.W - 1)
            fx, fy = float(self.fx[iy, ix]), float(self.fy[iy, ix])
            if fx * dx + fy * dy < 0:
                fx, fy = -fx, -fy
            clean = clean and fits   # only the strand seen alone steers it (a crossing pulls its centre aside)
            if clean:
                hx.append(px_); hy.append(py_)
            if clean and len(hx) > CHORD:
                ang = np.arctan2(hy[-1] - hy[-1 - CHORD], hx[-1] - hx[-1 - CHORD])
                if len(hx) > 2 * CHORD:
                    prev = np.arctan2(hy[-1 - CHORD] - hy[-1 - 2 * CHORD], hx[-1 - CHORD] - hx[-1 - 2 * CHORD])
                    turn = (ang - prev + np.pi) % (2 * np.pi) - np.pi
                    omega = min(max(0.5 * omega + 0.5 * turn / CHORD, -max_turn), max_turn)
                ang += omega * CHORD / 2   # the chord's direction is that of its middle
                ndx, ndy = np.cos(ang) + 0.2 * fx, np.sin(ang) + 0.2 * fy
                nl = (ndx * ndx + ndy * ndy) ** 0.5
                dx, dy = ndx / nl, ndy / nl
            elif clean:
                mx, my = px_ - cx, py_ - cy
                ml = (mx * mx + my * my) ** 0.5
                if ml > 1e-6:
                    mx, my = mx / ml, my / ml
                ndx, ndy = dx + 0.3 * mx + 0.2 * fx, dy + 0.3 * my + 0.2 * fy
                nl = (ndx * ndx + ndy * ndy) ** 0.5
                dx, dy = ndx / nl, ndy / nl
            cx, cy = px_, py_
            out_x.append(cx); out_y.append(cy); out_w.append(width); out_own.append(own)
            last_fit = fits
            gap = 0
            step += 1
        # A strand ends on its own pixels: drop the trailing coasted steps.
        while out_own and not out_own[-1]:
            out_x.pop(); out_y.pop(); out_w.pop(); out_own.pop()
        return out_x, out_y, out_w, out_own

    def trace_from(self, sid, x0, y0):
        self.set_typical(float(self.typ[y0, x0]))
        dx, dy = float(self.fx[y0, x0]), float(self.fy[y0, x0])
        cov, _, _ = self.profile(float(x0), float(y0), -dy, dx)
        s, e = row_runs(cov)
        k0 = self.K
        inside = [(a, b) for a, b in zip(s.tolist(), e.tolist()) if a <= k0 <= b]
        off, w = 0.0, self.t
        if inside and self.narrow(inside[0][1] - inside[0][0] + 1, self.t):
            a, b = inside[0]
            # A seed on an end cap or a fringe sees less than the strand's width: start from the tile's typical
            # width at least; the width then follows the strand's own runs.
            off, w = (a + b) / 2 - k0, max(float(b - a + 1), self.t)
        x, y = x0 - dy * off, y0 + dx * off
        self.log = []
        hist = [float(self.rid[y0 + self.P, x0 + self.P])]
        fx, fy, fw, fo = self.march(sid, x, y, dx, dy, w, list(hist), 1)
        # The backward half counts its steps from far below so the loop rule never confuses it with the forward one.
        bx, by, bw, bo = self.march(sid, x, y, -dx, -dy, w, list(hist), -(1 << 30))
        xs = np.array(bx[::-1] + [x] + fx, float)
        ys = np.array(by[::-1] + [y] + fy, float)
        ws = np.array(bw[::-1] + [w] + fw, float)
        # Length on the strand's own pixels (coasted steps through crossings and clumps do not count): the row
        # tracer's minimum length in rows.
        if len(xs) < 2 or sum(fo) + sum(bo) + 1 < self.min_len:
            for yy, xx in self.log:   # a dropped piece leaves its pixels to the strands they belong to
                self.owner[yy, xx] = -1
            return None
        arc = np.r_[0, np.cumsum(np.hypot(np.diff(xs), np.diff(ys)))]
        b = self.root[np.clip(np.rint(ys).astype(int), 0, self.H - 1), np.clip(np.rint(xs).astype(int), 0, self.W - 1)]
        b = b.astype(float)
        corr = float(np.corrcoef(arc, b)[0, 1]) if b.std() > 0 else 0.0
        if corr < 0:
            xs, ys, ws, corr = xs[::-1], ys[::-1], ws[::-1], -corr
        return Curve2D(xs, ys, ws, corr)


def trace(rgba, threshold=128, typical=None, max_gap=None, min_len=12, tile_grid=None):
    """Strand centrelines of a curly mask (H, W, 4 uint8: R id, G depth, B root, A coverage), root to tip.

    typical: strand width in pixels (scalar or per pixel); default: per tile of tile_grid (whole mask when None).
    Returns (curves, typical widths (H, W), claimed: bool (H, W) of the covered texels a kept strand took)."""
    tr = Tracer(rgba, threshold, typical, max_gap, min_len, tile_grid)
    P = tr.P
    core = (slice(P, P + tr.H), slice(P, P + tr.W))
    yy, xx = np.nonzero(tr.cov[core])
    order = np.lexsort((xx, yy, tr.root[yy, xx]))   # roots first, then raster order
    curves, sid = [], 0
    for i in order.tolist():
        y0, x0 = int(yy[i]), int(xx[i])
        if tr.owner[y0 + P, x0 + P] >= 0 or tr.tried[y0 + P, x0 + P]:
            continue
        tr.tried[y0 + P, x0 + P] = True
        c = tr.trace_from(sid, x0, y0)
        sid += 1
        if c is not None:
            curves.append(c)
    return curves, np.asarray(tr.typ), (tr.owner[core] >= 0) & tr.cov[core]

def simplify(x, y, eps, max_step):
    """Indices of a polyline kept by Douglas-Peucker (deviation <= eps) with no piece longer than max_step."""
    n = len(x)
    keep = np.zeros(n, bool)
    keep[0] = keep[-1] = True
    seg = np.r_[0, np.cumsum(np.hypot(np.diff(x), np.diff(y)))]
    stack = [(0, n - 1)]
    while stack:
        a, b = stack.pop()
        if b - a < 2:
            continue
        px, py = x[a + 1:b], y[a + 1:b]
        ex, ey = x[b] - x[a], y[b] - y[a]
        el = np.hypot(ex, ey)
        d = np.hypot(px - x[a], py - y[a]) if el < 1e-9 else np.abs((px - x[a]) * ey - (py - y[a]) * ex) / el
        k = int(np.argmax(d))
        if d[k] > eps:
            m = a + 1 + k
        elif seg[b] - seg[a] > max_step:
            m = (a + b) // 2
        else:
            continue
        keep[m] = True
        stack += [(a, m), (m, b)]
    return np.flatnonzero(keep)


def resample(curve, step, eps):
    """Points of a smoothed centreline: deviation <= eps pixels, spacing <= step pixels along the arc.
    Returns x, y, width (pixels, across the strand), and the unit normal (nx, ny): the direction of the width."""
    x, y, w = curve.x, curve.y, curve.w
    if len(x) >= 5:
        k = np.ones(5) / 5
        xs = np.convolve(np.pad(x, 2, mode="edge"), k, "valid")
        ys = np.convolve(np.pad(y, 2, mode="edge"), k, "valid")
        xs[0], ys[0], xs[-1], ys[-1] = x[0], y[0], x[-1], y[-1]
    else:
        xs, ys = x.astype(float), y.astype(float)
    idx = simplify(xs, ys, eps, step)
    tx, ty = np.gradient(xs), np.gradient(ys)
    tl = np.maximum(np.hypot(tx, ty), 1e-9)
    wm = np.array([np.median(w[max(0, i - 2):i + 3]) for i in idx])
    return xs[idx], ys[idx], wm, (-ty / tl)[idx], (tx / tl)[idx]
