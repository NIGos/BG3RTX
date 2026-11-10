"""Pixel-footprint LOD study (docs/progress/claude-hair-pixel-lod-study.md): would a strand keep fraction chosen from
the pixel size (strand diameter about k render pixels, radius widened by 1 / keep) give the one-binary-sample strand
G-buffer a sharper, steadier image than today's LOD by bounds angle? Offline, no game, no GPU.

python -m tools.hair_groom.pixel_lod --hcap <style.hcap> --hgrm <style.hgrm> --out <new or existing folder>
        [--heads 40 80 150] [--frames 32] [--shadow] [--selfcheck] [--ladder [--ks 0.5 0.6 0.7 0.8]]

Inputs: the style's card mesh and mask (.hcap v1: `bg3rtx_hairconv --all --match <gr2 name> --export-hcap <dir>`, read
only from the game's packages) and its groom (.hgrm, data/assetdb/hair_grooms). Both stay outside the repository.

Scene: the groom in bind pose seen from the front and 35 degrees above, at the distance where a 0.25 m head is `head`
render pixels tall (render 2560 x 1067, vertical field of view 0.838 rad as the live log prints; only the groom's crop
is rendered). One binary ray per render pixel and frame through the game's jitter (primary.jitter: Halton 2, 3 from
index 1, the values the live log shows for frames 0-2). Cases: LOD 0, 1, 2 (keep 1, 0.5, 0.25; "today" is the one
bvhsplit's angle rule picks) and the width-driven keep = median LOD 0 diameter / (k x pixel) for k = 0.7, 1.0, 1.4
(storage.thin_mask's nested selection, radii / keep, the 9 mm cap). Reference: LOD 0, true radius, 6 x 6 stratified
rays per pixel of a 3 x render grid (144 rays per 1.5 x output pixel, 324 per render pixel). Signal per hit:
0.35 + 0.65 x hash(strand), a per-strand albedo, so that line structure exists in the bulk where coverage is 1.
Accumulation: every frame's samples splatted bilinearly into the 1.5 x output grid (3840 x 1600), normalised: the
ideal static-camera temporal upscale (no history rejection, no learnt prior). Cards: the same pipeline on the card
mesh with the game's stochastic alpha test (keep share = the mip-filtered alpha, hp_tips_keep) and the mask's ID
channel as the per-painted-strand value.

The rasteriser works in screen space (distance of the sample point to the projected segment against the projected,
linearly varying radius; pieces of at most one pixel are binned to the sample points in their bounds). --selfcheck
compares it with primary.trace's ray-capsule tracer on a subset.
"""
from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

import numpy as np

from . import grain_stages, hcap, primary, storage

FOV_Y = 0.838            # rad, the live log's "fov" (hl1-s8-late-arrival.1.161106.log); taken as vertical
RENDER_H = 1067
HEAD_M = 0.25            # the head height the pixel counts refer to
UPSCALE = 1.5            # 2560 x 1067 -> 3840 x 1600
REF_SCALE, REF_N = 3, 6  # reference grid (x render) and stratified rays per reference pixel side
ANGLE_LOD = (0.09, 0.03)  # layer/bvhsplit.cpp groom_pick_lod: bounds diagonal / distance above -> LOD 0, 1, else 2
KEEPS = (1.0, 0.5, 0.25)
BULK = 0.95               # reference coverage at or above this is the bulk (energy and line measures)
_T0 = time.perf_counter()
_LAST = [0.0]


def log(msg, force=True):
    now = time.perf_counter()
    if force or now - _LAST[0] > 20:
        _LAST[0] = now
        print(f"[pixel_lod {now - _T0:6.0f}s] {msg}", flush=True)


# ---- data ----------------------------------------------------------------------------------------------------------
def load(hcap_path, hgrm_path):
    """(mesh, mask, points (P, 3) f64, radii (P,), strand offsets (S + 1,)) of LOD 0."""
    m, mask, _, _, _ = hcap.read(hcap_path)
    l0 = storage.read_any(hgrm_path).lods[0]
    v = m.positions[m.tris[l0.triangle]].astype(np.float64)
    b1, b2 = l0.bary[:, 0:1].astype(np.float64), l0.bary[:, 1:2].astype(np.float64)
    pts = v[:, 0] * (1 - b1 - b2) + v[:, 1] * b1 + v[:, 2] * b2
    return m, mask, pts, l0.radius.astype(np.float64), storage.strand_offsets(l0.points, l0.segment)


def variant(pts, rad, off, keep, scale=1.0):
    """The groom thinned as convert.thin does for any keep: (points, radii, offsets, original strand ids, capped share).
    scale (--ladder's coverage-conserving case): the widened radii times this factor."""
    n = len(off) - 1
    if keep >= 1 and scale == 1.0:
        return pts, rad, off, np.arange(n), 0.0
    sel = storage.thin_mask(n, keep) if keep < 1 else np.ones(n, bool)
    lengths = np.diff(off)
    p = np.repeat(sel, lengths)
    wide = rad[p] / min(keep, 1.0) * scale
    return (pts[p], np.minimum(wide, storage.MAX_THIN_RADIUS), np.concatenate([[0], np.cumsum(lengths[sel])]),
            np.flatnonzero(sel), float((wide > storage.MAX_THIN_RADIUS).mean()))


def strand_value(ids):
    """Per-strand value in [0, 1): a murmur3 finaliser, independent of the thinning hash (a plain multiplicative
    hash is correlated with storage.thin_mask and shifted the mean of the kept strands by up to -33%)."""
    m = np.uint64(0xFFFFFFFF)
    h = (np.asarray(ids, np.uint64) + np.uint64(0x5BD1E995)) & m
    h ^= h >> np.uint64(16); h = (h * np.uint64(0x85EBCA6B)) & m
    h ^= h >> np.uint64(13); h = (h * np.uint64(0xC2B2AE35)) & m
    h ^= h >> np.uint64(16)
    return h.astype(np.float64) / 2.0 ** 32


# ---- camera --------------------------------------------------------------------------------------------------------
def pixel_angle():
    return np.tan(FOV_Y / 2) / (RENDER_H / 2)


def crop_camera(pts, pixel_m, direction, scale=1.0, k=None):
    """primary.Camera on the points' centre from `direction` (towards the camera) with pixel size `pixel_m` at the
    centre; the image is the points' crop (even size, 6 pixels of margin), `scale` times the resolution."""
    k = pixel_angle() if k is None else k
    centre = (pts.min(0) + pts.max(0)) / 2
    d = np.asarray(direction, np.float64)
    d /= np.linalg.norm(d)
    cam = primary.Camera(centre + d * (pixel_m / k), -d, np.array([0.0, 1.0, 0.0]), 0.1, (2, 2))
    r, u, f = cam.basis()
    rel = pts - cam.position
    z = rel @ f
    x, y = rel @ r / (z * k), rel @ u / (z * k)
    w = 2 * int(np.ceil(np.abs(x).max() + 6))
    h = 2 * int(np.ceil(np.abs(y).max() + 6))
    w, h = int(w * scale), int(h * scale)
    cam.size = (w, h)
    cam.fov_y = 2 * np.arctan(k / scale * h / 2)
    return cam


# ---- strand rasteriser -----------------------------------------------------------------------------------------------
class Strands:
    """Projected segments of a groom for one camera (jitter-independent)."""

    def __init__(self, pts, rad, off, ids, cam):
        s0, s1 = primary.segments(pts, rad, off)
        x, y, z = cam.project(pts)
        k = np.tan(cam.fov_y / 2) / (cam.size[1] / 2)
        rp = rad / (z * k)
        self.a = np.stack([x[s0], y[s0], z[s0], rp[s0]], 1)
        self.b = np.stack([x[s1], y[s1], z[s1], rp[s1]], 1)
        self.k = k
        self.size = cam.size
        strand = np.searchsorted(off, s0, side="right") - 1
        self.strand = ids[strand]
        r, u, _ = cam.basis()
        t = pts[s1] - pts[s0]
        tx, ty = t @ r, t @ u
        ang = 2 * np.arctan2(ty, tx)
        self.c2, self.s2 = np.cos(ang), np.sin(ang)
        self.width_px = float(np.median(2 * rp))


def raster(st, jitter=(0.0, 0.0), piece=1.0, chunk=150_000):
    """Nearest strand at every pixel's sample point (centre + jitter): (segment index or -1 (h, w), depth)."""
    w, h = st.size
    best_z = np.full(w * h, np.inf)
    best_s = np.full(w * h, -1, np.int64)
    jx, jy = float(jitter[0]) + 0.5, float(jitter[1]) + 0.5
    for c0 in range(0, len(st.a), chunk):
        a, b = st.a[c0:c0 + chunk], st.b[c0:c0 + chunk]
        n = np.maximum(1, np.ceil(np.hypot(b[:, 0] - a[:, 0], b[:, 1] - a[:, 1]) / piece)).astype(np.int64)
        seg = np.repeat(np.arange(len(a)), n)
        j = np.arange(n.sum()) - np.repeat(np.cumsum(n) - n, n)
        t0, t1 = (j / n[seg])[:, None], ((j + 1) / n[seg])[:, None]
        pa = a[seg] + (b[seg] - a[seg]) * t0
        pb = a[seg] + (b[seg] - a[seg]) * t1
        r = np.maximum(pa[:, 3], pb[:, 3])
        lo_x = np.ceil(np.minimum(pa[:, 0], pb[:, 0]) - r - jx).astype(np.int64).clip(0, w)
        hi_x = (np.floor(np.maximum(pa[:, 0], pb[:, 0]) + r - jx).astype(np.int64) + 1).clip(0, w)
        lo_y = np.ceil(np.minimum(pa[:, 1], pb[:, 1]) - r - jy).astype(np.int64).clip(0, h)
        hi_y = (np.floor(np.maximum(pa[:, 1], pb[:, 1]) + r - jy).astype(np.int64) + 1).clip(0, h)
        nx, ny = np.maximum(hi_x - lo_x, 0), np.maximum(hi_y - lo_y, 0)
        cnt = nx * ny
        live = np.flatnonzero(cnt)
        if not len(live):
            continue
        c = cnt[live]
        pi = np.repeat(live, c)
        local = np.arange(c.sum()) - np.repeat(np.cumsum(c) - c, c)
        px = lo_x[pi] + local % nx[pi]
        py = lo_y[pi] + local // nx[pi]
        sx, sy = px + jx, py + jy
        ax, ay, bx, by = pa[pi, 0], pa[pi, 1], pb[pi, 0], pb[pi, 1]
        ex, ey = bx - ax, by - ay
        u = np.clip(((sx - ax) * ex + (sy - ay) * ey) / np.maximum(ex * ex + ey * ey, 1e-20), 0.0, 1.0)
        dx, dy = sx - (ax + ex * u), sy - (ay + ey * u)
        rr = pa[pi, 3] + (pb[pi, 3] - pa[pi, 3]) * u
        d2 = dx * dx + dy * dy
        m = d2 <= rr * rr
        if not m.any():
            continue
        pi, u, d2, rr = pi[m], u[m], d2[m], rr[m]
        z = pa[pi, 2] + (pb[pi, 2] - pa[pi, 2]) * u
        z = z - np.sqrt(np.maximum(rr * rr - d2, 0.0)) * z * st.k      # the front of the swept sphere
        pix = (py[m] * w + px[m])
        order = np.lexsort((z, pix))
        pix, z, sg = pix[order], z[order], (c0 + seg[pi])[order]
        first = np.r_[True, pix[1:] != pix[:-1]]
        pix, z, sg = pix[first], z[first], sg[first]
        better = z < best_z[pix]
        best_z[pix[better]], best_s[pix[better]] = z[better], sg[better]
    return best_s.reshape(h, w), best_z.reshape(h, w)


def shade_of(st, seg):
    """The scalar signal of a strand image: 0.35 + 0.65 x per-strand value on hits, 0 elsewhere."""
    return np.where(seg >= 0, 0.35 + 0.65 * strand_value(st.strand[np.maximum(seg, 0)]), 0.0)


# ---- card rasteriser -------------------------------------------------------------------------------------------------
class Cards:
    def __init__(self, mesh, mask, cam):
        x, y, z = cam.project(mesh.positions.astype(np.float64))
        t = mesh.tris
        self.xy = np.stack([x[t], y[t]], -1)             # (T, 3, 2)
        self.iz = 1.0 / z[t]                             # (T, 3)
        th, tw = mask.shape[:2]
        self.uv = mesh.uv[t].astype(np.float64) * [tw, th]   # texels of level 0
        e1, e2 = self.xy[:, 1] - self.xy[:, 0], self.xy[:, 2] - self.xy[:, 0]
        self.area = e1[:, 0] * e2[:, 1] - e1[:, 1] * e2[:, 0]
        u1, u2 = self.uv[:, 1] - self.uv[:, 0], self.uv[:, 2] - self.uv[:, 0]
        texels = np.abs(u1[:, 0] * u2[:, 1] - u1[:, 1] * u2[:, 0])
        with np.errstate(divide="ignore", invalid="ignore"):
            self.lod = 0.5 * np.log2(np.maximum(texels, 1e-12) / np.maximum(np.abs(self.area), 1e-12))
        self.size = cam.size
        self.alpha = mips(mask[..., 3] / 255.0)
        self.ident = mips(mask[..., 0] / 255.0)


def mips(x):
    out = [np.asarray(x, np.float64)]
    while min(out[-1].shape) > 1:
        m = out[-1]
        out.append(m.reshape(m.shape[0] // 2, 2, m.shape[1] // 2, 2).mean((1, 3)))
    return out


def tex(levels, u, v, lod):
    """Trilinear sample (clamped addressing; u, v in level 0 texels; lod per sample)."""
    lod = np.clip(lod, 0.0, len(levels) - 1.0)
    lo = np.floor(lod).astype(np.int64)
    f = lod - lo
    out = np.zeros(len(u))
    for level in np.unique(lo):
        sel = lo == level
        acc = 0.0
        for lv, wgt in ((level, 1 - f[sel]), (min(level + 1, len(levels) - 1), f[sel])):
            m = levels[lv]
            hh, ww = m.shape
            x, y = u[sel] / 2.0 ** lv - 0.5, v[sel] / 2.0 ** lv - 0.5
            x0, y0 = np.floor(x).astype(np.int64), np.floor(y).astype(np.int64)
            fx, fy = x - x0, y - y0
            at = lambda i, j: m[np.clip(j, 0, hh - 1), np.clip(i, 0, ww - 1)]
            acc = acc + wgt * ((at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx) * (1 - fy) +
                               (at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx) * fy)
        out[sel] = acc
    return out


def raster_cards(cd, jitter, dither, lod_bias=0.0, chunk=400):
    """The nearest card fragment that passes the game's dithered alpha test (alpha + dither >= 1; `dither` (h x w,) in
    [0, 1)) at every pixel's sample point: (hit (h, w), shade = 0.35 + 0.65 x ID channel)."""
    w, h = cd.size
    jx, jy = float(jitter[0]) + 0.5, float(jitter[1]) + 0.5
    best_z = np.full(w * h, np.inf)
    best_v = np.zeros(w * h)
    for c0 in range(0, len(cd.xy), chunk):
        xy = cd.xy[c0:c0 + chunk]
        lo_x = np.ceil(xy[..., 0].min(1) - jx).astype(np.int64).clip(0, w)
        hi_x = (np.floor(xy[..., 0].max(1) - jx).astype(np.int64) + 1).clip(0, w)
        lo_y = np.ceil(xy[..., 1].min(1) - jy).astype(np.int64).clip(0, h)
        hi_y = (np.floor(xy[..., 1].max(1) - jy).astype(np.int64) + 1).clip(0, h)
        nx, ny = np.maximum(hi_x - lo_x, 0), np.maximum(hi_y - lo_y, 0)
        cnt = nx * ny
        live = np.flatnonzero((cnt > 0) & (np.abs(cd.area[c0:c0 + chunk]) > 1e-12))
        if not len(live):
            continue
        c = cnt[live]
        ti = np.repeat(live, c)
        local = np.arange(c.sum()) - np.repeat(np.cumsum(c) - c, c)
        px, py = lo_x[ti] + local % nx[ti], lo_y[ti] + local // nx[ti]
        sx, sy = px + jx, py + jy
        p = xy[ti]
        area = cd.area[c0 + ti]
        b1 = ((sx - p[:, 0, 0]) * (p[:, 2, 1] - p[:, 0, 1]) - (sy - p[:, 0, 1]) * (p[:, 2, 0] - p[:, 0, 0])) / area
        b2 = ((p[:, 1, 0] - p[:, 0, 0]) * (sy - p[:, 0, 1]) - (p[:, 1, 1] - p[:, 0, 1]) * (sx - p[:, 0, 0])) / area
        b0 = 1 - b1 - b2
        m = (b0 >= 0) & (b1 >= 0) & (b2 >= 0)
        if not m.any():
            continue
        ti, b0, b1, b2, pix = ti[m], b0[m], b1[m], b2[m], (py[m] * w + px[m])
        iz = cd.iz[c0 + ti]
        q = np.stack([b0 * iz[:, 0], b1 * iz[:, 1], b2 * iz[:, 2]], 1)
        z = 1.0 / q.sum(1)
        q *= z[:, None]
        uv = (cd.uv[c0 + ti] * q[:, :, None]).sum(1)
        lod = cd.lod[c0 + ti] + lod_bias
        keep = tex(cd.alpha, uv[:, 0], uv[:, 1], lod) + dither[pix] >= 1.0
        if not keep.any():
            continue
        pix, z, uv, lod = pix[keep], z[keep], uv[keep], lod[keep]
        order = np.lexsort((z, pix))
        pix, z, uv, lod = pix[order], z[order], uv[order], lod[order]
        first = np.r_[True, pix[1:] != pix[:-1]]
        pix, z, uv, lod = pix[first], z[first], uv[first], lod[first]
        better = z < best_z[pix]
        pix, z, uv, lod = pix[better], z[better], uv[better], lod[better]
        best_z[pix] = z
        best_v[pix] = 0.35 + 0.65 * tex(cd.ident, uv[:, 0], uv[:, 1], lod)
    hit = np.isfinite(best_z)
    return hit.reshape(h, w), np.where(hit, best_v, 0.0).reshape(h, w)


# ---- accumulation and metrics ----------------------------------------------------------------------------------------
def splat(acc, wsum, img, jitter):
    """Bilinear splat of a render-resolution frame's samples (centre + jitter) into the 1.5 x output grid."""
    h, w = img.shape
    oh, ow = acc.shape
    x = (np.arange(w) + 0.5 + jitter[0]) * UPSCALE - 0.5
    y = (np.arange(h) + 0.5 + jitter[1]) * UPSCALE - 0.5
    x0, y0 = np.floor(x).astype(np.int64), np.floor(y).astype(np.int64)
    fx, fy = x - x0, y - y0
    for dy, wy in ((0, 1 - fy), (1, fy)):
        for dx, wx in ((0, 1 - fx), (1, fx)):
            yy, xx = y0 + dy, x0 + dx
            oky, okx = (yy >= 0) & (yy < oh), (xx >= 0) & (xx < ow)
            wgt = wy[oky][:, None] * wx[okx][None, :]
            np.add.at(acc, (yy[oky][:, None], xx[okx][None, :]), wgt * img[oky][:, okx])
            np.add.at(wsum, (yy[oky][:, None], xx[okx][None, :]), wgt)


def down(x, f):
    return x.reshape(x.shape[0] // f, f, x.shape[1] // f, f).mean((1, 3))


def metrics(hits, shades, ids, c2, s2, jit, ref_cov3, ref_shade3):
    """hits / shades / ids / c2 / s2: (frames, h, w) of the case at render resolution; ref_*3: the 3 x reference."""
    f = len(hits)
    ref_r, ref_o = down(ref_cov3, 3), down(ref_cov3, 2)
    shade_o = down(ref_shade3, 2)
    roi_r = ref_r > 0
    edge_r = roi_r & (ref_r < BULK)
    bulk_r = grain_stages.erode(ref_r >= BULK, 1)
    roi_o = grain_stages.dilate(ref_o > 0, 1)
    bulk_o = grain_stages.erode(ref_o >= BULK, 2)
    hf = hits.astype(np.float64)
    out = {"frames": f, "pixels_roi": int(roi_r.sum()), "pixels_bulk": int(bulk_r.sum())}
    # (a) temporal behaviour under the jitter
    cstd = hf.std(0)
    out["cov_tstd_roi"] = float(cstd[roi_r].mean())
    out["cov_tstd_edge"] = float(cstd[edge_r].mean()) if edge_r.any() else float("nan")
    out["cov_tstd_bulk"] = float(cstd[bulk_r].mean()) if bulk_r.any() else float("nan")
    out["shade_tstd_roi"] = float(shades.std(0)[roi_r].mean())
    both = hits[1:] & hits[:-1]
    out["id_change"] = float(((ids[1:] != ids[:-1]) & both).sum() / max(both.sum(), 1))
    srt = np.sort(np.where(hits, ids, -1), 0)
    distinct = (np.diff(srt, axis=0) != 0).sum(0) + 1 - (~hits).any(0)
    seen = hits.sum(0) >= 8
    out["ids_per_pixel"] = float(distinct[seen & roi_r].mean()) if (seen & roi_r).any() else float("nan")
    n = np.maximum(hits.sum(0), 1)
    res = np.hypot((c2 * hits).sum(0) / n, (s2 * hits).sum(0) / n)
    circ = np.degrees(0.5 * np.sqrt(-2 * np.log(np.clip(res, 1e-9, 1.0))))
    out["tangent_tstd_deg"] = float(circ[seen & roi_r].mean()) if (seen & roi_r).any() else float("nan")
    # (b), (c) the accumulated image against the reference at output resolution
    acc_s, acc_c, wsum = np.zeros(ref_o.shape), np.zeros(ref_o.shape), np.zeros(ref_o.shape)
    w2 = np.zeros(ref_o.shape)
    for i in range(f):
        splat(acc_s, wsum, shades[i], jit[i])
        splat(acc_c, w2, hf[i], jit[i])
    acc_s, acc_c = acc_s / np.maximum(wsum, 1e-9), acc_c / np.maximum(w2, 1e-9)
    mean_ref = float(shade_o[roi_o].mean())
    out["rms_shade"] = float(np.sqrt(((acc_s - shade_o) ** 2)[roi_o].mean()) / mean_ref)
    out["rms_cov"] = float(np.sqrt(((acc_c - ref_o) ** 2)[roi_o].mean()))
    out["coherence"] = grain_stages.coherence(acc_s, bulk_o)
    out["hf"] = grain_stages.hf_scalar(acc_s, bulk_o)
    out["frame_hf"] = float(np.mean([grain_stages.hf_scalar(shades[i], bulk_r) for i in range(min(f, 4))]))
    out["cov_bias"] = float(acc_c[roi_o].sum() / ref_o[roi_o].sum() - 1)
    out["silhouette_growth"] = float((acc_c > 0.5).sum() / max((ref_o > 0.5).sum(), 1) - 1)
    out["bulk_cov"] = float(acc_c[bulk_o].mean()) if bulk_o.any() else float("nan")
    out["bulk_energy"] = float(acc_s[bulk_o].mean() / shade_o[bulk_o].mean() - 1) if bulk_o.any() else float("nan")
    return out, acc_s, acc_c


def reference_strands(pts, rad, off, cam3):
    st = Strands(pts, rad, off, np.arange(len(off) - 1), cam3)
    cov, sh = np.zeros(cam3.size[::-1]), np.zeros(cam3.size[::-1])
    for i in range(REF_N):
        for j in range(REF_N):
            seg, _ = raster(st, ((i + 0.5) / REF_N - 0.5, (j + 0.5) / REF_N - 0.5), piece=1.5)
            cov += seg >= 0
            sh += shade_of(st, seg)
            log(f"reference ray {i * REF_N + j + 1}/{REF_N * REF_N}", force=False)
    return cov / REF_N ** 2, sh / REF_N ** 2


def run_case(pts, rad, off, keep, cam, frames, scale=1.0):
    p, r, o, ids, capped = variant(pts, rad, off, keep, scale)
    st = Strands(p, r, o, ids, cam)
    h, w = cam.size[::-1]
    hits, shades = np.zeros((frames, h, w), bool), np.zeros((frames, h, w))
    sid = np.full((frames, h, w), -1, np.int64)
    c2, s2 = np.zeros((frames, h, w)), np.zeros((frames, h, w))
    for f in range(frames):
        seg, _ = raster(st, primary.jitter(f))
        hit = seg >= 0
        g = np.maximum(seg, 0)
        hits[f], shades[f] = hit, shade_of(st, seg)
        sid[f] = np.where(hit, st.strand[g], -1)
        c2[f], s2[f] = np.where(hit, st.c2[g], 0), np.where(hit, st.s2[g], 0)
        log(f"keep {keep:.4f} frame {f + 1}/{frames}", force=False)
    return hits, shades, sid, c2, s2, st.width_px, capped, len(o) - 1


def native_dither(pixel, tag, jitter):
    """The dither of hp_tips_keep / primary.tips_keep (which keeps alpha + dither - 0.6667 >= 0.3333): the game's
    ordered pattern, four frame phases, plus the per-draw jitter term (not known offline: one uniform value per frame)."""
    frac = lambda x: x - np.floor(x)
    p0 = frac((pixel[:, 1] * 2 + pixel[:, 0] - 1.5 + ((tag - 1) & 3)) * 0.2)
    p1 = frac(pixel[:, 0] * 2.4084 + pixel[:, 1] * 3.2535)
    return frac(0.833 * p0 + 0.1666 * p1 + jitter)


def run_cards(mesh, mask, cam, cam3, frames, seed=7):
    rng = np.random.default_rng(seed)
    c3 = Cards(mesh, mask, cam3)
    cov, sh = np.zeros(cam3.size[::-1]), np.zeros(cam3.size[::-1])
    for i in range(REF_N):
        for j in range(REF_N):
            hit, s = raster_cards(c3, ((i + 0.5) / REF_N - 0.5, (j + 0.5) / REF_N - 0.5), rng.random(c3.size[0] * c3.size[1]), lod_bias=-99.0)
            cov += hit
            sh += s
            log(f"card reference ray {i * REF_N + j + 1}/{REF_N * REF_N}", force=False)
    cd = Cards(mesh, mask, cam)
    h, w = cam.size[::-1]
    hits, shades = np.zeros((frames, h, w), bool), np.zeros((frames, h, w))
    py, px = np.mgrid[0:h, 0:w]
    centre = np.stack([px + 0.5, py + 0.5], -1).reshape(-1, 2)
    for f in range(frames):
        hits[f], shades[f] = raster_cards(cd, primary.jitter(f), native_dither(centre, f + 1, rng.random()))
    return hits, shades, cov / REF_N ** 2, sh / REF_N ** 2


def view_direction(mesh_positions, pts):
    """Towards the camera: the side of the head the groom is on (its centre against the card mesh's widest extent is
    not known, so: the horizontal direction in which the groom is thinnest, on the side where it has more points),
    raised 35 degrees."""
    c = pts - pts.mean(0)
    hx = c[:, [0, 2]]
    _, vec = np.linalg.eigh(np.cov(hx.T))
    d = vec[:, 0]
    if np.median(hx @ d) < 0:
        d = -d
    e = np.radians(35.0)
    return np.array([d[0] * np.cos(e), np.sin(e), d[1] * np.cos(e)])


def study(args):
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    mesh, mask, pts, rad, off = load(args.hcap, args.hgrm)
    diag = float(np.linalg.norm(pts.max(0) - pts.min(0)))
    d0 = 2 * float(np.median(rad))
    log(f"groom: {len(off) - 1} strands, {len(pts)} points, median diameter {d0 * 1e3:.3f} mm, bounds diagonal {diag:.3f} m")
    direction = view_direction(mesh.positions, pts)
    k = pixel_angle()
    jit = [primary.jitter(f) for f in range(args.frames)]
    report = {"hcap": Path(args.hcap).name, "hgrm": Path(args.hgrm).name, "strands": int(len(off) - 1),
              "median_diameter_mm": d0 * 1e3, "bounds_diagonal_m": diag, "frames": args.frames, "heads": {}}
    for head in args.heads:
        pixel = HEAD_M / head
        dist = pixel / k
        size = diag / dist
        today = 0 if size > ANGLE_LOD[0] else 1 if size > ANGLE_LOD[1] else 2
        cam = crop_camera(pts, pixel, direction)
        cam3 = crop_camera(pts, pixel, direction, scale=REF_SCALE)
        log(f"head {head} px: pixel {pixel * 1e3:.2f} mm, distance {dist:.2f} m, angle {size:.3f} -> LOD {today}, crop {cam.size}")
        ref_cov, ref_shade = reference_strands(pts, rad, off, cam3)
        cases = [(f"lod{i}", KEEPS[i]) for i in range(3)] + [(f"k{kk}", min(1.0, d0 / (kk * pixel))) for kk in (0.7, 1.0, 1.4)]
        rows, accs = {}, {}
        for name, keep in cases:
            hits, shades, sid, c2, s2, width, capped, strands = run_case(pts, rad, off, keep, cam, args.frames)
            m, acc_s, acc_c = metrics(hits, shades, sid, c2, s2, jit, ref_cov, ref_shade)
            m.update(keep=keep, width_px=width, capped_share=capped, strands=strands, today=name == f"lod{today}")
            rows[name], accs[name] = m, acc_s
            log(f"  {name}: keep {keep:.4f} width {width:.2f} px cov_tstd {m['cov_tstd_roi']:.3f} id_change {m['id_change']:.3f} "
                f"rms {m['rms_shade']:.3f} coh {m['coherence']:.3f} hf {m['hf']:.3f} bias {m['cov_bias']:+.3f}")
        roi = grain_stages.dilate(down(ref_cov, 2) > 0, 1)
        mean_ref = down(ref_shade, 2)[roi].mean()
        pops = {f"{a}->{b}": float(np.sqrt(((accs[a] - accs[b]) ** 2)[roi].mean()) / mean_ref)
                for a, b in (("lod0", "lod1"), ("lod1", "lod2"), ("k0.7", "k1.4"), ("k0.7", "k1.0"))}
        bulk_o = grain_stages.erode(down(ref_cov, 2) >= BULK, 2)
        ref_row = {"coherence": grain_stages.coherence(down(ref_shade, 2), bulk_o), "hf": grain_stages.hf_scalar(down(ref_shade, 2), bulk_o)}
        card = None
        if not args.no_cards:
            ch, cs, ccov, csh = run_cards(mesh, mask, cam, cam3, args.frames)
            z = np.zeros(ch.shape)
            card, _, _ = metrics(ch, cs, np.where(ch, 0, -1), z, z, jit, ccov, csh)
            for key in ("id_change", "ids_per_pixel", "tangent_tstd_deg"):
                card.pop(key)
            cb = grain_stages.erode(down(ccov, 2) >= BULK, 2)
            card["reference_coherence"] = grain_stages.coherence(down(csh, 2), cb)
            card["reference_hf"] = grain_stages.hf_scalar(down(csh, 2), cb)
            card["mean_alpha_coverage"] = float(ccov.sum() / max(ref_cov.sum(), 1e-9))
            log(f"  cards: cov_tstd {card['cov_tstd_roi']:.3f} rms {card['rms_shade']:.3f} coh {card['coherence']:.3f} hf {card['hf']:.3f}")
        report["heads"][str(head)] = {"pixel_mm": pixel * 1e3, "distance_m": dist, "angle": size, "today": f"lod{today}",
                                      "crop": list(cam.size), "cases": rows, "pop_rms": pops, "reference": ref_row, "cards": card}
        (out / "report.json").write_text(json.dumps(report, indent=1) + "\n", encoding="utf-8")
    if args.shadow:
        report["shadow"] = shadow_study(pts, rad, off, d0, args.heads)
        (out / "report.json").write_text(json.dumps(report, indent=1) + "\n", encoding="utf-8")
    log("done")
    return report


def shadow_study(pts, rad, off, d0, heads, pixel=0.001, n=4):
    """Secondary rays on the decimated groom: light-view visibility (parallel rays from 45 degrees above the side, 1 mm
    grid, n x n rays per cell) of every keep against LOD 0: the mean blocked share, and the RMS difference of the
    blocked share over 3 mm and 11 mm boxes (what a shadow denoiser's footprint sees)."""
    direction = np.array([0.6, 0.7, 0.4])
    cam = crop_camera(pts, pixel, direction, k=pixel / 200.0)
    keeps = {"lod0": 1.0, "lod1": 0.5, "lod2": 0.25, "1/8": 0.125, "1/16": 0.0625, "1/32": 0.03125}
    for head in heads:
        for kk in (0.7, 1.0, 1.4):
            keeps[f"head{head}_k{kk}"] = min(1.0, d0 / (kk * HEAD_M / head))
    out, base = {}, None
    for name, keep in keeps.items():
        p, r, o, ids, capped = variant(pts, rad, off, keep)
        st = Strands(p, r, o, ids, cam)
        cov = np.zeros(cam.size[::-1])
        for i in range(n):
            for j in range(n):
                cov += raster(st, ((i + 0.5) / n - 0.5, (j + 0.5) / n - 0.5))[0] >= 0
                log(f"shadow {name} ray {i * n + j + 1}/{n * n}", force=False)
        cov /= n * n
        if base is None:
            base = cov
        roi = primary.box_mean(base, 5) > 0
        row = {"keep": keep, "capped_share": capped, "blocked_mean": float(cov[roi].mean()),
               "blocked_ratio": float(cov.sum() / base.sum())}
        for label, rr in (("rms_3mm", 1), ("rms_11mm", 5)):
            row[label] = float(np.sqrt(((primary.box_mean(cov, rr) - primary.box_mean(base, rr)) ** 2)[roi].mean()))
        thin = roi & (primary.box_mean(base, 5) < 0.9)
        row["thin_blocked_ratio"] = float(cov[thin].sum() / max(base[thin].sum(), 1e-9))
        out[name] = row
        log(f"  shadow {name}: keep {keep:.4f} blocked ratio {row['blocked_ratio']:.4f} thin {row['thin_blocked_ratio']:.4f} "
            f"rms 3 mm {row['rms_3mm']:.4f} 11 mm {row['rms_11mm']:.4f}")
    return out


# ---- ladder sweep (branch claude/hair-pixel-lod-small) -------------------------------------------------------------------
MIN_KEEP = 1.0 / 32      # common/hair_groom_budget.h kPixelLodMinKeep: the decode-time ladder's last keep
GAIN_KEYS = ("cov_tstd_roi", "hf", "frame_hf")
# Gated errors. rms_cov (per-pixel accumulated coverage against the smooth truth) is reported, not gated: it rises with
# every HF gain by construction (the strand structure the case adds is a deviation from the smooth average).
ERR_KEYS = ("cov_bias", "silhouette_growth", "bulk_energy")
NOTE_KEYS = ("rms_cov",)


def halves(hits, shades, sid, c2, s2, jit, ref_cov, ref_shade):
    """The case's metrics on the first and the second half of the frames: their difference is each metric's sampling
    noise (an accumulation of half the frames)."""
    h = len(hits) // 2
    a, _, _ = metrics(hits[:h], shades[:h], sid[:h], c2[:h], s2[:h], jit[:h], ref_cov, ref_shade)
    b, _, _ = metrics(hits[h:], shades[h:], sid[h:], c2[h:], s2[h:], jit[h:], ref_cov, ref_shade)
    return {k: abs(a[k] - b[k]) for k in GAIN_KEYS + ERR_KEYS + NOTE_KEYS}


def ladder(args):
    """Heads below the 60 px floor: every camera level the layer can trace (today's angle LOD, then keep / 2^j down to
    1/32 with radii x 2^j, hair_groom::camera_subset), the continuous widths k (keep = d0 / (k x pixel), no 1/32 floor),
    and per level a coverage-conserving case (radii x c, c fitted so the accumulated coverage equals the truth's).
    Thresholds per head, derived from today's image: an error metric may exceed today's by at most
    max(today's LOD step (| |metric(LOD today +- 1)| - |metric(LOD today)| |), the split-half noise of today's and of the
    case's metric); a gain is a coverage noise or HF change beyond the split-half noise. Pass: no error fails, one gain."""
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    mesh, mask, pts, rad, off = load(args.hcap, args.hgrm)
    diag = float(np.linalg.norm(pts.max(0) - pts.min(0)))
    d0 = 2 * float(np.median(rad))
    direction = view_direction(mesh.positions, pts)
    k = pixel_angle()
    jit = [primary.jitter(f) for f in range(args.frames)]
    report = {"hcap": Path(args.hcap).name, "hgrm": Path(args.hgrm).name, "strands": int(len(off) - 1),
              "median_diameter_mm": d0 * 1e3, "bounds_diagonal_m": diag, "frames": args.frames, "heads": {}}
    for head in args.heads:
        pixel = HEAD_M / head
        dist = pixel / k
        today = 0 if diag / dist > ANGLE_LOD[0] else 1 if diag / dist > ANGLE_LOD[1] else 2
        cam = crop_camera(pts, pixel, direction)
        cam3 = crop_camera(pts, pixel, direction, scale=REF_SCALE)
        d0_px = d0 / pixel
        log(f"head {head} px: LOD {today}, d0 {d0_px:.4f} px, crop {cam.size}")
        ref_cov, ref_shade = reference_strands(pts, rad, off, cam3)
        cases = [(f"lod{i}", KEEPS[i], 1.0) for i in range(3)]
        j = 1
        while KEEPS[today] / 2 ** j >= MIN_KEEP * 0.999:
            cases.append((f"level{j}", KEEPS[today] / 2 ** j, 1.0))
            j += 1
        cases += [(f"k{kk}", min(1.0, d0_px / kk), 1.0) for kk in args.ks]
        rows, noise = {}, {}

        def evaluate(name, keep, scale, frames=args.frames):
            hits, shades, sid, c2, s2, width, capped, strands = run_case(pts, rad, off, keep, cam, frames, scale)
            m, _, _ = metrics(hits, shades, sid, c2, s2, jit[:frames], ref_cov, ref_shade)
            m.update(keep=keep, scale=scale, width_px=width, capped_share=capped, strands=strands)
            if frames == args.frames:
                noise[name] = halves(hits, shades, sid, c2, s2, jit, ref_cov, ref_shade)
            return m

        for name, keep, scale in cases:
            rows[name] = evaluate(name, keep, scale)
            m = rows[name]
            log(f"  {name}: keep {keep:.4f} width {m['width_px']:.3f} px cov_tstd {m['cov_tstd_roi']:.3f} hf {m['hf']:.3f} "
                f"bias {m['cov_bias']:+.3f} sil {m['silhouette_growth']:+.3f} rms_cov {m['rms_cov']:.3f}")
        for jj in range(1, j):   # coverage-conserving radius per level: secant on the 8-frame coverage bias, then all frames
            name, keep = f"level{jj}", KEEPS[today] / 2 ** jj
            b1 = rows[name]["cov_bias"]
            b2 = evaluate(name, keep, 0.6, 8)["cov_bias"]
            c = float(np.clip(1.0 - b1 * 0.4 / (b1 - b2), 0.3, 1.0)) if b1 != b2 else 1.0
            rows[f"{name}c"] = evaluate(f"{name}c", keep, c)
            m = rows[f"{name}c"]
            log(f"  {name}c: c {c:.3f} width {m['width_px']:.3f} px cov_tstd {m['cov_tstd_roi']:.3f} hf {m['hf']:.3f} "
                f"bias {m['cov_bias']:+.3f} sil {m['silhouette_growth']:+.3f} rms_cov {m['rms_cov']:.3f}")
        t = rows[f"lod{today}"]
        steps = [rows[f"lod{i}"] for i in (today - 1, today + 1) if 0 <= i <= 2]
        verdict = {}
        for name, m in rows.items():
            if name.startswith("lod"):
                continue
            v = {"width_px": m["width_px"], "fail": [], "gain": []}
            for key in ERR_KEYS:
                step = max(abs(abs(s[key]) - abs(t[key])) for s in steps)
                tol = max(step, noise[f"lod{today}"][key], noise[name][key])
                v[f"{key}_excess"] = abs(m[key]) - abs(t[key])
                v[f"{key}_tol"] = tol
                if abs(m[key]) - abs(t[key]) > tol:
                    v["fail"].append(key)
            for key, sign in (("cov_tstd_roi", -1), ("hf", 1), ("frame_hf", 1)):
                tol = max(noise[f"lod{today}"][key], noise[name][key])
                if sign * (m[key] - t[key]) > tol:
                    v["gain"].append(key)
            v["pass"] = not v["fail"] and bool(v["gain"])
            verdict[name] = v
            log(f"  verdict {name}: width {m['width_px']:.3f} {'PASS' if v['pass'] else 'fail'} fail {v['fail']} gain {v['gain']}")
        report["heads"][str(head)] = {"pixel_mm": pixel * 1e3, "d0_px": d0_px, "today": f"lod{today}", "crop": list(cam.size),
                                      "cases": rows, "noise": noise, "verdict": verdict}
        (out / "ladder.json").write_text(json.dumps(report, indent=1) + "\n", encoding="utf-8")
    log("done")
    return report


def selfcheck(args):
    """The screen-space rasteriser against primary.trace's ray-capsule tracer: one frame of every eighth strand at
    80 pixels. The two differ in the radius along a segment (linear here, the mean there) and in perspective terms of
    the order of radius / distance."""
    _, _, pts, rad, off = load(args.hcap, args.hgrm)
    sel = np.zeros(len(off) - 1, bool)
    sel[::8] = True
    lengths = np.diff(off)
    p = np.repeat(sel, lengths)
    pts2, rad2, off2 = pts[p], np.minimum(rad[p] * 8, storage.MAX_THIN_RADIUS), np.concatenate([[0], np.cumsum(lengths[sel])])
    cam = crop_camera(pts, HEAD_M / 80, view_direction(None, pts))
    j = primary.jitter(3)
    a = primary.trace(pts2, rad2, off2, cam, j).hit
    b = raster(Strands(pts2, rad2, off2, np.arange(len(off2) - 1), cam), j)[0] >= 0
    agree = float((a == b).mean())
    iou = float((a & b).sum() / max((a | b).sum(), 1))
    print(json.dumps({"pixels": int(a.size), "trace_hits": int(a.sum()), "raster_hits": int(b.sum()), "agree": agree, "iou": iou}))
    return iou


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--hcap", required=True)
    ap.add_argument("--hgrm", required=True)
    ap.add_argument("--out", default=None)
    ap.add_argument("--heads", type=int, nargs="+", default=[40, 80, 150])
    ap.add_argument("--frames", type=int, default=32)
    ap.add_argument("--shadow", action="store_true")
    ap.add_argument("--no-cards", action="store_true")
    ap.add_argument("--selfcheck", action="store_true")
    ap.add_argument("--ladder", action="store_true", help="the camera-level sweep for small heads (ladder.json)")
    ap.add_argument("--ks", type=float, nargs="+", default=[0.5, 0.6, 0.7, 0.8])
    args = ap.parse_args(argv)
    if args.selfcheck:
        return 0 if selfcheck(args) > 0.9 else 1
    if not args.out:
        ap.error("--out is required")
    ladder(args) if args.ladder else study(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
