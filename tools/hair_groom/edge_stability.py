"""Strand edge stability (docs/RTX_HAIR_PRIMARY.md 5n, key rr_hair_strand_aa): offline models of the dithering seen at
the edges of on-screen strands.

  py -3 tools/hair_groom/edge_stability.py [--frames 72] [--seed 1] [--json out.json]

1. Ray position (rejected candidate). Every strand candidate pixel traces one ray through its raster point (the pixel
   centre shifted by the screen-wide jitter): a sub-pixel strand is a dotted line that moves with the jitter. A per-pixel
   white-noise position (decorrelate_position) was modelled on the M3.2 fixture (frames_masks, run): unbiased, but the
   shared Halton jitter converges faster on an ideal accumulator, and the ray leaves the card at its edges
   (card_edge_loss). Metrics: raw (primary.coverage_metrics), ema (exponential history 0.1), taa (history clamped to the
   current 3 x 3 min/max).
2. The game's stencil dither resolve (resolve, twin of rr_post.hlsl `resolve`) on a card strip whose strands follow its
   alpha (card_strip, resolve_frames: tags from the card dither, from the strands, both, or none).
3. The analytic strand coverage of rr_strand_aa.hlsl (box_strand_coverage, found_probability, strand_aa_frames,
   strand_aa_run): each pixel's colour c Lh + (1 - c) Lb with c the sum of the found strands' box coverage over their
   found probability. Edge pixels: reference coverage in (0, 1); the first 18 frames (one Halton period at 1.5x) are
   warm-up; energy = mean colour against the 8 x 8 supersampled area reference.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.hair_groom import primary  # noqa: E402

PERIOD = 18   # DLSS jitter phases at 1.5x per axis: 8 x 1.5^2 (DLSS Programming Guide 3.7)
SALT = 0x68616972   # "hair": decorrelates the offset stream from the seal and the coverage pick (both use hp_mix of tag)


def mix(x):
    """hp_mix (lowbias32), vectorised over uint32 arrays."""
    x = np.asarray(x, np.uint64) & 0xFFFFFFFF
    x ^= x >> 16; x = (x * 0x7feb352d) & 0xFFFFFFFF
    x ^= x >> 15; x = (x * 0x846ca68b) & 0xFFFFFFFF
    return x ^ (x >> 16)


def decorrelate_position(px, py, tag):
    """The rejected candidate: the ray's position in the unjittered pixel (pixels from its centre, y down,
    [-0.5, 0.5)^2 at 24-bit steps). The shader adds it plus the frame jitter J to SV_Position.xy (the raster point is the
    centre shifted by -J in the unjittered image), so the ray samples the unjittered centre + this position."""
    h0 = mix(np.asarray(px, np.uint64) ^ mix(np.asarray(py, np.uint64) ^ mix(np.uint64((int(tag) ^ SALT) & 0xFFFFFFFF))))
    h1 = mix(h0)
    return np.stack([(h0 >> 8).astype(np.float64), (h1 >> 8).astype(np.float64)], -1) / 16777216.0 - 0.5


def frames_masks(points, radii, offsets, cam, frames, mode):
    """Per-frame strand masks: 'jitter' the baseline ray (unjittered sample offset primary.jitter, shared by every
    pixel), 'decorrelate' the per-pixel position (independent of the jitter)."""
    w, h = cam.size
    py, px = np.mgrid[0:h, 0:w]
    out = []
    for f in range(frames):
        off = primary.jitter(f % PERIOD) if mode == "jitter" else decorrelate_position(px, py, f + 1)
        out.append(primary.trace(points, radii, offsets, cam, off).hit)
    return np.array(out, np.float64)


def box_minmax(x):
    from numpy.lib.stride_tricks import sliding_window_view
    p = np.pad(x, 1, mode="edge")
    v = sliding_window_view(p, (3, 3))
    return v.min((-1, -2)), v.max((-1, -2))


def accumulate(masks, alpha=0.1, clamp=False):
    """Exponential history over the masks (static camera: no reprojection), optionally clamped to the current frame's
    3 x 3 min/max (a temporal reconstructor's neighbourhood rejection). Returns the output of every frame."""
    hist = masks[0].copy()
    out = [hist.copy()]
    for x in masks[1:]:
        if clamp:
            lo, hi = box_minmax(x)
            hist = np.clip(hist, lo, hi)
        hist = hist + alpha * (x - hist)
        out.append(hist.copy())
    return np.array(out)


def edge_metrics(out, ref, warm=PERIOD):
    o = out[warm:]
    edge = (ref > 0.0) & (ref < 1.0)
    mean = o.mean(0)
    return {"flicker": float(o.std(0)[edge].mean()),
            "bias": float((mean[edge].sum() - ref[edge].sum()) / max(ref[edge].sum(), 1e-12)),
            "rmse": float(np.sqrt(((o - ref) ** 2)[:, edge].mean())),
            "edge_pixels": int(edge.sum())}


def card_edge_loss(width_px, a_max=1.0, seed=3, n=64, cards=6, phases=PERIOD):
    """Strand hits per card fragment, decorrelated / baseline, on a card strip of `width_px` filled with strands up to
    its edges (0.33 of every 0.35 px across: test_coverage_pair_gate_bias's model) at random angles. The ray stays a
    candidate only where the card covers the raster point; strands beyond the card edge are not there (they lie on the
    card), so the decorrelated ray loses the strands it would have hit outside the card. Returns (ratio, baseline share,
    decorrelated share)."""
    rng = np.random.default_rng(seed)
    py, px = np.mgrid[0:n, 0:n]
    base = dec = frags = 0
    for _ in range(cards):
        a, c = rng.uniform(0, np.pi), rng.uniform(n / 2 - 4, n / 2 + 4, 2)
        card = lambda x, y: np.abs(-(x - c[0]) * np.sin(a) + (y - c[1]) * np.cos(a)) <= width_px / 2
        strand = lambda x, y: card(x, y) & (np.mod((x - c[0]) * np.cos(a) + (y - c[1]) * np.sin(a), 0.35) < 0.33)
        for t in range(1, phases + 1):
            j = primary.jitter(t - 1)
            rx, ry = px + 0.5 + j[0], py + 0.5 + j[1]   # raster point (unjittered image)
            frag = card(rx, ry)
            o = decorrelate_position(px, py, t)
            sx, sy = px + 0.5 + o[..., 0], py + 0.5 + o[..., 1]
            frags += frag.sum()
            base += (frag & strand(rx, ry)).sum()
            dec += (frag & strand(sx, sy)).sum()
    return dec / max(base, 1), base / max(frags, 1), dec / max(frags, 1)


# ---- the game's stencil-tagged dither resolve (7401d785, ported in shaders/rr_post.hlsl `resolve`) on strands ----
def resolve(x, b5, b4=None):
    """Twin of rr_post.hlsl resolve (7401d78564eb8bac): per pixel, over the 4 x 4 taps at offsets -1..2 (edge clamped),
    the geometric means g1 of the bit-5 taps and g0 of the others (3 x 3 taps always; row and column 2 only while a group
    is still empty), r = bit-5 taps / non-bit-4 taps, out = lerp(a0, a1, r) with the pixel's own colour lerped into its
    own group's mean by r. A pixel without both groups, or with bit 4, keeps its colour. x: (h, w) colours > 0."""
    h, w = x.shape
    b4 = np.zeros_like(b5) if b4 is None else b4
    acc1 = np.zeros((h, w)); acc0 = np.zeros((h, w))      # log sums
    n1 = np.zeros((h, w)); n0w = np.zeros((h, w))         # taps accumulated per group
    n5 = np.zeros((h, w)); n0 = np.zeros((h, w))
    yy, xx = np.mgrid[0:h, 0:w]
    for i in range(4):
        for j in range(4):
            qx, qy = np.clip(xx + i - 1, 0, w - 1), np.clip(yy + j - 1, 0, h - 1)
            s5, s4, col = b5[qy, qx], b4[qy, qx], x[qy, qx]
            n0 += ~s4
            n5 += s5
            take = ((i != 3) & (j != 3)) | (n1 == 0) | (n0w == 0)
            lc = np.log2(col)
            acc1 += np.where(take & s5, lc, 0.0); n1 += take & s5
            acc0 += np.where(take & ~s5, lc, 0.0); n0w += take & ~s5
    both = (n1 > 0) & (n0w > 0) & ~b4
    g1 = np.exp2(acc1 / np.maximum(n1, 1)); g0 = np.exp2(acc0 / np.maximum(n0w, 1))
    r = n5 / np.maximum(n0, 1)
    a1 = np.where(b5, g1 + (x - g1) * r, g1)
    a0 = np.where(b5, g0, g0 + (x - g0) * r)
    return np.where(both, a0 + (a1 - a0) * r, x)


def card_strip(n=64, width=24.0, fade=0.35, strand_px=0.3, density=None, angle=0.37, seed=5):
    """A hair card strip across an n x n image, alpha 1 in the middle fading linearly to 0 over `fade` of the half width
    at both edges (the silhouette), and the strands converted from it: lines along the strip at lateral offsets drawn
    with density proportional to the alpha (`density` strands per pixel of width where alpha = 1), `strand_px` wide.
    Returns (alpha(x, y), strand(x, y)) as vectorised point tests in image coordinates."""
    rng = np.random.default_rng(seed)
    c = np.array([n / 2, n / 2])
    ca, sa = np.cos(angle), np.sin(angle)
    half = width / 2
    lat = lambda x, y: -(x - c[0]) * sa + (y - c[1]) * ca
    prof = lambda d: np.clip((half - np.abs(d)) / (fade * half), 0.0, 1.0)
    k = int((density or 3.0 / strand_px) * width)   # default: three strand layers deep where alpha = 1 (opaque)
    cand = rng.uniform(-half, half, 4 * k)
    offs = cand[rng.uniform(0, 1, len(cand)) < prof(cand)][:k]
    def strand(x, y):
        d = lat(x, y)[..., None]
        return (np.abs(d - offs) < strand_px / 2).any(-1)
    return (lambda x, y: prof(lat(x, y))), strand


def resolve_frames(frames=72, n=64, hair=0.15, back=0.6, mode="off", **strip):
    """Per frame the RR input after the dither resolve, for the strands ('off': the tags are the prepass clone's card
    dither, native stencil state; 'on': the swapped strands' footprint untagged, rr_hair_strand_aa=1) and for the native
    cards ('cards': colour and tag from the same card dither). The ray samples the raster point (centre + jitter)."""
    alpha, strand = card_strip(n, **strip)
    py, px = np.mgrid[0:n, 0:n]
    out = []
    for f in range(frames):
        j = primary.jitter(f % PERIOD)
        rx, ry = px + 0.5 + j[0], py + 0.5 + j[1]
        a = alpha(rx, ry)
        dither = primary.tips_keep(a, np.stack([px + 0.5, py + 0.5], -1), f + 1, 0.0) & (a > 0)
        if mode == "cards":
            x, tag = np.where(dither, hair, back), dither
        else:
            hit = strand(rx, ry)
            x = np.where(hit, hair, back)
            # off: the prepass clone writes the tag (card dither); strand / union: the G-buffer clone writes it on the
            # strand fragments (alone, or as well); on: rr_hair_strand_aa=1 reads the swapped footprint as untagged.
            tag = {"off": dither, "strand": hit, "union": dither | hit}.get(mode, np.zeros_like(dither))
        out.append(resolve(x.astype(np.float64), tag))
    return np.array(out)


def resolve_reference(n=64, hair=0.15, back=0.6, cards=False, ss=8, **strip):
    """Area-coverage colour per pixel: strands (or, for the native cards, the card alpha) over the background."""
    alpha, strand = card_strip(n, **strip)
    py, px = np.mgrid[0:n, 0:n]
    acc = np.zeros((n, n))
    for i in range(ss):
        for j in range(ss):
            x, y = px + (i + 0.5) / ss, py + (j + 0.5) / ss
            acc += alpha(x, y) if cards else strand(x, y)
    c = acc / ss ** 2
    return c * hair + (1 - c) * back, c


def resolve_metrics(out, ref, cov, warm=PERIOD):
    """Edge pixels (0 < coverage < 1, the silhouette band and the gaps): the raw per-frame temporal std of the RR input
    (what RR has to remove), the 4-frame-phase share of it (the native dither's period: the ordered pattern), the
    ideal-accumulation (EMA 0.1) residual against the area reference and the energy (mean over the edge pixels and
    frames relative to the reference's)."""
    edge = (cov > 0.0) & (cov < 1.0)
    o = out[warm:]
    t = len(o) - len(o) % 4
    ph = o[:t].reshape(-1, 4, *o.shape[1:]).mean(0)            # mean per 4-frame phase
    phase_std = ph.std(0)[edge].mean()
    ema = accumulate(out)[warm:]
    return {"raw_temporal_std": float(o.std(0)[edge].mean()), "phase4_std": float(phase_std),
            "ema_rmse": float(np.sqrt(((ema - ref) ** 2)[:, edge].mean())),
            "ema_flicker": float(ema.std(0)[edge].mean()),
            "energy": float(o[:, edge].mean() / ref[edge].mean() - 1.0), "edge_pixels": int(edge.sum()),
            "frame_energy": float(o.mean() / ref.mean() - 1.0)}   # whole image: a blur cannot leak out of it


def resolve_run(frames=72, angles=(0.37, 1.1, 2.3), strand_px=0.3):
    res = {}
    for mode in ("cards", "off", "union", "strand", "on"):
        rows = []
        for a in angles:
            ref, cov = resolve_reference(cards=mode == "cards", angle=a, strand_px=strand_px)
            rows.append(resolve_metrics(resolve_frames(frames, mode=mode, angle=a, strand_px=strand_px), ref, cov))
        res[mode] = {k: float(np.mean([r[k] for r in rows])) for k in rows[0]}
    return res


# ---- analytic strand coverage resolve (GBAA-style: geometry in the buffer, colours exchanged across the edge) ----
def box_strand_coverage(d, half_w, nx, ny):
    """Share of a unit pixel box covered by an infinite straight band of half width `half_w` (pixels) whose axis passes
    at signed distance d from the box centre, with unit normal (nx, ny). The box's extent along the normal is a
    trapezoid of support |nx| + |ny|; the covered share is the integral of its normalised density over
    [d - half_w, d + half_w]. Twin of hp_box_coverage (shaders/rt/hair_strand_aa.hlsli)."""
    a, b = np.abs(nx), np.abs(ny)
    lo, hi = np.minimum(a, b), np.maximum(a, b)

    def cdf(x):   # CDF of the box's projection on the normal: a uniform [-lo/2, lo/2] convolved with [-hi/2, hi/2]
        x = np.clip(x, -(lo + hi) / 2, (lo + hi) / 2)
        lo_s = np.maximum(lo, 1e-6)
        u = x + (lo + hi) / 2                     # 0 .. lo + hi
        ramp = u ** 2 / (2 * lo_s * hi)           # 0 <= u <= lo
        mid = (u - lo / 2) / hi                   # lo <= u <= hi
        top = 1 - (lo + hi - u) ** 2 / (2 * lo_s * hi)
        return np.where(u <= lo, ramp, np.where(u <= hi, mid, top))
    return np.clip(cdf(d + half_w) - cdf(d - half_w), 0.0, 1.0)


def union_coverage(d, half_w, nx, ny, found):
    """Box coverage of the union of the found bands, all with the normal (nx, ny) (the model's strands are parallel):
    their intervals [d - w, d + w] along the normal merged in order of their lower ends, then the box CDF over each
    merged interval. Twin of the shader's sorted merge (the shader sums per strand instead): exact for parallel strands."""
    lo = np.where(found, d - half_w, np.inf)
    hi = np.where(found, d + half_w, np.inf)
    order = np.argsort(lo, -1)
    lo, hi = np.take_along_axis(lo, order, -1), np.take_along_axis(hi, order, -1)
    total = np.zeros(d.shape[:-1])
    cur_lo, cur_hi = lo[..., 0].copy(), hi[..., 0].copy()
    seg = lambda a, b: np.where(np.isfinite(a), box_strand_coverage((a + b) / 2, (b - a) / 2, nx, ny), 0.0)
    for i in range(1, d.shape[-1]):
        a, b = lo[..., i], hi[..., i]
        join = np.isfinite(a) & (a <= cur_hi)
        total += np.where(np.isfinite(a) & ~join, seg(cur_lo, cur_hi), 0.0)
        cur_lo = np.where(np.isfinite(a) & ~join, a, cur_lo)
        cur_hi = np.where(join, np.maximum(cur_hi, b), np.where(np.isfinite(a), b, cur_hi))
    return np.clip(total + seg(cur_lo, cur_hi), 0.0, 1.0)


def found_probability(d, half_w, nx, ny):
    """Twin of rr_strand_aa.hlsl found_probability: the probability over a uniform jitter in the pixel box that one of
    the 3 x 3 rays around a pixel (raster points at the unjittered centres plus the jitter) hits a band of half width
    half_w whose axis lies at distance d (along the unit normal (nx, ny)) from the pixel's unjittered centre."""
    d = np.asarray(d, np.float64)
    dq = np.stack([d + nx * dx + ny * dy for dy in (-1, 0, 1) for dx in (-1, 0, 1)], -1)
    return union_coverage(dq, half_w, nx, ny, np.ones(dq.shape, bool))


def strand_aa_frames(frames=72, n=64, hair=0.15, back=0.6, window=1, box="centre", weight=True, **strip):
    """Per frame the RR input with the analytic strand resolve: each pixel's colour becomes c Lh + (1 - c) Lb with c the
    analytic box coverage (at its raster point) of the strands that any pixel of its (2 window + 1)^2 neighbourhood hit
    this frame, Lh the hair colour such a pixel shows, Lb its own colour when it missed, else a missed neighbour's
    (none: unchanged). The game's resolve does not see the swapped footprint (as rr_hair_strand_aa=1)."""
    alpha, _ = card_strip(n, **strip)
    lines = _strip_lines(n, **strip)
    py, px = np.mgrid[0:n, 0:n]
    dcen = lines["lat"](px + 0.5, py + 0.5)[..., None] - lines["offs"]   # (n, n, k) pixel-centre distance to each axis
    ck_c = box_strand_coverage(dcen, lines["half"], lines["nx"], lines["ny"])
    if weight:
        # A strand is found only when the shared jitter puts some window pixel's ray on it: probability P = the box
        # coverage of the jitter square by the bands at -d_q (q the window pixels, d_q their pixel-centre distances;
        # lat() grows by nx dx + ny dy per pixel step). Geometry only: computed once. Weighting each found strand by
        # 1 / P makes its expected coverage its own (unbiased).
        pf = np.zeros_like(dcen)
        for k0 in range(0, dcen.shape[-1], 64):
            dq = np.stack([dcen[..., k0:k0 + 64] + lines["nx"] * dx + lines["ny"] * dy
                           for dy in range(-window, window + 1) for dx in range(-window, window + 1)], -1)
            pf[..., k0:k0 + 64] = union_coverage(-dq, lines["half"], lines["nx"], lines["ny"], np.ones(dq.shape, bool))
    out = []
    for f in range(frames):
        j = primary.jitter(f % PERIOD)
        rx, ry = px + 0.5 + j[0], py + 0.5 + j[1]
        d = lines["lat"](rx, ry)[..., None] - lines["offs"]           # (n, n, k) raster-point distance to each axis
        hitk = np.abs(d) < lines["half"]
        hit = hitk.any(-1)
        # A pixel entry records one strand (the closest): the first hit index stands for an arbitrary depth order.
        rec = np.zeros_like(hitk)
        first = np.argmax(hitk, -1)
        np.put_along_axis(rec, first[..., None], hit[..., None], -1)
        x = np.where(hit, hair, back)
        found = np.zeros_like(hitk)
        missed_nb = np.zeros((n, n), bool)
        for dy in range(-2, 3):
            for dx in range(-2, 3):
                sy, sx = np.clip(py + dy, 0, n - 1), np.clip(px + dx, 0, n - 1)
                if max(abs(dx), abs(dy)) <= window:
                    found |= rec[sy, sx]
                missed_nb |= ~hit[sy, sx]   # the background colour: from the 5 x 5 neighbourhood
        dc = d if box == "raster" else dcen
        ck = (box_strand_coverage(dc, lines["half"], lines["nx"], lines["ny"]) if box == "raster" else ck_c) * found
        if weight:
            c = np.minimum(1.0, (ck / np.maximum(pf, 1e-3)).sum(-1))   # the shader's estimator: sum of c / P, clamped
        else:
            c = union_coverage(dc, lines["half"], lines["nx"], lines["ny"], found)
        has_back = ~hit | missed_nb
        lb = back   # every missed pixel in the model shows the background
        o = np.where(found.any(-1) & has_back, c * hair + (1 - c) * lb, x)
        out.append(o)
    return np.array(out)


def _strip_lines(n=64, width=24.0, fade=0.35, strand_px=0.3, density=None, angle=0.37, seed=5):
    """The strand axes of card_strip (same draw): lateral offsets, half width and the unit normal in pixels."""
    rng = np.random.default_rng(seed)
    c = np.array([n / 2, n / 2])
    ca, sa = np.cos(angle), np.sin(angle)
    half = width / 2
    prof = lambda d: np.clip((half - np.abs(d)) / (fade * half), 0.0, 1.0)
    k = int((density or 3.0 / strand_px) * width)   # default: three strand layers deep where alpha = 1 (opaque)
    cand = rng.uniform(-half, half, 4 * k)
    offs = cand[rng.uniform(0, 1, len(cand)) < prof(cand)][:k]
    return {"lat": lambda x, y: -(x - c[0]) * sa + (y - c[1]) * ca, "offs": offs, "half": strand_px / 2,
            "nx": -sa, "ny": ca}


def strand_aa_run(frames=72, angles=(0.37, 1.1, 2.3), strand_px=0.3, window=1, box="centre", weight=True):
    """Raw ('on': rr_hair_strand_aa=1) against the analytic resolve, both against the area reference, plus the
    clamped-history proxy (taa) of each."""
    res = {}
    for mode in ("on", "aa"):
        rows = []
        for a in angles:
            ref, cov = resolve_reference(angle=a, strand_px=strand_px)
            out = (resolve_frames(frames, mode="on", angle=a, strand_px=strand_px) if mode == "on" else
                   strand_aa_frames(frames, angle=a, strand_px=strand_px, window=window, box=box, weight=weight))
            m = resolve_metrics(out, ref, cov)
            taa = accumulate(out, clamp=True)[PERIOD:]
            edge = (cov > 0) & (cov < 1)
            m["taa_rmse"] = float(np.sqrt(((taa - ref) ** 2)[:, edge].mean()))
            m["taa_flicker"] = float(taa.std(0)[edge].mean())
            m["taa_energy"] = float(taa[:, edge].mean() / ref[edge].mean() - 1.0)
            m["frame_energy"] = float(out[PERIOD:].mean() / ref.mean() - 1.0)   # whole image: blur cannot leak out
            rows.append(m)
        res[mode] = {k: float(np.mean([r[k] for r in rows])) for k in rows[0]}
    return res


def run(frames=72, seed=1):
    pts, rad, offs, cam = primary.subpixel_fixture(seed=seed)
    ref = primary.coverage_reference(pts, rad, offs, cam)
    res = {"frames": frames, "seed": seed}
    for mode in ("jitter", "decorrelate"):
        m = frames_masks(pts, rad, offs, cam, frames, mode)
        raw = primary.coverage_metrics(m, ref)
        res[mode] = {"raw": {k: raw[k] for k in ("bias", "box_rmse", "box_temporal_std", "bias_sigma")},
                     "ema": edge_metrics(accumulate(m), ref),
                     "taa": edge_metrics(accumulate(m, clamp=True), ref)}
    res["card_edge"] = {str(w): dict(zip(("ratio", "baseline_share", "decorrelate_share"), map(float, card_edge_loss(w))))
                        for w in (2.0, 8.0, 32.0)}
    return res


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--frames", type=int, default=72)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--json")
    a = ap.parse_args()
    res = run(a.frames, a.seed)
    text = json.dumps(res, indent=1)
    print(text)
    if a.json:
        Path(a.json).write_text(text + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
