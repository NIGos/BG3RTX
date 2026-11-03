"""Small offline renderer for the card vs groom comparison (no GPU, no game).

Orthographic views. Both versions become the same kind of fragments (pixel, depth, opacity,
fibre tangent, root parameter) and share everything after that: an opacity shadow map from
the light (Kim and Neumann 2001, slabs along the light direction), exact front-to-back
compositing per pixel, one fibre shading model (Kajiya-Kay diffuse and primary highlight
plus a forward-scattering transmission lobe), and an opaque ellipsoid head proxy.
Cards: rasterised triangles, opacity = the mask's alpha (box-filtered to the pixel size),
tangent = the card's root-to-tip direction dP/dv. Strands: each segment sampled every half
pixel; a sample covers 2 r x its projected length, as a fraction of the pixel area.
"""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np

HAIR = np.array([0.30, 0.20, 0.14])     # albedo-like tint, same for both versions
SKIN = np.array([0.55, 0.42, 0.36])


@dataclass
class View:
    centre: np.ndarray
    forward: np.ndarray   # unit vector into the screen
    up: np.ndarray
    pixel: float          # metres per pixel
    size: tuple           # (width, height)

    def basis(self):
        f = self.forward / np.linalg.norm(self.forward)
        r = np.cross(self.up, f)
        r /= np.linalg.norm(r)
        return r, np.cross(f, r), f

    def project(self, p):
        r, u, f = self.basis()
        d = p - self.centre
        x = d @ r / self.pixel + self.size[0] / 2
        y = -(d @ u) / self.pixel + self.size[1] / 2
        return x, y, d @ f


@dataclass
class Fragments:
    pixel: np.ndarray     # flat pixel index
    depth: np.ndarray
    alpha: np.ndarray
    tangent: np.ndarray   # (N, 3)
    root: np.ndarray      # 0 root .. 1 tip
    world: np.ndarray     # (N, 3) position, for the shadow lookup

    FIELDS = ("pixel", "depth", "alpha", "tangent", "root", "world")

    @staticmethod
    def concat(parts):
        if not parts:
            return Fragments(np.empty(0, np.int64), np.empty(0), np.empty(0),
                             np.empty((0, 3)), np.empty(0), np.empty((0, 3), np.float32))
        return Fragments(*(np.concatenate([getattr(p, k) for p in parts]) for k in Fragments.FIELDS))

    def select(self, keep):
        if np.asarray(keep).dtype == bool and np.all(keep):
            return self
        return Fragments(*(getattr(self, k)[keep] for k in Fragments.FIELDS))


def _keep(view, x, y):
    w, h = view.size
    xi, yi = np.floor(x).astype(np.int64), np.floor(y).astype(np.int64)
    ok = (xi >= 0) & (xi < w) & (yi >= 0) & (yi < h)
    return ok, yi * w + xi


def strand_fragments(points, radii, offsets, root, view, spacing=0.5):
    keep = np.ones(len(points), bool)
    keep[offsets[:-1]] = False
    seg = np.flatnonzero(keep)  # end vertex of each segment
    return Fragments.concat([_strand_chunk(points, radii, root, view, spacing, seg[i:i + 16384])
                             for i in range(0, len(seg), 16384)])


def _strand_chunk(points, radii, root, view, spacing, seg):
    p0, p1 = points[seg - 1].astype(np.float64), points[seg].astype(np.float64)
    x0, y0, _ = view.project(p0)
    x1, y1, _ = view.project(p1)
    n = np.maximum(1, np.ceil(np.hypot(x1 - x0, y1 - y0) / spacing - 1e-6)).astype(np.int64)
    s = np.repeat(np.arange(len(seg)), n)
    t = (np.arange(n.sum()) - np.repeat(np.cumsum(n) - n, n) + 0.5) / n[s]
    p = np.repeat(p0, n, axis=0) + np.repeat(p1 - p0, n, axis=0) * t[:, None]
    r = radii[seg - 1][s] * (1 - t) + radii[seg][s] * t
    rt = root[seg - 1][s] * (1 - t) + root[seg][s] * t
    x, y, d = view.project(p)
    proj_len = np.hypot(x1 - x0, y1 - y0)[s] * view.pixel / n[s]
    alpha = np.minimum(1.0, 2 * r * proj_len / view.pixel ** 2)
    tan = p1 - p0
    tan /= np.maximum(np.linalg.norm(tan, axis=1, keepdims=True), 1e-12)
    ok, pix = _keep(view, x, y)
    if np.all(ok):
        ok = slice(None)
    return Fragments(pix[ok], d[ok], alpha[ok], tan[s[ok]], rt[ok], p[ok].astype(np.float32))


def box_filter(alpha, factor):
    if factor <= 1:
        return alpha.astype(np.float32) / 255
    h, w = alpha.shape
    return alpha[: h // factor * factor, : w // factor * factor].reshape(
        h // factor, factor, w // factor, factor).mean((1, 3)).astype(np.float32) / 255


def card_fragments(positions, uv, tris, mask, view, chunk=1024, fibres=False):
    """Rasterise cards, optionally preserving projected fibre width (see _card_fragments)."""
    return _card_fragments(positions, uv, tris, mask, view, chunk, fibres)


def card_fragments_both(positions, uv, tris, mask, view, chunk=1024):
    """Return (fibres, plain) from one rasterisation; non-alpha fields may share storage."""
    return _card_fragments(positions, uv, tris, mask, view, chunk, True, both=True)


def card_fragment_builders(positions, uv, tris, mask):
    """Scoped pair of build(view) callbacks for two consecutive render() calls.

    Use fibres, plain = card_fragment_builders(...), then render(fibres, ...) and
    render(plain, ...) for the same view/light/head. Drop the builders after each
    view: their cache owns the two views' fragments, and inputs must stay unchanged.
    """
    cache = {}

    def build(view, which):
        key = (tuple(view.centre), tuple(view.forward), tuple(view.up), view.pixel, tuple(view.size))
        if key not in cache:
            cache[key] = card_fragments_both(positions, uv, tris, mask, view)
        return cache[key][which]

    return lambda v: build(v, 0), lambda v: build(v, 1)


def _card_fragments(positions, uv, tris, mask, view, chunk, fibres, both=False):
    """Rasterise card triangles; opacity from the mask filtered to roughly the pixel footprint.

    fibres: the card stands for the layer of fibres it paints (the strand reference of tools.hair_groom.batch). Face on,
    a painted fraction alpha of the card is covered; seen at an angle, the card's projected area shrinks by |n.v| while
    a fibre keeps its diameter and loses only the length along v (sqrt(1 - (t.v)^2), t the root-to-tip direction), so
    the fibres cover alpha sqrt(1 - (t.v)^2) / |n.v| of the projected card (at most 1). Without it, cards curved around a
    chin or seen edge-on (beards, 2026-11-03: Gale's strand coverage +0.11 at the beard's rim) under-state what fibres
    there cover."""
    # Texel size vs pixel: a card tile (256 texels) spans about 3 cm; pick the mip that matches.
    texel = 0.03 / (mask.shape[1] / 8)
    factor = int(2 ** max(0, np.floor(np.log2(view.pixel / texel))))
    alpha_tex = box_filter(mask[..., 3], factor)
    root_tex = box_filter(mask[..., 2], factor)
    th, tw = alpha_tex.shape
    out, plain_out = [], []
    x, y, d = view.project(positions.astype(np.float64))
    for c0 in range(0, len(tris), chunk):
        tri = tris[c0:c0 + chunk]
        tx, ty = x[tri], y[tri]
        lo_x, hi_x = np.floor(tx.min(1)).astype(np.int64), np.ceil(tx.max(1)).astype(np.int64)
        lo_y, hi_y = np.floor(ty.min(1)).astype(np.int64), np.ceil(ty.max(1)).astype(np.int64)
        lo_x, hi_x = np.maximum(lo_x, 0), np.minimum(hi_x, view.size[0] - 1)
        lo_y, hi_y = np.maximum(lo_y, 0), np.minimum(hi_y, view.size[1] - 1)
        bw, bh = np.maximum(0, hi_x - lo_x + 1), np.maximum(0, hi_y - lo_y + 1)
        n = bw * bh
        k = np.repeat(np.arange(len(tri)), n)
        local = np.arange(n.sum()) - np.repeat(np.cumsum(n) - n, n)
        px = lo_x[k] + local % bw[k] + 0.5
        py = lo_y[k] + local // bw[k] + 0.5
        ax, ay = tx[:, 0], ty[:, 0]
        dx1, dy1 = tx[:, 1] - ax, ty[:, 1] - ay
        dx2, dy2 = tx[:, 2] - ax, ty[:, 2] - ay
        det = dx1 * dy2 - dy1 * dx2
        det = np.where(np.abs(det) < 1e-12, np.nan, det)
        dx, dy = px - ax[k], py - ay[k]
        l1 = (dx * dy2[k] - dy * dx2[k]) / det[k]
        l2 = (dx1[k] * dy - dy1[k] * dx) / det[k]
        l0 = 1 - l1 - l2
        inside = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
        k, l0, l1, l2, px, py = k[inside], l0[inside], l1[inside], l2[inside], px[inside], py[inside]
        tv = tri[k]
        b = np.stack([l0, l1, l2], 1)
        uvs = np.einsum("nk,nkd->nd", b, uv[tv])
        ui = np.clip((uvs[:, 0] * tw).astype(np.int64), 0, tw - 1)
        vi = np.clip((uvs[:, 1] * th).astype(np.int64), 0, th - 1)
        a = alpha_tex[vi, ui]
        # Reject transparent texels before interpolating attributes or allocating world positions.
        nonzero = a > 0
        k, b, tv, px, py = k[nonzero], b[nonzero], tv[nonzero], px[nonzero], py[nonzero]
        a, ui, vi = a[nonzero], ui[nonzero], vi[nonzero]
        dep = np.einsum("nk,nk->n", b, d[tv])
        # dP/dv: the card's root-to-tip direction (the game's strand frame).
        P, T = positions[tri].astype(np.float64), uv[tri].astype(np.float64)
        e1, e2 = P[:, 1] - P[:, 0], P[:, 2] - P[:, 0]
        t1, t2 = T[:, 1] - T[:, 0], T[:, 2] - T[:, 0]
        tan = (e2 * t1[:, :1] - e1 * t2[:, :1])
        tan *= np.sign(t1[:, 0] * t2[:, 1] - t1[:, 1] * t2[:, 0])[:, None]
        tan /= np.maximum(np.linalg.norm(tan, axis=1, keepdims=True), 1e-12)
        plain_alpha = a
        if fibres:
            f = view.basis()[2]
            nrm = np.cross(e1, e2)
            nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-12)
            cos_n = np.abs(nrm @ f)
            sin_t = np.sqrt(np.clip(1.0 - (tan @ f) ** 2, 0.0, 1.0))
            a = np.minimum(1.0, a * sin_t[k] / np.maximum(cos_n[k], 1e-3))
        ok, pix = _keep(view, px, py)
        ok &= (plain_alpha > 0) if both else (a > 0)
        world = np.einsum("nk,nkd->nd", b[ok], P[k[ok]]).astype(np.float32)
        frag = Fragments(pix[ok], dep[ok], a[ok], tan[k[ok]], root_tex[vi, ui][ok], world)
        if both:
            plain_out.append(Fragments(frag.pixel, frag.depth, plain_alpha[ok], frag.tangent, frag.root, frag.world))
            frag = frag.select(frag.alpha > 0)
        out.append(frag)
    if both:
        return Fragments.concat(out), Fragments.concat(plain_out)
    return Fragments.concat(out)


def ellipsoid_hit(view, centre, radii):
    """Per-pixel depth and normal of the head proxy (inf where missed)."""
    w, h = view.size
    r, u, f = view.basis()
    xs = (np.arange(w) + 0.5 - w / 2) * view.pixel
    ys = -(np.arange(h) + 0.5 - h / 2) * view.pixel
    o = view.centre + xs[None, :, None] * r + ys[:, None, None] * u - 2.0 * f
    oc = (o - centre) / radii
    dv = f / radii
    a = dv @ dv
    b = 2 * oc @ dv
    c = (oc * oc).sum(-1) - 1
    disc = b * b - 4 * a * c
    t = np.where(disc >= 0, (-b - np.sqrt(np.maximum(disc, 0))) / (2 * a), np.inf)
    p = o + np.where(np.isfinite(t), t, 0)[..., None] * f
    n = (p - centre) / radii ** 2
    n /= np.maximum(np.linalg.norm(n, axis=-1, keepdims=True), 1e-12)
    depth = np.where(np.isfinite(t), (p - view.centre) @ f, np.inf)
    return depth.ravel(), n.reshape(-1, 3)


def _shadow_map(light_frags, light_view, slabs):
    """Opacity shadow map: optical depth per (light pixel, slab); returns a lookup."""
    if not len(light_frags.depth):
        npix = light_view.size[0] * light_view.size[1]
        return 0.0, 1.0, np.zeros((npix, slabs + 1))
    lo, hi = np.percentile(light_frags.depth, [0, 100])
    span = max(hi - lo, 1e-6)
    npix = light_view.size[0] * light_view.size[1]
    sl = np.clip(((light_frags.depth - lo) / span * slabs).astype(np.int64), 0, slabs - 1)
    tau = np.bincount(light_frags.pixel * (slabs + 1) + sl + 1,
                      weights=-np.log1p(-np.minimum(light_frags.alpha, 0.999)),
                      minlength=npix * (slabs + 1)).reshape(npix, slabs + 1)
    cum = np.cumsum(tau, 1)
    return lo, span, cum


def shadow_transmittance(light_frags, frag_points_light, light_view, slabs=64, *, shadow_map=None):
    """Look up opacity shadows; a render shares the map between hair and head."""
    lo, span, cum = shadow_map if shadow_map is not None else _shadow_map(light_frags, light_view, slabs)
    x, y, d = frag_points_light
    ok, pix = _keep(light_view, x, y)
    pos = np.clip((d - lo) / span * slabs, 0, slabs)
    i = np.clip(np.floor(pos).astype(np.int64), 0, slabs - 1)
    frac = pos - i
    pix = np.where(ok, pix, 0)
    depth_tau = cum[pix, i] * (1 - frac) + cum[pix, i + 1] * frac
    return np.where(ok, np.exp(-depth_tau), 1.0)


def shade(tangent, view_dir, light_dir, shadow, root):
    """One fibre model for both versions. Radiance of a fragment."""
    tl = tangent @ light_dir
    tv = tangent @ view_dir
    sin_l = np.sqrt(np.maximum(0, 1 - tl * tl))
    sin_v = np.sqrt(np.maximum(0, 1 - tv * tv))
    # Kajiya-Kay primary highlight: cos(theta_l - theta_v) term = tl*tv + sin_l*sin_v (shifted 0)
    spec = np.maximum(0, -tl * tv + sin_l * sin_v) ** 40 * 0.35
    # Forward scattering (TT): light passing through toward the viewer, tinted twice.
    fwd = np.maximum(0, -(view_dir @ light_dir))
    tt = (fwd ** 8) * sin_l * 1.2
    root_dark = 0.75 + 0.25 * root
    # Channel-wise arithmetic keeps the same operations while avoiding several N x 3 temporaries.
    colour = np.empty((len(tangent), 3))
    tint = HAIR ** 0.5
    for c in range(3):
        diffuse = sin_l * HAIR[c] * 0.9
        direct = (diffuse + spec + tt * tint[c]) * shadow
        colour[:, c] = (direct * 2.5 + HAIR[c] * 0.12) * root_dark
    return colour


def light_view_for(view, light_dir, pixel):
    up = np.array([0, 1.0, 0]) if abs(light_dir[1]) < 0.9 else np.array([0, 0, 1.0])
    side = int(0.8 / pixel)
    return View(view.centre, -light_dir, up, pixel, (side, side))


def _fragment_order(pix, depth):
    """Stable pixel/depth order using a radix key, refining any float32 collisions.

    Float32 is only a sorting bucket, never a replacement for the original depth.
    All unequal float64 depths in a shared bucket are sorted again exactly.
    """
    if len(pix) == 0:
        return np.empty(0, np.int64)
    if pix.max() >= 2 ** 32 or pix.min() < 0 or not np.all(np.isfinite(depth)):
        return np.lexsort((depth, pix))
    with np.errstate(over="ignore"):
        bits = depth.astype(np.float32).view(np.uint32)
    # IEEE sign-magnitude -> monotonically ordered unsigned bits (including -0).
    bits = np.where(bits & 0x80000000, ~bits, bits ^ np.uint32(0x80000000))
    # Signed zeros compare equal in lexsort, so give them the same bucket.
    bits = np.where(depth == 0, np.uint32(0x80000000), bits)
    key = (pix.astype(np.uint64) << 32) | bits.astype(np.uint64)
    order = np.argsort(key, kind="stable")
    keys, depths = key[order], depth[order]
    conflict = (keys[1:] == keys[:-1]) & (depths[1:] != depths[:-1])
    if np.any(conflict):
        bad = np.unique(keys[1:][conflict])
        lo, hi = np.searchsorted(keys, bad, side="left"), np.searchsorted(keys, bad, side="right")
        edges = np.zeros(len(order) + 1, np.int32)
        np.add.at(edges, lo, 1)
        np.add.at(edges, hi, -1)
        fix = np.flatnonzero(np.cumsum(edges[:-1]) != 0)
        refine = np.lexsort((depths[fix], keys[fix]))
        order[fix] = order[fix[refine]]
    return order


def composite(pix, depth, alpha, colour, npix):
    """Exact front-to-back 'over' per pixel. Returns radiance sum and total transmittance."""
    if not len(pix):
        return np.zeros((npix, 3)), np.ones(npix)
    order = _fragment_order(pix, depth)
    pix, a, colour = pix[order], np.minimum(alpha[order], 0.999), colour[order]
    tau = -np.log1p(-a)
    cum = np.cumsum(tau)
    start = np.r_[True, pix[1:] != pix[:-1]]
    first = np.maximum.accumulate(np.where(start, np.arange(len(pix)), 0))
    t_before = np.exp(-(cum - tau - (cum[first] - tau[first])))
    weighted = (t_before * a)[:, None] * colour
    image = np.stack([np.bincount(pix, weights=weighted[:, c], minlength=npix) for c in range(3)], 1)
    total = np.bincount(pix, weights=tau, minlength=npix)
    return image, np.exp(-total)


def render(build, view, light_dir, head, light_pixel=None, shade_fn=None):
    """build(view) -> Fragments of one hair version; head = (centre, radii) of the proxy. shade_fn: the fibre
    shading (default shade(); tools/hair_groom/bsdf_compare.py passes the physically based one)."""
    shade_fn = shade_fn or shade
    light_dir = light_dir / np.linalg.norm(light_dir)
    lview = light_view_for(view, light_dir, light_pixel or view.pixel * 2)
    lfrags = build(lview)
    frags = build(view)
    hd, hn = ellipsoid_hit(view, *head)
    frags = frags.select(frags.depth < hd[frags.pixel])
    shadow_map = _shadow_map(lfrags, lview, 64)
    shadow = shadow_transmittance(lfrags, lview.project(frags.world.astype(np.float64)), lview, shadow_map=shadow_map)
    r, u, f = view.basis()
    colour = shade_fn(frags.tangent, -f, light_dir, shadow, frags.root)
    w, h = view.size
    image, trans = composite(frags.pixel, frags.depth, frags.alpha, colour, w * h)
    # Background: the head proxy, lit and shadowed by the hair at its surface, or dark grey.
    head_px = np.isfinite(hd)
    xs = (np.arange(w) + 0.5 - w / 2) * view.pixel
    ys = -(np.arange(h) + 0.5 - h / 2) * view.pixel
    plane = (view.centre + xs[None, :, None] * r + ys[:, None, None] * u).reshape(-1, 3)
    surf = plane[head_px] + hd[head_px, None] * f + hn[head_px] * 1e-3
    hs = np.ones(w * h)
    hs[head_px] = shadow_transmittance(lfrags, lview.project(surf), lview, shadow_map=shadow_map)
    lam = np.maximum(0, hn @ light_dir)
    bg = np.where(head_px[:, None], SKIN[None, :] * (lam * hs * 2.0 + 0.12)[:, None],
                  np.array([0.05, 0.05, 0.06])[None, :])
    image += trans[:, None] * bg
    return image.reshape(h, w, 3), (1 - trans).reshape(h, w)


def to_png(image, exposure=1.0):
    x = np.clip(image * exposure, 0, None)
    x = x / (1 + x)  # Reinhard, same for both
    return (np.clip(x ** (1 / 2.2), 0, 1) * 255 + 0.5).astype(np.uint8)
