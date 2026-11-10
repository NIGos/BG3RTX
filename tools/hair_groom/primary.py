"""Primary strand hair (docs/RTX_HAIR_PRIMARY.md): the CPU reference of what the strand fragment shaders write.

python -m tools.hair_groom.primary --groom <groom.npz> --out <new folder> [--size 512] [--distance 0.9]

The math twin of shaders/rt/hair_primary.hlsli: the depth of a strand hit on the card fragment's camera ray, the
fibre shading normal, and the G-buffer encodings that the game's decoders read back (stereographic RT0 normal,
12:12 octahedral RT3 tangent, RT2.w class bits). Plus a reference tracer: perspective pixel-centre rays against
the groom's segments as capsules (radius = mean of the two point radii; the hardware sweeps a linearly varying
radius, so edge pixels may differ by one) -> per pixel hit, distance, segment, parameter along it, tangent.
The CLI writes coverage, depth and tangent images and report.json for one groom (the GPU bench compares to them).
M3.2 sub-pixel coverage (shaders/rt/hair_primary_coverage.hlsli, key rt_hair_groom_coverage): coverage_offset and
coverage_pick are the shader's twins; coverage_reference supersamples the pixel area (n x n stratified rays) and
coverage_metrics scores a sequence of per-frame strand masks against it (--coverage FRAMES on the CLI).
"""
from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path

import numpy as np

HAIR_CLASS = 5


def tips_keep(alpha, pixel, tag, jitter):
    """Twin of hp_tips_keep: native TAA dither, m8=0/m9=1, no card grazing."""
    pixel = np.asarray(pixel, dtype=np.float32)
    phase = np.asarray((np.asarray(tag) - 1) & 3, dtype=np.float32)
    frac = lambda x: x - np.floor(x)
    pattern0 = frac((pixel[..., 1] * 2 + pixel[..., 0] - 1.5 + phase) * np.float32(0.2))
    pattern1 = frac(pixel[..., 0] * np.float32(2.4084) + pixel[..., 1] * np.float32(3.2535))
    dither = frac(np.float32(0.833) * pattern0 + np.float32(0.1666) * pattern1 + np.asarray(jitter, np.float32))
    return np.asarray(alpha, np.float32) + dither - np.float32(0.6667) >= np.float32(0.3333)


# ---- G-buffer encodings (inverse of the game's decoders; FRAME_MAP "G-buffer", bg3_materials.hlsli) ----
def stereo_encode(n):
    """RT0 (RG16F): rg = n.xy / (1 - n.z) * 0.28125 + 0.5 (G-buffer FS aa4efa4d). n: (..., 3) unit view-space."""
    n = np.asarray(n, np.float64)
    return n[..., :2] / np.maximum(1.0 - n[..., 2:3], 1e-7) * 0.28125 + 0.5


def stereo_decode(rg):
    """The game's decode (tiled lighting f469db78), as in FRAME_MAP."""
    e = np.asarray(rg, np.float64) * 3.5554 - 1.7777
    t = (e * e).sum(-1, keepdims=True) + 1.0
    n = np.concatenate([2.0 * e / t, 1.0 - 2.0 / t], -1)
    return np.where(t > 5.4e10, np.array([0.0, 0.0, 1.0]), n)


def oct12_encode(v):
    """RT3.xyz (RGBA8 UNORM) of a unit vector: 12-bit octahedral x, y split over three bytes (inverse of bg3_oct12)."""
    v = np.asarray(v, np.float64)
    p = v / np.abs(v).sum(-1, keepdims=True)
    sg = np.where(p[..., :2] >= 0.0, 1.0, -1.0)
    e = np.where(p[..., 2:3] >= 0.0, p[..., :2], (1.0 - np.abs(p[..., 1::-1])) * sg)
    q = np.clip(np.rint((e + 1.0) * 2047.0), 0, 4095).astype(np.int64)
    x, y = q[..., 0], q[..., 1]
    return np.stack([x >> 4, ((x & 15) << 4) | (y >> 8), y & 255], -1).astype(np.float64) / 255.0


def oct12_decode(rgb):
    """bg3_oct12 (shaders/rt/bg3_materials.hlsli, the game's 3033c895:344)."""
    b = np.asarray(rgb, np.float64) * 255.0
    h = b[..., 1] * 0.0625
    e = np.clip(np.stack([b[..., 0] * 16.0 + np.floor(h), (h - np.floor(h)) * 4096.0 + b[..., 2]], -1)
                * 0.00048851978 - 1.0, -1.0, 1.0)
    z = (1.0 - np.abs(e[..., 0])) - np.abs(e[..., 1])
    sg = np.where(e >= 0.0, 1.0, -1.0)
    xy = np.where(z[..., None] >= 0.0, e, (1.0 - np.abs(e[..., ::-1])) * sg)
    v = np.concatenate([xy, z[..., None]], -1)
    return v / np.linalg.norm(v, axis=-1, keepdims=True)


def rt2_w(cls=HAIR_CLASS, light_mask=0, copy_flag=False):
    """RT2.w (UNORM8): bits 0-2 material class, bit 3 the RG16F-copy flag, bits 4-6 the light-channel mask."""
    return float((cls & 7) | (int(copy_flag) << 3) | ((light_mask & 7) << 4)) / 255.0


def rt2_w_decode(w):
    b = int(round(float(w) * 255.0))
    return b & 7, bool(b & 8), (b >> 4) & 7


# ---- geometry on the camera ray ----
def fibre_normal(tangent, to_camera):
    """Shading normal of a fibre: the view direction made perpendicular to the tangent (Kajiya-Kay frame)."""
    t = np.asarray(tangent, np.float64)
    v = np.asarray(to_camera, np.float64)
    n = v - t * (v * t).sum(-1, keepdims=True)
    return n / np.maximum(np.linalg.norm(n, axis=-1, keepdims=True), 1e-12)


# ---- 5g: the spaces of the native hair card's G-buffer (VS 41a168e2 -> FS 42a95e17) ----
# The VS (row vectors, v * M): n = cross(T, B) sign * world3; location 1 = n * view3 (view space), location 3 =
# normalize(n) and locations 4/5 = normalize(B/T * world3) (world space). The FS writes RT0 = stereo(normalize(loc1)
# * facing) and RT3 = oct12(normalize(mat3(loc5, loc3 * facing, loc4) (0, shift, 1))): a view-space normal and a
# world-space tangent. The game's hair lighting (3033c895) reads that tangent without rotation.
def native_card_frame(n_obj, b_obj, t_obj, world3, view3):
    """VS 41a168e2 outputs (location 1, 3, 4, 5) for object-space normal/bitangent/tangent rows."""
    n = np.asarray(n_obj, np.float64) @ world3
    unit = lambda v: v / np.linalg.norm(v, axis=-1, keepdims=True)
    return n @ view3, unit(n), unit(np.asarray(b_obj, np.float64) @ world3), unit(np.asarray(t_obj, np.float64) @ world3)


def macro_normal(card_n_world, view3):
    """rt_hair_macro_normal: RT0's normal from the card fragment's world normal (gbuffer.hlsl, view = gCam.view)."""
    n = np.asarray(card_n_world, np.float64) @ view3
    return n / np.linalg.norm(n, axis=-1, keepdims=True)


def card_grazing(t, n, b, to_cam):
    """The card alpha's grazing factor (5a03e5b6: saturate(|N . V'| 5), V' the view vector projected on the frame and
    back; gbuffer.hlsl card_to_cam). The frame rows and the view vector must share one space."""
    basis = np.stack([t, n, b], -2)
    v = np.einsum("...ij,...i->...j", basis, np.einsum("...ij,...j->...i", basis, to_cam))
    v /= np.linalg.norm(v, axis=-1, keepdims=True)
    return np.clip(np.abs((v * n).sum(-1) / np.linalg.norm(n, axis=-1)) * 5.0, 0.0, 1.0)


def depth_a(near, far):
    """A of the native perspective depth D = A + B / w (NATIVE_HAIR "Native fragment depth")."""
    return near / (near - far)


def strand_depth(d_card, t_card, t_strand, a):
    """Perspective: on one camera ray the view depth w is proportional to the distance t, so
    D_s = A + (D_c - A) * t_c / t_s. Exact for any projection of the form A + B / w."""
    return a + (d_card - a) * (t_card / t_strand)


def segment_param(hit, p0, p1):
    """Parameter in [0, 1] of the point of segment p0 -> p1 closest to `hit` (hp_segment_param)."""
    d = np.asarray(p1, np.float64) - p0
    return np.clip(((np.asarray(hit) - p0) * d).sum(-1) / np.maximum((d * d).sum(-1), 1e-30), 0.0, 1.0)


def motion_vector(frag_xy, prev_rel, prev_vp, scale, offset, size, jitter, prev_jitter):
    """The native velocity FS b70cadc5 (and hair_primary_velocity.hlsl) for a strand point: fragment position plus the
    current jitter (y negated) minus the previous pixel of `prev_rel` (camera-relative to the previous camera,
    projected with the previous view-projection, row vectors) plus the previous jitter. Pixels, current - previous."""
    c = np.append(np.asarray(prev_rel, np.float64), 1.0) @ prev_vp
    uv = (c[:2] / c[3]) * np.array([0.5, -0.5]) + 0.5
    prev_px = (uv * scale + offset) * size + np.array([prev_jitter[0], -prev_jitter[1]])
    return np.asarray(frag_xy, np.float64) + np.array([jitter[0], -jitter[1]]) - prev_px


def strand_depth_ortho(d_card, t_card, t_strand, dd_dt):
    """Orthographic: D is affine in the distance along the (parallel) ray."""
    return d_card + (t_strand - t_card) * dd_dt


MAX_RADIUS = 0.01   # common/hair_groom_asset.h rejects radii >= 10 mm (hair_primary_max_radius)


def on_card(t_card, t_hit, cos_card, foot, seg_len):
    """hp_on_card: the hit lies on the card fragment's own card (strand points lie on their card triangles)."""
    return abs(t_hit - t_card) <= (MAX_RADIUS + 0.5 * seg_len) / max(cos_card, 1e-6) + foot


def segment_ok(row, seg, i0, pool_bytes):
    """hp_segment's bounds (64-bit offsets): every pool byte the strand readers touch for segment seg of table row
    (points, uv, starts, prev offsets), whose first point is i0, lies inside the mesh pool."""
    x, y, z, w = row
    inside = lambda off, n: off + n <= pool_bytes
    return (inside(z + seg * 4, 4) and inside(x + i0 * 12, 24) and (y == 0 or inside(y + i0 * 8, 16))
            and (w == 0 or inside(w + i0 * 12, 24)))


def _mix(x):
    x &= 0xFFFFFFFF
    x ^= x >> 16; x = (x * 0x7feb352d) & 0xFFFFFFFF
    x ^= x >> 15; x = (x * 0x846ca68b) & 0xFFFFFFFF
    return x ^ (x >> 16)


def seal(x, y, z, tag):
    """hp_seal: word 3 of a pixel entry."""
    return ((tag ^ _mix(x ^ _mix(y ^ _mix(z)))) | 0x80000000) & 0xFFFFFFFF


# ---- reference tracer ----
@dataclass
class Camera:
    position: np.ndarray
    forward: np.ndarray
    up: np.ndarray
    fov_y: float        # radians
    size: tuple         # (width, height)

    def basis(self):
        f = self.forward / np.linalg.norm(self.forward)
        r = np.cross(self.up, f)
        r /= np.linalg.norm(r)
        return r, np.cross(f, r), f   # right, up, forward: view space x, y, z (z into the screen)

    def rays(self, jitter=(0.0, 0.0)):
        """Unit ray per pixel through its centre plus `jitter` (pixels, y down): one (2,) offset for the frame, or an
        (h, w, 2) offset per pixel (the coverage pair)."""
        w, h = self.size
        r, u, f = self.basis()
        k = np.tan(self.fov_y / 2) / (h / 2)
        j = np.asarray(jitter, np.float64)
        if j.ndim == 1:
            x = (np.arange(w) + 0.5 + j[0] - w / 2) * k
            y = -(np.arange(h) + 0.5 + j[1] - h / 2) * k
            d = f[None, None] + x[None, :, None] * r[None, None] + y[:, None, None] * u[None, None]
        else:
            x = (np.arange(w)[None, :] + 0.5 + j[..., 0] - w / 2) * k
            y = -(np.arange(h)[:, None] + 0.5 + j[..., 1] - h / 2) * k
            d = f + x[..., None] * r + y[..., None] * u
        return (d / np.linalg.norm(d, axis=-1, keepdims=True)).reshape(-1, 3)

    def project(self, p):
        r, u, f = self.basis()
        d = p - self.position
        z = d @ f
        k = np.tan(self.fov_y / 2) / (self.size[1] / 2)
        return d @ r / (z * k) + self.size[0] / 2, -(d @ u) / (z * k) + self.size[1] / 2, z


def segments(points, radii, offsets):
    """(start point index, end point index) of every segment; offsets as in groom.npz (strand starts + end)."""
    end = np.flatnonzero(~np.isin(np.arange(len(points)), offsets[:-1]))
    end = end[end > 0]
    return end - 1, end


def capsule_hits(ro, rd, a, b, r):
    """Vectorised ray-capsule (I. Quilez' capIntersect): distance t (inf: miss) and the parameter along a->b."""
    ba, oa = b - a, ro - a
    baba = (ba * ba).sum(-1)
    bard = (ba * rd).sum(-1)
    baoa = (ba * oa).sum(-1)
    rdoa = (rd * oa).sum(-1)
    oaoa = (oa * oa).sum(-1)
    qa = baba - bard * bard
    qb = baba * rdoa - baoa * bard
    qc = baba * oaoa - baoa * baoa - r * r * baba
    h = qb * qb - qa * qc
    t = np.full(len(ro), np.inf)
    with np.errstate(invalid="ignore", divide="ignore"):
        tb = (-qb - np.sqrt(np.maximum(h, 0.0))) / qa
        y = baoa + tb * bard
        body = (h >= 0.0) & (qa > 0.0) & (y > 0.0) & (y < baba) & (tb > 0.0)
        t[body] = tb[body]
        for cap, c in ((y <= 0.0, a), (y >= baba, b)):
            oc = ro - c
            cb = (rd * oc).sum(-1)
            cc = (oc * oc).sum(-1) - r * r
            ch = cb * cb - cc
            tc = -cb - np.sqrt(np.maximum(ch, 0.0))
            m = ~body & cap & (h >= 0.0) & (ch > 0.0) & (tc > 0.0) & (tc < t)
            t[m] = tc[m]
        v = np.clip((baoa + t * bard) / np.maximum(baba, 1e-30), 0.0, 1.0)
    return t, v


@dataclass
class Hits:
    hit: np.ndarray       # (h, w) bool
    t: np.ndarray         # distance from the camera (inf: miss)
    segment: np.ndarray   # segment index (-1: miss)
    v: np.ndarray         # parameter along the segment
    tangent: np.ndarray   # (h, w, 3) unit root->tip direction


def trace(points, radii, offsets, cam, jitter=(0.0, 0.0), chunk=2_000_000):
    """Closest capsule hit per pixel-centre ray. Segments are binned to the pixels of their projected bounds."""
    w, h = cam.size
    s0, s1 = segments(points, radii, offsets)
    p0, p1 = points[s0].astype(np.float64), points[s1].astype(np.float64)
    rad = (radii[s0] + radii[s1]).astype(np.float64) / 2
    x0, y0, z0 = cam.project(p0)
    x1, y1, z1 = cam.project(p1)
    k = np.tan(cam.fov_y / 2) / (h / 2)
    pad = rad / (np.minimum(z0, z1) * k) + 1.0
    front = (z0 > 0) & (z1 > 0)
    lo_x = np.floor(np.minimum(x0, x1) - pad).astype(np.int64).clip(0, w)
    hi_x = np.ceil(np.maximum(x0, x1) + pad).astype(np.int64).clip(0, w)
    lo_y = np.floor(np.minimum(y0, y1) - pad).astype(np.int64).clip(0, h)
    hi_y = np.ceil(np.maximum(y0, y1) + pad).astype(np.int64).clip(0, h)
    nx, ny = np.where(front, hi_x - lo_x, 0), np.where(front, hi_y - lo_y, 0)
    count = nx * ny
    rays = cam.rays(jitter)
    best_t = np.full(w * h, np.inf)
    best_s = np.full(w * h, -1, np.int64)
    best_v = np.zeros(w * h)
    seg = np.flatnonzero(count)
    bounds = np.searchsorted(np.cumsum(count[seg]), np.arange(chunk, count[seg].sum() + chunk, chunk))
    start = 0
    for stop in np.unique(np.append(bounds, len(seg))):
        part = seg[start:stop]
        start = stop
        if not len(part):
            continue
        n = count[part]
        si = np.repeat(part, n)
        local = np.arange(n.sum()) - np.repeat(np.cumsum(n) - n, n)
        px = lo_x[si] + local % nx[si]
        py = lo_y[si] + local // nx[si]
        pix = py * w + px
        t, v = capsule_hits(np.broadcast_to(cam.position, (len(si), 3)), rays[pix], p0[si], p1[si], rad[si])
        m = np.isfinite(t)
        pix, t, v, si = pix[m], t[m], v[m], si[m]
        if not len(pix):   # a chunk without any hit (sub-pixel strands missed by every ray)
            continue
        order = np.lexsort((t, pix))
        pix, t, v, si = pix[order], t[order], v[order], si[order]
        first = np.r_[True, pix[1:] != pix[:-1]]
        pix, t, v, si = pix[first], t[first], v[first], si[first]
        better = t < best_t[pix]
        best_t[pix[better]], best_s[pix[better]], best_v[pix[better]] = t[better], si[better], v[better]
    hit = best_s >= 0
    tan = np.zeros((w * h, 3))
    d = p1[best_s[hit]] - p0[best_s[hit]]
    tan[hit] = d / np.maximum(np.linalg.norm(d, axis=1, keepdims=True), 1e-12)
    return Hits(hit.reshape(h, w), best_t.reshape(h, w), best_s.reshape(h, w), best_v.reshape(h, w),
                tan.reshape(h, w, 3))


# ---- M3.2 sub-pixel coverage: twins of shaders/rt/hair_primary_coverage.hlsli and its reference ----
def coverage_offset(px, py, i):
    """hp_cov_offset: (..., 2) pixel offset of ray i (0 A, 1 B), a quarter pixel either side of the jittered centre on
    the diagonal of the pixel's checkerboard parity."""
    s = -0.25 if i == 0 else 0.25
    odd = ((np.asarray(px) + np.asarray(py)) & 1) != 0
    return np.stack([np.full(odd.shape, s), np.where(odd, -s, s)], -1)


def coverage_pick(hit_a, hit_b, t_a, t_b, px, py, tag):
    """hp_cov_pick: 0 keeps ray A's hit, 1 ray B's, 2 none. Both hit: the nearer; one hit: a checkerboard whose phase is
    hashed per frame keeps half of them, so the expected coverage is (h_A + h_B) / 2."""
    hit_a, hit_b = np.asarray(hit_a, bool), np.asarray(hit_b, bool)
    keep = (((np.asarray(px) + np.asarray(py)) ^ _mix(int(tag))) & 1) == 0
    pick = np.full(hit_a.shape, 2)
    pick = np.where(hit_a & hit_b, np.where(np.asarray(t_b) < np.asarray(t_a), 1, 0), pick)
    return np.where((hit_a ^ hit_b) & keep, np.where(hit_a, 0, 1), pick)


def halton(i, base):
    """Radical inverse of i in `base` (the DLSS jitter sequence uses bases 2 and 3, from index 1)."""
    f, r = 1.0, 0.0
    while i > 0:
        f /= base
        r += f * (i % base)
        i //= base
    return r


def jitter(frame):
    """Sub-pixel camera jitter of frame `frame` (0-based): Halton (2, 3) from index 1, centred, pixels."""
    return np.array([halton(frame + 1, 2) - 0.5, halton(frame + 1, 3) - 0.5])


def subpixel_fixture(seed=1, n=200, size=256, fov_deg=20.0):
    """Wavy 0.25 mm strands (radius 0.125 mm) 0.6-3 m in front of a camera at the origin: sub-pixel at every distance
    (pixel 1.4-7 mm at 256 pixels and 20 degrees). Returns (points, radii, offsets, camera)."""
    rng = np.random.default_rng(seed)
    pts, rad, offs = [], [], [0]
    for _ in range(n):
        dist = rng.uniform(0.6, 3.0)
        root = np.array([*rng.uniform(-0.18, 0.18, 2) * dist, dist])
        d = rng.normal(size=3)
        d[2] *= 0.3
        d /= np.linalg.norm(d)
        s = np.linspace(0, 0.1, 12)[:, None]
        p = root + s * d + 0.003 * np.sin(s * 80 + rng.uniform(0, 6)) * np.array([[0.0, 1.0, 0.0]])
        pts.append(p)
        rad.append(np.full(len(p), 0.000125))
        offs.append(offs[-1] + len(p))
    cam = Camera(np.zeros(3), np.array([0.0, 0.0, 1.0]), np.array([0.0, 1.0, 0.0]), np.radians(fov_deg), (size, size))
    return np.concatenate(pts), np.concatenate(rad), np.array(offs), cam


def coverage_reference(points, radii, offsets, cam, n=8):
    """Area coverage of every pixel by the strands: the hit share of n x n stratified rays per pixel."""
    acc = np.zeros(cam.size[::-1])
    for i in range(n):
        for j in range(n):
            acc += trace(points, radii, offsets, cam, ((i + 0.5) / n - 0.5, (j + 0.5) / n - 0.5)).hit
    return acc / (n * n)


def coverage_frames(points, radii, offsets, cam, frames=16, pair=False):
    """Per-frame strand masks (frames, h, w) with the frame jitter: the single pixel ray, or with `pair` the two rays
    and hp_cov_pick (tag = frame + 1, as the layer's header tag is frame index + 1)."""
    w, h = cam.size
    py, px = np.mgrid[0:h, 0:w]
    out = []
    for f in range(frames):
        j = jitter(f)
        if not pair:
            out.append(trace(points, radii, offsets, cam, j).hit)
            continue
        a = trace(points, radii, offsets, cam, j + coverage_offset(px, py, 0))
        b = trace(points, radii, offsets, cam, j + coverage_offset(px, py, 1))
        out.append(coverage_pick(a.hit, b.hit, a.t, b.t, px, py, f + 1) != 2)
    return np.array(out)


def box_mean(x, r=1):
    """Mean over the (2r + 1)^2 pixel box (zero outside the image) of the last two axes."""
    from numpy.lib.stride_tricks import sliding_window_view
    p = np.pad(np.asarray(x, np.float64), [(0, 0)] * (np.ndim(x) - 2) + [(r, r), (r, r)])
    return sliding_window_view(p, (2 * r + 1, 2 * r + 1), axis=(-2, -1)).mean((-1, -2))


def coverage_metrics(masks, ref, r=1):
    """Scores of per-frame strand masks against the area coverage: relative bias of the total (energy), RMS error of the
    box-filtered coverage per frame (what a small reconstruction filter sees; aliasing) and its temporal standard
    deviation (flicker), both over the pixels whose box holds any strand."""
    m = np.asarray(masks, np.float64)
    b, rb = box_mean(m, r), box_mean(ref, r)
    roi = rb > 0
    return {"frames": int(len(m)), "reference_coverage": float(ref.sum()),
            "bias": float((m.mean(0).sum() - ref.sum()) / max(ref.sum(), 1e-12)),
            "box_rmse": float(np.sqrt(((b - rb) ** 2)[:, roi].mean())),
            "box_temporal_std": float(b.std(0)[roi].mean()),
            "bias_sigma": float(np.sqrt(np.sum(ref * (1.0 - ref))) / max(ref.sum(), 1e-12))}


def gbuffer(hits, cam, near=0.1, far=1000.0):
    """The words the strand G-buffer variant writes, from the reference hits: native depth, RT0 rg, RT3 rgb, RT2.w."""
    r, u, f = cam.basis()
    rays = cam.rays().reshape(hits.t.shape + (3,))
    a = depth_a(near, far)
    w = np.where(hits.hit, hits.t * (rays @ f), np.inf)             # view depth of the hit
    b = -a * far                                                      # reversed Z: D(near) = 1, D(far) = 0
    depth = np.where(hits.hit, a + b / w, 0.0)
    n_world = fibre_normal(hits.tangent, -rays)
    n_view = np.stack([n_world @ r, n_world @ u, n_world @ f], -1)
    return {"depth": depth, "rt0": np.where(hits.hit[..., None], stereo_encode(n_view), 0.0),
            "rt3": np.where(hits.hit[..., None], oct12_encode(np.where(hits.hit[..., None], hits.tangent, 1.0)), 0.0),
            "rt2w": rt2_w(), "n_view": n_view}


# ---- M3.4 post depth: what the post passes read on strand pixels ----
def d24(depth):
    """The game's D24S8 depth aspect: 24-bit UNORM, round to nearest."""
    return np.round(np.clip(depth, 0.0, 1.0) * (2 ** 24 - 1)) / (2 ** 24 - 1)


def view_depth(depth, near=0.1, far=1000.0):
    """Linear (view) depth w from the native reversed depth D = A + B / w (FRAME_MAP step 4 linearises the D24S8 like
    this; DoF, AO/SSR, fog and the camera motion-vector pass read the D24S8 or that linear depth)."""
    a = depth_a(near, far)
    with np.errstate(divide="ignore"):
        return np.where(depth > 0.0, (-a * far) / (depth - a), np.inf)


def card_fixture(seed=4, size=160, fov_deg=20.0, dist=1.0, strands=60, band=0.004):
    """Strands embedded in one alpha-tested card sheet in front of a wall, as the converter places them (every strand
    point on its card's triangles, radius 0.125 mm): the card plane z = dist tilted 35 degrees about y, strand points up
    to `band` / 2 off the sheet (the card's own thickness of curls), a wall 0.4 m behind. Returns (points, radii,
    offsets, camera, card): card = dict(normal, point, half extent) of the sheet."""
    rng = np.random.default_rng(seed)
    ang = np.radians(35.0)
    n = np.array([np.sin(ang), 0.0, -np.cos(ang)])
    e1, e2 = np.array([np.cos(ang), 0.0, np.sin(ang)]), np.array([0.0, 1.0, 0.0])
    c = np.array([0.0, 0.0, dist])
    pts, rad, offs = [], [], [0]
    for _ in range(strands):
        x0 = rng.uniform(-0.05, 0.05)
        s = np.linspace(-0.06, 0.06, 14)
        p = (c + (x0 + 0.004 * np.sin(s * 60 + rng.uniform(0, 6)))[:, None] * e1 + s[:, None] * e2 +
             (rng.uniform(-band, band) / 2 * np.cos(s * 40 + rng.uniform(0, 6)))[:, None] * n)
        pts.append(p)
        rad.append(np.full(len(p), 0.000125))
        offs.append(offs[-1] + len(p))
    cam = Camera(np.zeros(3), np.array([0.0, 0.0, 1.0]), np.array([0.0, 1.0, 0.0]), np.radians(fov_deg), (size, size))
    card = {"normal": n, "point": c, "e1": e1, "e2": e2, "half": (0.065, 0.07), "wall_z": dist + 0.4}
    return np.concatenate(pts), np.concatenate(rad), np.array(offs), cam, card


def post_depth(points, radii, offsets, cam, card, keep_card_depth=False, alpha_px=1.5, near=0.1, far=1000.0):
    """The D24S8 the post passes read after the G-buffer pass, per pixel, and which surface the colour shows.

    Strand path (layer/hair_primary.cpp): the prepass clone writes no card depth, so the depth before the G-buffer is
    the wall's; the strand G-buffer variant (GREATER_OR_EQUAL, writes) stores the strand hit's depth where the card's
    raster holds a strand hit and discards elsewhere (wall colour and depth stay). keep_card_depth: the card depth stays
    in the buffer (the native Depth FS, alpha-tested at `alpha_px` pixels around the strands; its virtual offset h is
    ignored, which only moves it further back) while the colour comes from the strands: the mismatch a halo needs.
    Returns dict(depth, colour_w, depth_w, strand) of (h, w) arrays: colour_w the view depth of the surface the colour
    shows, depth_w the view depth the post passes reconstruct."""
    w, h = cam.size
    rays = cam.rays().reshape(h, w, 3)
    _, _, f = cam.basis()
    cos = rays @ f
    a = depth_a(near, far)
    native = lambda t: a + (-a * far) / (t * cos)
    nrm, pnt = card["normal"], card["point"]
    with np.errstate(divide="ignore", invalid="ignore"):
        t_card = (pnt @ nrm) / (rays @ nrm)
        q = rays * t_card[..., None] - pnt
        inside = (np.abs(q @ card["e1"]) <= card["half"][0]) & (np.abs(q @ card["e2"]) <= card["half"][1]) & (t_card > 0)
        t_wall = card["wall_z"] / cos
    hits = trace(points, radii, offsets, cam)
    strand = hits.hit & inside                     # the card raster is the strand candidate region
    return _post_depth(strand, hits.t, t_card, inside, t_wall, native, keep_card_depth, alpha_px, cos)


def _post_depth(strand, t_strand, t_card, inside, t_wall, native, keep_card_depth, alpha_px, cos):
    r = int(np.ceil(alpha_px))
    m = np.pad(strand, r)
    alpha = np.zeros_like(strand)
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            if dx * dx + dy * dy <= alpha_px * alpha_px:
                alpha |= m[r + dy:r + dy + strand.shape[0], r + dx:r + dx + strand.shape[1]]
    alpha &= inside
    t_depth = np.where(strand, t_strand, t_wall)
    if keep_card_depth:
        t_depth = np.where(alpha, np.minimum(t_card, t_wall), t_wall)
    depth = d24(native(t_depth))
    return {"depth": depth, "strand": strand, "card_alpha": alpha,
            "colour_w": np.where(strand, t_strand, t_wall) * cos, "depth_w": view_depth(depth)}


def post_depth_report(frame, near=0.1, far=1000.0):
    """Halo pixels: the depth the post passes read is not the surface the colour shows, beyond the D24 precision at that
    depth (one 24-bit step, converted to view depth: dw = w^2 / |B| / (2^24 - 1), |B| = -A far)."""
    a = depth_a(near, far)
    step = frame["colour_w"] ** 2 / abs(-a * far) / (2 ** 24 - 1)
    err = np.abs(frame["depth_w"] - frame["colour_w"])
    bad = err > step
    s = frame["strand"]
    return {"strand_pixels": int(s.sum()), "card_alpha_pixels": int(frame["card_alpha"].sum()),
            "halo_pixels": int(bad.sum()), "halo_strand_pixels": int((bad & s).sum()),
            "halo_background_pixels": int((bad & ~s).sum()),
            "max_err_mm": float(err[np.isfinite(err)].max() * 1e3), "max_step_mm": float(step[np.isfinite(step)].max() * 1e3)}


# ---- 5u: the coverage gate's measurement against the pixel footprint (docs/RTX_HAIR_PRIMARY.md 5u) ----
def card_mask_fixture(seed=6, texels=256, lines=30, paint=6.0, strand=0.95, supersample=4):
    """A hair card's alpha mask and its groom in card texel space: `lines` straight strands (random x, slope up to
    0.25), painted `paint` texels wide (alpha = the texel's share inside a band, supersample^2 samples), and the groom's
    strands on the same centre lines `strand` times as wide (the converter puts the strands where the card is opaque;
    the offline bound's minimum style has strand/card opacity 0.91). Returns (mask [texels, texels], lines (x0, slope),
    half width of a strand in texels)."""
    rng = np.random.default_rng(seed)
    x0, slope = rng.uniform(0, texels, lines), rng.uniform(-0.25, 0.25, lines)
    s = (np.arange(texels * supersample) + 0.5) / supersample
    y, x = np.meshgrid(s, s, indexing="ij")
    inside = np.zeros(x.shape, bool)
    for a, b in zip(x0, slope):
        inside |= band_distance(x, y, a, b, texels) <= paint / 2
    mask = inside.reshape(texels, supersample, texels, supersample).mean((1, 3))
    return mask, np.stack([x0, slope], 1), strand * paint / 2


def band_distance(x, y, x0, slope, texels):
    """Distance (texels) of (x, y) to the wrapped line x = x0 + slope (y - texels / 2)."""
    d = np.mod(x - (x0 + slope * (y - texels / 2)) + texels / 2, texels) - texels / 2
    return np.abs(d) / np.sqrt(1 + slope * slope)


def mip_chain(mask):
    """Box-filtered mips (the mean alpha of every level equals the mask's)."""
    levels = [np.asarray(mask, np.float64)]
    while levels[-1].shape[0] > 1:
        m = levels[-1]
        levels.append(m.reshape(m.shape[0] // 2, 2, m.shape[1] // 2, 2).mean((1, 3)))
    return levels


def sample_trilinear(levels, u, v, lod):
    """SampleGrad with a wrap sampler: bilinear in the two mips around `lod` (texel units of level 0 for u, v)."""
    lod = float(np.clip(lod, 0.0, len(levels) - 1))
    lo = int(np.floor(lod))
    hi, f = min(lo + 1, len(levels) - 1), lod - lo

    def bilinear(m, scale):
        n = m.shape[0]
        x, y = u / scale - 0.5, v / scale - 0.5
        x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
        fx, fy = x - x0, y - y0
        at = lambda i, j: m[np.mod(j, n), np.mod(i, n)]
        return ((at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx) * (1 - fy) +
                (at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx) * fy)

    a = bilinear(levels[lo], 2.0 ** lo)
    return a if f == 0 else a * (1 - f) + bilinear(levels[hi], 2.0 ** hi) * f


def gate_counts(mask, lines, half, footprint, frames=16, seed=0):
    """What the count shader measures for one card seen frontally at `footprint` card texels per pixel, per frame: card
    pixels C (the card marker: the dithered native alpha test at the jittered sample, keep share = the mip-filtered
    alpha), accepted strand pixels inside them S (the gate's ratio today: mark.y == card word) and all accepted strand
    pixels U (any pixel of the card's raster, the accepted marker alone). One ray per pixel through the jittered sample
    (Halton jitter, frame f), the dither a uniform variate per pixel and frame (hp_tips_keep keeps exactly the alpha
    share, 5c). Returns int arrays (frames, 3): C, S, U."""
    rng = np.random.default_rng(seed)
    texels = mask.shape[0]
    n = max(1, int(round(texels / footprint)))
    levels = mip_chain(mask)
    py, px = np.mgrid[0:n, 0:n]
    out = []
    for f in range(frames):
        j = jitter(f)
        u, v = (px + 0.5 + j[0]) * footprint, (py + 0.5 + j[1]) * footprint
        alpha = sample_trilinear(levels, u, v, np.log2(footprint))
        card = alpha + rng.uniform(size=alpha.shape) >= 1.0
        hit = np.zeros(u.shape, bool)
        for a, b in lines:
            hit |= band_distance(u, v, a, b, texels) <= half
        out.append((int(card.sum()), int((card & hit).sum()), int(hit.sum())))
    return np.array(out)


def footprint_report(footprints=(0.5, 1, 2, 4, 8, 16), frames=16, seed=6):
    """The gate's two ratios over footprints (5u): S/C (today) and U/C (the bound's definition: strand opacity over card
    opacity), pooled over `frames`, and the per-frame U/C spread; plus the fixture's own opacity ratio (strand area /
    card alpha, the offline bound's quantity)."""
    mask, lines, half = card_mask_fixture(seed=seed)
    texels = mask.shape[0]
    ss = 8
    s = (np.arange(texels * ss) + 0.5) / ss
    y, x = np.meshgrid(s, s, indexing="ij")
    area = np.zeros(x.shape, bool)
    for a, b in lines:
        area |= band_distance(x, y, a, b, texels) <= half
    rows = []
    for fp in footprints:
        c = gate_counts(mask, lines, half, fp, frames, seed)
        per = c[:, 2] / np.maximum(c[:, 0], 1)
        rows.append({"footprint": float(fp), "card_pixels": float(c[:, 0].mean()),
                     "intersection": float(c[:, 1].sum() / c[:, 0].sum()), "union": float(c[:, 2].sum() / c[:, 0].sum()),
                     "union_frame_std": float(per.std()), "intersection_frame_min": float((c[:, 1] / c[:, 0]).min())})
    return {"opacity_ratio": float(area.mean() / mask.mean()), "mean_alpha": float(mask.mean()), "rows": rows}


def main():
    from PIL import Image
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--groom", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--size", type=int, default=512)
    ap.add_argument("--distance", type=float, default=0.9)
    ap.add_argument("--fov", type=float, default=30.0, help="vertical field of view, degrees")
    ap.add_argument("--coverage", type=int, default=0, metavar="FRAMES",
                    help="also score the single ray and the M3.2 pair over FRAMES jittered frames (8 x 8 reference)")
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    g = np.load(args.groom)
    centre = np.array([0.0, 1.57, 0.05])
    cam = Camera(centre + np.array([0.0, 0.0, -args.distance]), np.array([0.0, 0.0, 1.0]), np.array([0.0, 1.0, 0.0]),
                 np.radians(args.fov), (args.size, args.size))
    hits = trace(g["points"], g["radii"], g["offsets"], cam)
    gb = gbuffer(hits, cam)
    cov = hits.hit
    Image.fromarray((cov * 255).astype(np.uint8)).save(args.out / "coverage.png")
    t = np.where(cov, hits.t, np.nan)
    lo, hi = np.nanmin(t), np.nanmax(t)
    Image.fromarray(np.where(cov, 255 - (t - lo) / max(hi - lo, 1e-9) * 200, 0).astype(np.uint8)).save(args.out / "distance.png")
    Image.fromarray((np.where(cov[..., None], hits.tangent * 0.5 + 0.5, 0) * 255).astype(np.uint8)).save(args.out / "tangent.png")
    np.savez_compressed(args.out / "reference.npz", hit=cov, t=hits.t, segment=hits.segment, v=hits.v,
                        tangent=hits.tangent, depth=gb["depth"], rt0=gb["rt0"], rt3=gb["rt3"])
    report = {"size": args.size, "distance_m": args.distance, "fov_deg": args.fov, "hair_pixels": int(cov.sum()),
              "coverage": float(cov.mean()), "t_min": float(lo), "t_max": float(hi)}
    if args.coverage:
        ref = coverage_reference(g["points"], g["radii"], g["offsets"], cam)
        report["subpixel"] = {name: coverage_metrics(coverage_frames(g["points"], g["radii"], g["offsets"], cam,
                                                                     args.coverage, pair), ref)
                              for name, pair in (("single", False), ("pair", True))}
    (args.out / "report.json").write_text(json.dumps(report, indent=1) + "\n", encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
