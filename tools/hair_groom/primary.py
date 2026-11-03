"""Primary strand hair (docs/RTX_HAIR_PRIMARY.md): the CPU reference of what the strand fragment shaders write.

python -m tools.hair_groom.primary --groom <groom.npz> --out <new folder> [--size 512] [--distance 0.9]

The math twin of shaders/rt/hair_primary.hlsli: the depth of a strand hit on the card fragment's camera ray, the
fibre shading normal, and the G-buffer encodings that the game's decoders read back (stereographic RT0 normal,
12:12 octahedral RT3 tangent, RT2.w class bits). Plus a reference tracer: perspective pixel-centre rays against
the groom's segments as capsules (radius = mean of the two point radii; the hardware sweeps a linearly varying
radius, so edge pixels may differ by one) -> per pixel hit, distance, segment, parameter along it, tangent.
The CLI writes coverage, depth and tangent images and report.json for one groom (the GPU bench compares to them).
"""
from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path

import numpy as np

HAIR_CLASS = 5


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
        w, h = self.size
        r, u, f = self.basis()
        k = np.tan(self.fov_y / 2) / (h / 2)
        x = (np.arange(w) + 0.5 + jitter[0] - w / 2) * k
        y = -(np.arange(h) + 0.5 + jitter[1] - h / 2) * k
        d = f[None, None] + x[None, :, None] * r[None, None] + y[:, None, None] * u[None, None]
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


def main():
    from PIL import Image
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--groom", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--size", type=int, default=512)
    ap.add_argument("--distance", type=float, default=0.9)
    ap.add_argument("--fov", type=float, default=30.0, help="vertical field of view, degrees")
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
    (args.out / "report.json").write_text(json.dumps(report, indent=1) + "\n", encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
