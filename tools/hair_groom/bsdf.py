"""Physically based hair fibre BSDF (offline prototype, roadmap step 4): Chiang, Bitterli, Tappan, Burley 2016,
"A Practical and Controllable Hair and Fur Model for Production Path Tracing" (EGSR), with d'Eon et al. 2011's
longitudinal scattering function. Written from the papers (no RTXCR or pbrt code).

Fibre frame: unit tangent t (root to tip); theta = angle out of the normal plane (sin theta = w . t); phi =
azimuth around t. h in [-1, 1] is the offset across the fibre where the ray hits it (gamma_o = asin h).
S(wi, wo) = sum_p M_p(theta_i, theta_o) A_p(theta_o, h) N_p(phi, h) for p = 0 (R), 1 (TT), 2 (TRT), plus the
residual p >= 3 term M_3 A_max / (2 pi). Outgoing radiance L_o = integral of S L_i over the sphere (dw =
cos theta dtheta dphi); with no absorption the integral of S over the sphere is 1 (energy conserving).
For a directional light of irradiance E: L_o = S * E.

CHAR_Hair mapping (material parameters of Materials.pak, see docs/RTX_HAIR_ROADMAP.md step 4):
  Hair_Color (linear RGB)    -> absorption sigma_a (Chiang eq. 9: the colour is the multiple-scattering albedo)
  Roughness                  -> beta_m = beta_n = Roughness (longitudinal and azimuthal roughness)
  cuticle tilt alpha = 2 deg, eta = 1.55 (keratin)
"""
from __future__ import annotations

import numpy as np

ETA = 1.55
ALPHA_DEG = 2.0
P_MAX = 3


def sigma_a_from_color(color, beta_n):
    """Chiang 2016 eq. 9: absorption that gives `color` as the multiple-scattering albedo of a hair volume."""
    c = np.clip(np.asarray(color, float), 1e-4, 1.0)
    d = 5.969 - 0.215 * beta_n + 2.532 * beta_n ** 2 - 10.73 * beta_n ** 3 + 5.574 * beta_n ** 4 + 0.245 * beta_n ** 5
    return (np.log(c) / d) ** 2


def longitudinal_variance(beta_m):
    v0 = (0.726 * beta_m + 0.812 * beta_m ** 2 + 3.7 * beta_m ** 20) ** 2
    return np.array([v0, 0.25 * v0, 4 * v0, 4 * v0])


def azimuthal_scale(beta_n):
    return np.sqrt(np.pi / 8) * (0.265 * beta_n + 1.194 * beta_n ** 2 + 5.372 * beta_n ** 22)


def _log_i0(x):
    """log of the modified Bessel function I0 (series below 12, asymptotic above)."""
    x = np.asarray(x, float)
    small = x < 12
    xs = np.where(small, x, 0.0)
    val = np.zeros_like(xs)
    term = np.ones_like(xs)
    for k in range(1, 30):
        term = term * (xs * xs / 4) / (k * k)
        val = val + term
    big = np.where(small, 1.0, x)
    return np.where(small, np.log1p(val), big + 0.5 * (-np.log(2 * np.pi) + np.log(1 / big)) + 1 / (8 * big))


def mp(cos_ti, cos_to, sin_ti, sin_to, v):
    """d'Eon 2011 longitudinal lobe; integral over theta_i of M_p cos theta_i = 1."""
    a = cos_ti * cos_to / v
    b = sin_ti * sin_to / v
    if v <= 0.1:
        return np.exp(_log_i0(a) - b - 1 / v + 0.6931 + np.log(1 / (2 * v)))
    return np.exp(-b) * np.exp(_log_i0(a)) / (np.sinh(1 / v) * 2 * v)


def fresnel(cos_i, eta):
    """Unpolarised dielectric reflectance."""
    cos_i = np.clip(cos_i, 0.0, 1.0)
    sin_t2 = (1 - cos_i ** 2) / eta ** 2
    cos_t = np.sqrt(np.clip(1 - sin_t2, 0, 1))
    rs = (cos_i - eta * cos_t) / (cos_i + eta * cos_t)
    rp = (eta * cos_i - cos_t) / (eta * cos_i + cos_t)
    return np.where(sin_t2 >= 1, 1.0, 0.5 * (rs * rs + rp * rp))


def ap(cos_to, h, sigma_a):
    """Attenuation of p = 0..3 (the last is the p >= 3 residual); shape (4, N, 3)."""
    sin_to = np.sqrt(np.clip(1 - cos_to ** 2, 0, 1))
    sin_tt = sin_to / ETA
    cos_tt = np.sqrt(np.clip(1 - sin_tt ** 2, 0, 1))
    etap = np.sqrt(np.clip(ETA ** 2 - sin_to ** 2, 1e-12, None)) / np.maximum(cos_to, 1e-6)
    sin_gt = h / etap
    cos_gt = np.sqrt(np.clip(1 - sin_gt ** 2, 0, 1))
    cos_go = np.sqrt(np.clip(1 - h * h, 0, 1))
    f = fresnel(cos_to * cos_go, ETA)[:, None]
    T = np.exp(-sigma_a[None, :] * (2 * cos_gt / np.maximum(cos_tt, 1e-6))[:, None])
    a0 = np.broadcast_to(f, T.shape)
    a1 = (1 - f) ** 2 * T
    a2 = a1 * f * T
    a3 = a2 * f * T / np.maximum(1 - T * f, 1e-6)
    return np.stack([a0, a1, a2, a3]), np.arcsin(np.clip(sin_gt, -1, 1))


def trimmed_logistic(x, s, a=-np.pi, b=np.pi):
    def cdf(y):
        return 1 / (1 + np.exp(-y / s))
    x = np.abs(x)
    pdf = np.exp(-x / s) / (s * (1 + np.exp(-x / s)) ** 2)
    return pdf / (cdf(b) - cdf(a))


def np_lobe(phi, p, s, gamma_o, gamma_t):
    dphi = phi - (2 * p * gamma_t - 2 * gamma_o + p * np.pi)
    dphi = np.mod(dphi + np.pi, 2 * np.pi) - np.pi
    return trimmed_logistic(dphi, s)


def evaluate(t, wo, wi, h, color, roughness):
    """S(wi, wo) per row (N, 3 RGB). t, wo, wi: (N, 3) unit vectors; h: (N,) in [-1, 1]."""
    beta = float(np.clip(roughness, 0.05, 1.0))
    v = longitudinal_variance(beta)
    s = azimuthal_scale(beta)
    sigma_a = sigma_a_from_color(color, beta)
    sin_to = np.clip(np.einsum("nd,nd->n", wo, t), -1, 1)
    sin_ti = np.clip(np.einsum("nd,nd->n", wi, t), -1, 1)
    cos_to = np.sqrt(1 - sin_to ** 2)
    cos_ti = np.sqrt(1 - sin_ti ** 2)
    # Azimuth difference: project both directions onto the normal plane.
    po = wo - sin_to[:, None] * t
    pi_ = wi - sin_ti[:, None] * t
    nrm = np.maximum(np.linalg.norm(po, axis=1) * np.linalg.norm(pi_, axis=1), 1e-9)
    cos_phi = np.clip(np.einsum("nd,nd->n", po, pi_) / nrm, -1, 1)
    sin_phi = np.einsum("nd,nd->n", np.cross(po, pi_), t) / nrm
    phi = np.arctan2(sin_phi, cos_phi)
    A, gamma_t = ap(cos_to, h, sigma_a)
    gamma_o = np.arcsin(np.clip(h, -1, 1))
    # Cuticle tilt: theta_o shifted by -2 alpha (R), +alpha (TT), +4 alpha (TRT).
    alpha = np.radians(ALPHA_DEG)
    out = np.zeros((len(h), 3))
    for p, k in ((0, -2.0), (1, 1.0), (2, 4.0)):
        so = sin_to * np.cos(k * alpha) + cos_to * np.sin(k * alpha)
        co = np.abs(cos_to * np.cos(k * alpha) - sin_to * np.sin(k * alpha))
        out += (mp(cos_ti, co, sin_ti, so, v[p]) * np_lobe(phi, p, s, gamma_o, gamma_t))[:, None] * A[p]
    out += (mp(cos_ti, cos_to, sin_ti, sin_to, v[3]) / (2 * np.pi))[:, None] * A[3]
    return out


# CHAR_Hair defaults (Materials.pak, mesh_materials catalogue): Hair_Color, Roughness, Scatter, HairBacklit.
CHAR_HAIR = {"color": (0.136, 0.082, 0.043), "roughness": 0.41, "scatter": 0.30, "backlit": 0.40}


def _pow5(x):
    return x ** 5


def game_hair(t, wo, wi, color, roughness, scatter, backlit):
    """The game's own hair shading per unit irradiance (shaders/rt/bg3_materials.hlsli bg3_hair_spec +
    bg3_hair_diff, the camera-pixel path), unshadowed (S = 1, so texp = 0 and N.L = 0.25)."""
    c = np.asarray(color, float)
    rough = max(0.09, roughness)
    sl = np.clip(np.einsum("nd,nd->n", t, wi), -1, 1)
    sv = np.clip(np.einsum("nd,nd->n", t, wo), -1, 1)
    ctd = np.cos(0.5 * np.abs(np.arcsin(sv) - np.arcsin(sl)))
    lp, vp = wi - t * sl[:, None], wo - t * sv[:, None]
    cphi = np.einsum("nd,nd->n", lp, vp) / np.sqrt((lp * lp).sum(1) * (vp * vp).sum(1) + 1e-4)
    chp = np.sqrt(np.clip(0.5 * cphi + 0.5, 0, 1))
    r2 = rough * rough
    B = np.stack([r2 * 1.4142135 * chp, np.full_like(chp, r2 * 0.5), np.full_like(chp, r2 * 2.0)], 1)
    sh = np.stack([-0.13988569 * (0.99755102 * chp * np.sqrt(1 - sv * sv) - 0.069942847 * sv),
                   np.full_like(chp, 0.035), np.full_like(chp, 0.14)], 1)
    tt_ = np.maximum(np.abs((sl + sv)[:, None] - sh), 1e-4)
    Mp = np.exp(tt_ * tt_ * -0.5 / (B * B)) / (B * 2.5066283)
    vol = np.einsum("nd,nd->n", wo, wi)
    fR = 0.046520568 + 0.95347941 * _pow5(1 - np.sqrt(np.clip(0.5 * vol + 0.5, 0, 1)))
    R = 0.25 * chp * fR * (1 + (backlit - 1) * np.clip(-vol, 0, 1)) * Mp[:, 0]
    ia = 1 / (0.36 * ctd + 1.19 / ctd)
    hh = chp * (ia * (-0.8 * cphi + 0.6) + 1)
    base = np.maximum(np.abs(c), 1e-4)[None, :]
    ft = 0.046520568 + 0.95347941 * _pow5(1 - ctd * np.sqrt(np.clip(1 - hh * hh, 0, 1)))
    hx = np.minimum(np.maximum(np.abs(hh * ia), 1e-4), 1)
    TT = base ** (0.5 * np.sqrt(1 - hx * hx) / ctd)[:, None] * (
        np.exp(-3.65 * cphi - 3.98) * np.maximum(np.abs(1 - ft), 1e-4) ** 2 * backlit * Mp[:, 1])[:, None]
    f3 = 0.046520568 + 0.95347941 * _pow5(1 - 0.5 * ctd)
    TRT = base ** (0.8 / ctd)[:, None] * (np.exp(17 * cphi - 16.78) * np.maximum(np.abs(1 - f3), 1e-4) ** 2 * f3
                                          * Mp[:, 2])[:, None]
    k = 0.31830987 * ((0.25 + 1) * 0.25 * 0.67 + (1 - np.abs(sl)) * 0.33) * scatter
    diff = np.sqrt(c)[None, :] * k[:, None]  # texp = 0: the tint factor is 1
    return R[:, None] + TT + TRT + diff


def lambert_fibre(t, wo, wi, h, color):
    """The current GI/reflection hit shading on hair (gi_common.hlsli mat_reply: Lambert with the albedo),
    applied to the fibre's round cross-section: normal at offset h, per unit irradiance."""
    sv = np.einsum("nd,nd->n", wo, t)
    vp = wo - sv[:, None] * t
    vp /= np.maximum(np.linalg.norm(vp, axis=1, keepdims=True), 1e-9)
    b = np.cross(t, vp)
    g = np.arcsin(np.clip(h, -1, 1))
    n = np.cos(g)[:, None] * vp + np.sin(g)[:, None] * b
    return np.asarray(color, float)[None, :] / np.pi * np.maximum(np.einsum("nd,nd->n", n, wi), 0)[:, None]


def _sphere(rng, k):
    z = rng.uniform(-1, 1, k)
    a = rng.uniform(0, 2 * np.pi, k)
    r = np.sqrt(1 - z * z)
    return np.stack([r * np.cos(a), r * np.sin(a), z], 1)


def furnace(roughness, color=(1.0, 1.0, 1.0), n=200000, seed=1, model="chiang", return_se=False, **kw):
    """Monte Carlo mean over wi of the integral of S over wo (fixed tangent, random h): the fibre's
    single-scattering albedo. 1 for Chiang without absorption (energy conserving)."""
    rng = np.random.default_rng(seed)
    t = np.tile([0.0, 0.0, 1.0], (n, 1))
    wo, wi = _sphere(rng, n), _sphere(rng, n)
    h = rng.uniform(-1, 1, n)
    if model == "chiang":
        S = evaluate(t, wo, wi, h, color, roughness)
    elif model == "game":
        S = game_hair(t, wo, wi, color, roughness, kw.get("scatter", CHAR_HAIR["scatter"]),
                      kw.get("backlit", CHAR_HAIR["backlit"]))
    else:
        S = lambert_fibre(t, wo, wi, h, color)
    if return_se:
        return S.mean(0) * 4 * np.pi, S.std(0) * 4 * np.pi / np.sqrt(n)
    return S.mean(0) * 4 * np.pi


def sample(t, wo, h, color, roughness, rng):
    """Sample evaluate(t, wo, wi, h): lobe attenuation mixture, d'Eon longitudinal marginal
    (a von Mises-Fisher ring), and inverse-CDF trimmed logistic azimuth. Returns wi and RGB S/pdf.
    Sampling from the known wo side makes attenuation known before choosing a direction, avoiding
    the multiplicative weight variance of resampling uniform candidates in near-conservative slabs.
    """
    beta = float(np.clip(roughness, 0.05, 1.0))
    n = len(h)
    so = np.clip(np.einsum('nd,nd->n', wo, t), -1, 1)
    co = np.sqrt(np.maximum(1-so*so, 0))
    a, gt = ap(co, h, sigma_a_from_color(color, beta))
    lum = a @ np.array([0.2126, 0.7152, 0.0722])
    total = lum.sum(0)
    p = np.minimum((np.cumsum(lum, axis=0) < rng.uniform(size=n)[None,:]*total).sum(0), 3)
    rows = np.arange(n)
    weight = a[p, rows] * (total / np.maximum(lum[p, rows], 1e-30))[:,None]
    tilt = np.radians(ALPHA_DEG) * np.array([-2, 1, 4, 0])[p]
    s0, c0 = so*np.cos(tilt)+co*np.sin(tilt), np.abs(co*np.cos(tilt)-so*np.sin(tilt))
    variance = longitudinal_variance(beta)[p]
    u = rng.uniform(size=n).clip(1e-15, 1-1e-15)
    ct = 1 + variance * np.log(u + (1-u)*np.exp(-2/variance))
    si = np.clip(-s0*ct+c0*np.sqrt(np.maximum(1-ct*ct,0))*np.cos(rng.uniform(0,2*np.pi,n)), -1, 1)
    scale = azimuthal_scale(beta)
    lo = 1/(1+np.exp(np.pi/scale))
    u = lo + rng.uniform(size=n)*(1-2*lo)
    phi = scale*np.log(u/(1-u)) + 2*p*gt - 2*np.arcsin(h) + p*np.pi
    phi = np.where(p == 3, rng.uniform(-np.pi,np.pi,n), phi)
    x = wo-so[:,None]*t
    x /= np.maximum(np.linalg.norm(x, axis=1, keepdims=True), 1e-15)
    wi = si[:,None]*t + np.sqrt(np.maximum(1-si*si,0))[:,None]*(np.cos(phi)[:,None]*x+np.sin(phi)[:,None]*np.cross(t,x))
    return wi, weight


def slab_albedo(color, roughness, n=20000, bounces=400, seed=2, candidates=32, diffuse=False, progress=False):
    """Colour check of the sigma_a mapping: diffuse reflectance of a semi-infinite slab of randomly oriented
    fibres (normal incidence, exponential free paths, each event scatters by S with a fresh random tangent
    and offset h; direction sampled uniformly, weighted by 4 pi S). Chiang's eq. 9 was fitted so that this is
    about the input colour. diffuse=True uses constant incident radiance (cosine-distributed flux)."""
    import time
    last_report = time.monotonic()
    rng = np.random.default_rng(seed)
    w = np.ones((n, 3))
    d = np.tile([0.0, 0.0, -1.0], (n, 1))
    if diffuse:
        # Constant incident radiance: photon flux is cosine distributed over the hemisphere.
        u, phi = rng.uniform(size=n), rng.uniform(0, 2 * np.pi, n)
        d = np.column_stack((np.sqrt(u) * np.cos(phi), np.sqrt(u) * np.sin(phi), -np.sqrt(1 - u)))
    z = np.zeros(n)
    out = np.zeros(3)
    alive = np.ones(n, bool)
    for bounce in range(bounces):
        if progress and time.monotonic() - last_report >= 30:
            print(f"slab: bounce {bounce}/{bounces}, {int(alive.sum())}/{n} paths alive, seed {seed}", flush=True)
            last_report = time.monotonic()
        idx = np.flatnonzero(alive)
        if len(idx) == 0:
            break
        z[idx] += d[idx, 2] * rng.exponential(1.0, len(idx))
        esc = idx[z[idx] > 0]
        out += w[esc].sum(0)
        alive[esc] = False
        idx = np.flatnonzero(alive)
        if len(idx) == 0:
            break
        # Resampled importance sampling: K uniform candidates per photon, pick one by luminance of S; the
        # weight (mean candidate weight) * S / lum(S) is unbiased and avoids the heavy tail of plain uniform
        # sampling of the narrow TT lobe.
        m_ = len(idx)
        if diffuse:
            # Adjoint walk: wo is the known previous direction, sample wi from the same BSDF.
            # Random orientation and offset at each fibre, as in the original slab walk below.
            d[idx], weight = sample(_sphere(rng, m_), -d[idx], rng.uniform(-1,1,m_), color, roughness, rng)
            w[idx] *= weight
            q = np.minimum(1.0, w[idx].max(1))
            keep = rng.uniform(size=m_) < q
            w[idx[keep]] /= q[keep,None]
            alive[idx[~keep]] = False
            continue
        t = np.repeat(_sphere(rng, m_), candidates, 0)
        h = np.repeat(rng.uniform(-1, 1, m_), candidates)
        wi = np.repeat(-d[idx], candidates, 0)  # toward where the light came from
        wo = _sphere(rng, m_ * candidates)
        S = (4 * np.pi * evaluate(t, wo, wi, h, color, roughness)).reshape(m_, candidates, 3)
        lum = np.maximum(S @ np.array([0.2126, 0.7152, 0.0722]), 1e-12)
        cdf = np.cumsum(lum, 1)
        pick = (cdf < rng.uniform(size=(m_, 1)) * cdf[:, -1:]).sum(1)
        pick = np.minimum(pick, candidates - 1)
        rows = np.arange(m_)
        w[idx] *= S[rows, pick] / lum[rows, pick, None] * lum.mean(1)[:, None]
        d[idx] = wo.reshape(m_, candidates, 3)[rows, pick]
        # Russian roulette on the largest channel keeps the estimate unbiased.
        m = w[idx].max(1)
        q = np.minimum(1.0, m)
        kill = rng.uniform(size=len(idx)) >= q
        w[idx[~kill]] /= q[~kill, None]
        alive[idx[kill]] = False
    return out / n


def lobe_sweep(model, theta_i_deg=20.0, n_phi=73, n_h=64, **kw):
    """S along the azimuth phi in [0, 180] deg (0: view along the light, front lit; 180: backlit), theta_o =
    -theta_i (the specular cone), averaged over h. Returns (phi_deg, (n_phi, 3))."""
    p = dict(CHAR_HAIR)
    p.update(kw)
    ti = np.radians(theta_i_deg)
    phis = np.linspace(0, np.pi, n_phi)
    hs = (np.arange(n_h) + 0.5) / n_h * 2 - 1
    out = []
    for phi in phis:
        wi = np.array([np.cos(ti), 0, np.sin(ti)])
        wo = np.array([np.cos(ti) * np.cos(phi), np.cos(ti) * np.sin(phi), -np.sin(ti)])
        t = np.tile([0.0, 0.0, 1.0], (n_h, 1))
        W, I = np.tile(wo, (n_h, 1)), np.tile(wi, (n_h, 1))
        if model == "chiang":
            S = evaluate(t, W, I, hs, p["color"], p["roughness"])
        elif model == "game":
            S = game_hair(t, W, I, p["color"], p["roughness"], p["scatter"], p["backlit"])
        else:
            S = lambert_fibre(t, W, I, hs, p["color"])
        out.append(S.mean(0))
    return np.degrees(phis), np.array(out)
