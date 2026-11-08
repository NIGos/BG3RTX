"""Dual scattering for the fibre BSDF (Zinke, Yuksel, Weber, Keyser 2008, "Dual Scattering Approximation for Fast
Multiple Scattering in Hair"), written from the paper on top of tools/hair_groom/bsdf.py (docs/RTX_HAIR_PRIMARY.md 3b).

Single scattering alone leaves Chiang hair pale (each fibre transmits most of the light forward); the colour of a
hair volume comes from light scattered by many fibres. Dual scattering splits that into
  global: light reaching the shaded fibre through n fibres in front of it (along the shadow ray): forward
          transmittance T_f = d_f * a_f^n and a longitudinal spread sigma_f^2 = n * beta_f^2;
  local:  light scattered back to the viewer by the fibres around the shaded one: A_b with shift/spread.
Fibre averages a_f, a_b (forward/backward scattered fraction), alpha_f/b (mean longitudinal shift) and beta_f/b
(spread) come from Monte Carlo integration of bsdf.evaluate, so they follow the colour and roughness.
F(n) = f_s + d_b f_back for a directly lit fibre (n = 0), T_f (f_s + pi d_b f_back) otherwise.
d_f = d_b = 0.7 (the paper's density factors).
"""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from . import bsdf

D_F = 0.7
D_B = 0.7


@dataclass
class Averages:
    a_f: np.ndarray      # forward scattered fraction per RGB (|phi| > pi/2)
    a_b: np.ndarray      # backward scattered fraction
    alpha_f: float       # mean longitudinal shift theta_o + theta_i of forward scattering (rad)
    alpha_b: float
    beta_f: float        # longitudinal spread (std, rad) of forward scattering
    beta_b: float


def averages(color, roughness, n=200000, seed=3):
    """Fibre averages over random incident directions and offsets (fixed tangent +z)."""
    rng = np.random.default_rng(seed)
    t = np.tile([0.0, 0.0, 1.0], (n, 1))
    wo, wi = bsdf._sphere(rng, n), bsdf._sphere(rng, n)
    h = rng.uniform(-1, 1, n)
    S = bsdf.evaluate(t, wo, wi, h, color, roughness) * 4 * np.pi
    # phi = 0: wo on the light's side of the fibre (backward); |phi| > pi/2: through the fibre (forward).
    po, pi_ = wo[:, :2], wi[:, :2]
    cos_phi = (po * pi_).sum(1) / np.maximum(np.linalg.norm(po, axis=1) * np.linalg.norm(pi_, axis=1), 1e-9)
    fwd = cos_phi < 0
    a_f = (S * fwd[:, None]).mean(0)
    a_b = (S * ~fwd[:, None]).mean(0)
    dev = np.arcsin(np.clip(wo[:, 2], -1, 1)) + np.arcsin(np.clip(wi[:, 2], -1, 1))   # theta_o + theta_i
    w = S @ np.array([0.2126, 0.7152, 0.0722])

    def moments(mask):
        ww = w * mask
        m = (ww * dev).sum() / max(ww.sum(), 1e-12)
        return float(m), float(np.sqrt(max((ww * (dev - m) ** 2).sum() / max(ww.sum(), 1e-12), 0.0)))
    alpha_f, beta_f = moments(fwd)
    alpha_b, beta_b = moments(~fwd)
    return Averages(a_f, a_b, alpha_f, alpha_b, beta_f, beta_b)


def backscatter(av):
    """Zinke 2008 eq. 22-25: the backward multiple-scattering attenuation A_b, its shift and spread (sum over
    paths with one or three backward events between forward ones)."""
    af2 = np.minimum(av.a_f ** 2, 0.999)
    ab = av.a_b
    A1 = ab * af2 / (1 - af2)
    A3 = ab ** 3 * af2 / (1 - af2) ** 3
    A_b = A1 + A3
    delta_b = av.alpha_b * (1 - 2 * ab ** 2 / (1 - af2) ** 2) + \
        av.alpha_f * (2 * (1 - af2) ** 2 + 4 * af2 * ab ** 2) / (1 - af2) ** 3
    sigma_b = (1 + D_B * af2) * (ab * np.sqrt(2 * av.beta_f ** 2 + av.beta_b ** 2) +
                                 ab ** 3 * np.sqrt(2 * av.beta_f ** 2 + 3 * av.beta_b ** 2)) / \
        np.maximum(ab + ab ** 3 * (2 * av.beta_f + 3 * av.beta_b), 1e-12)
    return A_b, delta_b, sigma_b


def gauss(x, var):
    return np.exp(-x * x / (2 * var)) / np.sqrt(2 * np.pi * var)


def single_part(f_s, n_front, a_f):
    """The single-scattering part of F: f_s for a directly lit fibre, T_f f_s = d_f a_f^n f_s behind n fibres. The
    correction k never scales it (a fibre behind a strand never gets more f_s than a directly lit one)."""
    n = np.asarray(n_front, float)
    return f_s * np.where((n == 0)[:, None], 1.0, D_F * np.asarray(a_f)[None, :] ** n[:, None])


def shade(t, wo, wi, h, n_front, color, roughness, av=None, corrected=False):
    """F(wi, wo) per unit irradiance with dual scattering; n_front: fibres on the shadow ray in front (array).
    corrected: the backward multiple-scattering part (F minus single_part) times the path-traced correction k per
    channel (TABLE_K)."""
    av = av or averages(color, roughness)
    if corrected:
        single = single_part(bsdf.evaluate(t, wo, wi, h, color, roughness), n_front, av.a_f)
        return single + correction(color, roughness)[None, :] * (shade(t, wo, wi, h, n_front, color, roughness, av) - single)
    A_b, delta_b, sigma_b = backscatter(av)
    f_s = bsdf.evaluate(t, wo, wi, h, color, roughness)
    sin_o, sin_i = np.einsum("nd,nd->n", wo, t), np.einsum("nd,nd->n", wi, t)
    th_o, th_i = np.arcsin(np.clip(sin_o, -1, 1)), np.arcsin(np.clip(sin_i, -1, 1))
    th_h, th_d = (th_o + th_i) / 2, (th_o - th_i) / 2
    n = np.asarray(n_front, float)
    var_f = n * av.beta_f ** 2
    var = (var_f[:, None] + sigma_b[None, :] ** 2)
    f_back = 2 * A_b[None, :] * gauss((2 * th_h)[:, None] - delta_b[None, :], var) / (np.pi * np.cos(th_d) ** 2)[:, None]
    T_f = D_F * av.a_f[None, :] ** n[:, None]
    direct = (n == 0)[:, None]
    return np.where(direct, f_s + D_B * f_back, T_f * (f_s + np.pi * D_B * f_back))


def albedo(color, roughness, n_front=0, n=200000, seed=5):
    """4 pi mean of F over random directions and offsets: the reflected fraction of a lit fibre in a hair volume."""
    rng = np.random.default_rng(seed)
    t = np.tile([0.0, 0.0, 1.0], (n, 1))
    wo, wi = bsdf._sphere(rng, n), bsdf._sphere(rng, n)
    h = rng.uniform(-1, 1, n)
    av = averages(color, roughness)
    F = shade(t, wo, wi, h, np.full(n, n_front), color, roughness, av)
    # Backward half only: what a viewer on the light's side sees from the volume's surface.
    po, pi_ = wo[:, :2], wi[:, :2]
    back = (po * pi_).sum(1) >= 0
    return (F * back[:, None]).mean(0) * 4 * np.pi, (bsdf.evaluate(t, wo, wi, h, color, roughness) * back[:, None]).mean(0) * 4 * np.pi


UP = np.array([0.0, 0.0, 1.0])


def _upper(rng, k):
    v = bsdf._sphere(rng, k)
    v[:, 2] = np.abs(v[:, 2])
    return v


def slab_retro_reference(color, roughness, n=20000, bounces=300, seed=2, candidates=32, views=8):
    """Path-traced radiance leaving a semi-infinite slab of random fibres toward the light (normal incidence and
    normal view, unit mean free path), per unit irradiance: every scattering event adds its next-event estimate
    w * S(up) * exp(-depth). Same walk as bsdf.slab_albedo (resampled directions, Russian roulette)."""
    rng = np.random.default_rng(seed)
    w = np.ones((n, 3))
    d = np.tile([0.0, 0.0, -1.0], (n, 1))
    z = np.zeros(n)
    out = np.zeros(3)
    alive = np.ones(n, bool)
    for _ in range(bounces):
        idx = np.flatnonzero(alive)
        if len(idx) == 0:
            break
        z[idx] += d[idx, 2] * rng.exponential(1.0, len(idx))
        alive[idx[z[idx] > 0]] = False
        idx = np.flatnonzero(alive)
        if len(idx) == 0:
            break
        m_ = len(idx)
        t1 = bsdf._sphere(rng, m_)
        h1 = rng.uniform(-1, 1, m_)
        if views:   # radiance averaged over the upper hemisphere: `views` uniform directions per event
            for _v in range(views):
                wv = _upper(rng, m_)
                s_v = bsdf.evaluate(t1, wv, -d[idx], h1, color, roughness)
                out += (w[idx] * s_v * np.exp(z[idx] / wv[:, 2].clip(1e-3))[:, None]).sum(0) / views
        else:       # the retro direction only
            s_up = bsdf.evaluate(t1, np.tile(UP, (m_, 1)), -d[idx], h1, color, roughness)
            out += (w[idx] * s_up * np.exp(z[idx])[:, None]).sum(0)          # z < 0 inside: exp(-depth)
        t = np.repeat(t1, candidates, 0)
        h = np.repeat(h1, candidates)
        wi = np.repeat(-d[idx], candidates, 0)
        wo = bsdf._sphere(rng, m_ * candidates)
        S = (4 * np.pi * bsdf.evaluate(t, wo, wi, h, color, roughness)).reshape(m_, candidates, 3)
        lum = np.maximum(S @ np.array([0.2126, 0.7152, 0.0722]), 1e-12)
        cdf = np.cumsum(lum, 1)
        pick = np.minimum((cdf < rng.uniform(size=(m_, 1)) * cdf[:, -1:]).sum(1), candidates - 1)
        rows = np.arange(m_)
        w[idx] *= S[rows, pick] / lum[rows, pick, None] * lum.mean(1)[:, None]
        d[idx] = wo.reshape(m_, candidates, 3)[rows, pick]
        mx = w[idx].max(1)
        q = np.minimum(1.0, mx)
        kill = rng.uniform(size=len(idx)) >= q
        w[idx[~kill]] /= q[~kill, None]
        alive[idx[kill]] = False
    return out / n


def slab_retro_dual(color, roughness, n=200000, seed=4, single_only=False, views=True, corrected=False, single=False):
    """The same radiance from the visible fibre alone, shaded with dual scattering: the view ray's first fibre at
    depth z ~ Exp(1), n ~ Poisson(z) fibres on the light ray in front of it, F(up, up, n). single_only: f_s with
    only the direct (n = 0) light, the model without multiple scattering. single: single_part (f_s, and T_f f_s
    behind n fibres), the part of F the correction k does not scale."""
    rng = np.random.default_rng(seed)
    t = bsdf._sphere(rng, n)
    h = rng.uniform(-1, 1, n)
    up = np.tile(UP, (n, 1))
    wo = _upper(rng, n) if views else up
    zz = rng.exponential(1.0, n) * wo[:, 2]   # depth of the view ray's first fibre
    nf = rng.poisson(zz)
    # The reference's next-event estimates are flux per unit slab area: radiance x cos(theta_o).
    cos_o = wo[:, 2:3]
    if single_only:
        return (bsdf.evaluate(t, wo, up, h, color, roughness) * (nf == 0)[:, None] * cos_o).mean(0)
    if single:
        return (single_part(bsdf.evaluate(t, wo, up, h, color, roughness), nf, averages(color, roughness).a_f) * cos_o).mean(0)
    return (shade(t, wo, up, h, nf, color, roughness, corrected=corrected) * cos_o).mean(0)


# Per-channel correction of the backward multiple scattering (F minus single_part), from the path-traced diffuse
# slab (make_table): k(albedo, roughness) = (A - single) / back on grey colours (each channel's absorption sets its own
# response). Rows TABLE_C, columns TABLE_BETA; roughness below 0.35 is clamped (the walks do not converge there).
# Generated by `python -m tools.hair_groom.dual --table`.
TABLE_C = (0.02, 0.05, 0.1, 0.2, 0.35, 0.5, 0.7, 0.9)
TABLE_BETA = (0.35, 0.5, 0.7)
# 2026-11-08 (diffuse fit, k on the backward part only). The 2026-11-03 table (normal incidence, 2 x 12000 paths, k on
# T_f f_s too) zig-zagged in colour (2.43, 3.55, 2.51 at beta 0.5): the normal-incidence walk's weights are heavy
# tailed (12000-path walks at c 0.7, beta 0.5: 0.083..0.134); this fit is monotonic in roughness and in colour above 0.2.
TABLE_K = ((1.785, 2.173, 3.940),
           (1.329, 1.660, 2.617),
           (1.124, 1.381, 1.933),
           (1.012, 1.211, 1.619),
           (1.026, 1.209, 1.531),
           (1.121, 1.296, 1.616),
           (1.343, 1.523, 1.894),
           (1.659, 1.863, 2.320))


def diffuse_parts(color, beta, n=200000, seed=41):
    """volume_integral's diffuse slab integral of the shader's uncorrected model (table averages), in its two parts:
    the single scattering (single_part) and the backward multiple scattering that k scales; first channel."""
    rng = np.random.default_rng(seed)
    t, wo = bsdf._sphere(rng, n), _upper(rng, n)
    u, phi = rng.uniform(size=n), rng.uniform(0, 2 * np.pi, n)
    wi = np.column_stack((np.sqrt(u) * np.cos(phi), np.sqrt(u) * np.sin(phi), np.sqrt(1 - u)))
    h = rng.uniform(-1, 1, n)
    nf = rng.poisson(rng.exponential(size=n) * wo[:, 2] / wi[:, 2].clip(1e-8))
    av = averages_table(color, beta)
    F = shade(t, wo, wi, h, nf, color, beta, av)
    single = single_part(bsdf.evaluate(t, wo, wi, h, color, beta), nf, av.a_f)
    measure = (2 * np.pi * wo[:, 2] / wi[:, 2])[:, None]
    return float((single * measure).mean(0)[0]), float(((F - single) * measure).mean(0)[0])


def make_table():
    """k per (colour value, roughness) = (A - single) / back on the diffuse slab (diffuse_parts): A the path-traced
    diffuse slab reflectance of make_volume_table (volume_calibration.json "albedo", 2 x 50000 paths, 4000 bounces).
    A fit at diffuse incidence conserves the energy a lit convex hair volume reflects (its flux-weighted incidence
    is the cosine distribution); the old fit at normal incidence reflected 1.0-2.2 x the path-traced energy there
    (white fibres > 1)."""
    import json
    from pathlib import Path
    cells = json.loads((Path(__file__).resolve().parent / "volume_calibration.json").read_text())
    k = np.zeros((len(TABLE_C), len(TABLE_BETA)))
    for i, c in enumerate(TABLE_C):
        for j, b in enumerate(TABLE_BETA):
            a = cells[f"{c}/{b}"]["albedo"]
            s, m = diffuse_parts((c, c, c), b)
            k[i, j] = (a - s) / max(m, 1e-9)
            print(f"[dual table] {i * len(TABLE_BETA) + j + 1}/{len(TABLE_C) * len(TABLE_BETA)} c {c} beta {b}: "
                  f"path-traced {a:.5f} single {s:.5f} back {m:.5f} k {k[i, j]:.3f}", flush=True)
    return k


def correction(color, roughness):
    """Bilinear k per channel (clamped to the table's range)."""
    if TABLE_K is None:
        return np.ones(3)
    k = np.asarray(TABLE_K)
    b = float(np.clip(roughness, TABLE_BETA[0], TABLE_BETA[-1]))
    out = []
    for c in np.clip(np.asarray(color, float), TABLE_C[0], TABLE_C[-1]):
        col = np.array([np.interp(c, TABLE_C, k[:, j]) for j in range(len(TABLE_BETA))])
        out.append(np.interp(b, TABLE_BETA, col))
    return np.array(out)


# Fibre averages on the same grid (grey colour c, roughness beta): a_f, a_b, alpha_f, alpha_b, beta_f, beta_b
# (dual.averages with 400k samples, 2026-11-03). The shader reads these instead of integrating per pixel.
AVG_TABLE = (
    ((0.3689, 0.0772, -0.0194, 0.0217, 0.2196, 0.4132), (0.3305, 0.0828, -0.0191, 0.0161, 0.3561, 0.5882), (0.2340, 0.0926, -0.0158, 0.0045, 0.5772, 0.7785)),
    ((0.5108, 0.0937, -0.0235, 0.0079, 0.2109, 0.4591), (0.4727, 0.1027, -0.0227, 0.0075, 0.3378, 0.6159), (0.3664, 0.1225, -0.0195, -0.0018, 0.5350, 0.7466)),
    ((0.6225, 0.1108, -0.0254, -0.0019, 0.2083, 0.4937), (0.5887, 0.1232, -0.0243, 0.0017, 0.3310, 0.6446), (0.4873, 0.1542, -0.0210, -0.0053, 0.5177, 0.7391)),
    ((0.7233, 0.1296, -0.0266, -0.0094, 0.2083, 0.5223), (0.6958, 0.1459, -0.0253, -0.0027, 0.3289, 0.6728), (0.6078, 0.1904, -0.0219, -0.0077, 0.5096, 0.7445)),
    ((0.7868, 0.1432, -0.0271, -0.0135, 0.2094, 0.5389), (0.7643, 0.1624, -0.0258, -0.0050, 0.3295, 0.6911), (0.6889, 0.2176, -0.0222, -0.0087, 0.5077, 0.7532)),
    ((0.8152, 0.1497, -0.0273, -0.0151, 0.2102, 0.5461), (0.7953, 0.1705, -0.0260, -0.0060, 0.3302, 0.6995), (0.7266, 0.2310, -0.0223, -0.0091, 0.5077, 0.7585)),
    ((0.8320, 0.1537, -0.0274, -0.0160, 0.2108, 0.5503), (0.8136, 0.1754, -0.0260, -0.0065, 0.3308, 0.7044), (0.7492, 0.2394, -0.0224, -0.0093, 0.5080, 0.7620)),
    ((0.8375, 0.1551, -0.0275, -0.0163, 0.2110, 0.5517), (0.8197, 0.1771, -0.0261, -0.0067, 0.3311, 0.7061), (0.7568, 0.2422, -0.0224, -0.0094, 0.5082, 0.7632)),
)


def _bilinear(table, c, beta):
    """Bilinear in (log c, beta) over TABLE_C x TABLE_BETA, clamped; table rows c, columns beta, trailing values."""
    t = np.asarray(table, float)
    cs, bs = np.asarray(TABLE_C), np.asarray(TABLE_BETA)
    c = float(np.clip(c, cs[0], cs[-1]))
    beta = float(np.clip(beta, bs[0], bs[-1]))
    i = int(np.clip(np.searchsorted(cs, c) - 1, 0, len(cs) - 2))
    j = int(np.clip(np.searchsorted(bs, beta) - 1, 0, len(bs) - 2))
    u = (np.log(c) - np.log(cs[i])) / (np.log(cs[i + 1]) - np.log(cs[i]))   # the averages are smooth in log c
    v = (beta - bs[j]) / (bs[j + 1] - bs[j])
    return ((1 - u) * (1 - v) * t[i, j] + u * (1 - v) * t[i + 1, j] + (1 - u) * v * t[i, j + 1] + u * v * t[i + 1, j + 1])


def averages_table(color, roughness):
    """Averages from AVG_TABLE: a_f/a_b per channel, shifts and spreads at the colour's luminance (the shader's)."""
    col = np.asarray(color, float)
    per = np.array([_bilinear(AVG_TABLE, c, roughness) for c in col])
    lum = float(col @ np.array([0.2126, 0.7152, 0.0722]))
    g = _bilinear(AVG_TABLE, lum, roughness)
    return Averages(per[:, 0], per[:, 1], g[2], g[3], g[4], g[5])


def correction_table(color, roughness):
    """k per channel by the shader's bilinear rule (log c); correction() interpolates linearly in c."""
    return np.array([float(_bilinear(np.asarray(TABLE_K)[..., None], c, roughness)[0]) for c in np.asarray(color, float)])


def shade_table(t, wo, wi, h, n_front, color, roughness):
    """The shader's model: shade(corrected) with the table averages (hair_dual.hlsli hd_shade's twin)."""
    av = averages_table(color, roughness)
    single = single_part(bsdf.evaluate(t, wo, wi, h, color, roughness), n_front, av.a_f)
    return single + correction_table(color, roughness)[None, :] * (shade(t, wo, wi, h, n_front, color, roughness, av) - single)


def shade_table_pixel(t, wo, wi, n_front, color, roughness):
    """hd_shade_pixel's twin: the single scattering averaged over the offsets -0.75, -0.25, 0.25, 0.75."""
    k = len(t)
    f_s = sum(bsdf.evaluate(t, wo, wi, np.full(k, h), color, roughness) for h in (-0.75, -0.25, 0.25, 0.75)) / 4
    av = averages_table(color, roughness)
    # The backward multiple scattering does not depend on the offset: shade() at h = 0 minus its single part.
    f0 = bsdf.evaluate(t, wo, wi, np.zeros(k), color, roughness)
    back = shade(t, wo, wi, np.zeros(k), n_front, color, roughness, av) - single_part(f0, n_front, av.a_f)
    return single_part(f_s, n_front, av.a_f) + correction_table(color, roughness)[None, :] * back


def volume_integral(color, beta, n=200000, seed=41):
    """Diffuse slab integral of the existing corrected dual model; returns (single, multiple).

    Incident photons are cosine distributed, outgoing directions uniform over the upper hemisphere.
    The first visible fibre has depth z = mu_o Exp(1), and the incident path crosses Poisson(z/mu_i)
    fibres. A_raw = S + M = 2 pi <mu_o/mu_i shade_table>, S = 2 pi <mu_o/mu_i 1[n=0] f_s> (the direct single
    scattering), M the rest (T_f f_s behind n > 0 fibres and k times the backward multiple scattering).
    F_dual is shade with table averages, before k: Zinke 2008 eqs. 5, 10, 14, 18-22 and Fig. 5
    (the angular slab integral is our derivation, NOT a new formula attributed to that paper).
    This integrates a_f, a_b, d_f, d_b and k over the slab rather than treating the backward fraction
    at one fibre as the reflectance of an entire volume.
    """
    rng = np.random.default_rng(seed)
    t, wo = bsdf._sphere(rng, n), _upper(rng, n)
    u, phi = rng.uniform(size=n), rng.uniform(0, 2 * np.pi, n)
    wi = np.column_stack((np.sqrt(u) * np.cos(phi), np.sqrt(u) * np.sin(phi), np.sqrt(1 - u)))
    h = rng.uniform(-1, 1, n)
    nf = rng.poisson(rng.exponential(size=n) * wo[:, 2] / wi[:, 2].clip(1e-8))
    single = bsdf.evaluate(t, wo, wi, h, color, beta) * (nf == 0)[:, None]
    f = shade_table(t, wo, wi, h, nf, color, beta)
    # wi is sampled by incident flux (mu_i/pi), whereas a fibre integrates radiance d omega_i.
    measure = (2 * np.pi * wo[:, 2] / wi[:, 2])[:, None]
    return (single * measure).mean(0), ((f - single) * measure).mean(0)


def volume_albedo(colour, beta):
    """Diffuse reflectance of a semi-infinite random-fibre slab, per RGB channel.

    A = S + k_volume M, where S and M are volume_integral's single and corrected multiple
    terms. k_volume = (path-traced diffuse slab - S)/M; 1 within 0.03% since TABLE_K is fitted on
    the same diffuse slab (2026-11-08; below 1 with the earlier normal-incidence table). The nonabsorbing endpoint
    A(1,beta)=1 follows conservation in a semi-infinite slab, not the clamped 0.9 direct-light table.
    See docs/RTX_HAIR_PRIMARY.md M4.1 for the derivation and limitations.
    """
    from .volume_table import VOLUME_C, VOLUME_A
    # Reflectance is smooth in colour itself (unlike the single-fibre averages); explicit white endpoint.
    out = []
    b = float(np.clip(beta, TABLE_BETA[0], TABLE_BETA[-1]))
    j = int(np.clip(np.searchsorted(TABLE_BETA, b) - 1, 0, len(TABLE_BETA) - 2))
    v = (b - TABLE_BETA[j]) / (TABLE_BETA[j + 1] - TABLE_BETA[j])
    for c in np.clip(np.asarray(colour, float), VOLUME_C[0], 1):
        i = int(np.clip(np.searchsorted(VOLUME_C, c) - 1, 0, len(VOLUME_C) - 2))
        u = (c - VOLUME_C[i]) / (VOLUME_C[i + 1] - VOLUME_C[i])
        a = np.asarray(VOLUME_A)
        out.append((1-u)*((1-v)*a[i,j]+v*a[i,j+1])+u*((1-v)*a[i+1,j]+v*a[i+1,j+1]))
    return np.array(out)


def hlsl_tables():
    """The generated body of shaders/rt/hair_dual_table.hlsli (CTest test_dual checks it is current)."""
    fmt = lambda x: f"{x:.4f}"
    lines = ["// Generated by `python -m tools.hair_groom.dual --hlsl` from tools/hair_groom/dual.py (AVG_TABLE, TABLE_K).",
             "// Do not edit: CTest hair_groom_test (test_dual) fails when it differs from the Python tables.",
             "#pragma once",
             "static const float kHdC[8] = {" + ", ".join(fmt(c) for c in TABLE_C) + "};",
             "static const float kHdBeta[3] = {" + ", ".join(fmt(b) for b in TABLE_BETA) + "};",
             "// Flat (a multi-dimensional static array read shifted fields on the GPU): entry (c, beta) at 3 c + beta,",
             "// 7 floats each: a_f, a_b, alpha_f, alpha_b, beta_f, beta_b, k (the correction). One array: a second",
             "// static array (k alone) read shifted entries on the GPU (DXC SPIR-V, 2026-11-08).",
             "static const float kHdAvg[8 * 3 * 7] = {"]
    for r, kr in zip(AVG_TABLE, TABLE_K):
        lines.append("  " + ", ".join(", ".join(fmt(x) for x in tuple(e) + (k,)) for e, k in zip(r, kr)) + ",")
    lines.append("};")
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    import sys
    from pathlib import Path
    if "--table" in sys.argv:
        print(np.array2string(make_table(), precision=3, separator=", "))
    if "--hlsl" in sys.argv:
        out = Path(__file__).resolve().parents[2] / "shaders" / "rt" / "hair_dual_table.hlsli"
        out.write_text(hlsl_tables(), encoding="utf-8", newline="\n")
        print(f"wrote {out}")
