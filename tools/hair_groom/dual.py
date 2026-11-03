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


def shade(t, wo, wi, h, n_front, color, roughness, av=None):
    """F(wi, wo) per unit irradiance with dual scattering; n_front: fibres on the shadow ray in front (array)."""
    av = av or averages(color, roughness)
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


def slab_retro_reference(color, roughness, n=20000, bounces=300, seed=2, candidates=32):
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


def slab_retro_dual(color, roughness, n=200000, seed=4, single_only=False):
    """The same radiance from the visible fibre alone, shaded with dual scattering: the view ray's first fibre at
    depth z ~ Exp(1), n ~ Poisson(z) fibres on the light ray in front of it, F(up, up, n). single_only: f_s with
    only the direct (n = 0) light, the model without multiple scattering."""
    rng = np.random.default_rng(seed)
    t = bsdf._sphere(rng, n)
    h = rng.uniform(-1, 1, n)
    zz = rng.exponential(1.0, n)
    nf = rng.poisson(zz)
    up = np.tile(UP, (n, 1))
    if single_only:
        return (bsdf.evaluate(t, up, up, h, color, roughness) * (nf == 0)[:, None]).mean(0)
    return shade(t, up, up, h, nf, color, roughness).mean(0)
