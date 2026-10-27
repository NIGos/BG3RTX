"""Where the QA thresholds come from (docs/QA.md "Thresholds and their derivation").

User directive 2026-10-27: a gate, threshold, tolerance or budget comes from physics, the
measured noise (per-pixel variance of repeated traces, run-to-run spread), the display (a
code step, a published perceptual threshold), the frame budget, or a cited standard.
Anything else is review-only. This module holds the constants that are derived once and
the functions that derive the per-run ones from the traces themselves.
"""
import math
from statistics import NormalDist

import numpy as np

from . import imgops as im

# ---------------------------------------------------------------- standards and the display
# Family-wise false-alarm probability of one statistical test family (all pixels of one
# buffer, or all scalar gates of one check): the conventional 5% significance level (Fisher).
# A family of n tests uses alpha / n per test (Bonferroni), so the expected number of false
# alarms in the whole family stays below 5%, whatever its size.
ALPHA_FW = 0.05
# One 8-bit code step of the previews the harness reads (layer/dump.cpp).
PREVIEW_STEP = 1.0 / 255.0
# One 10-bit PQ code step: SMPTE ST 2084 is designed so that 12-bit steps stay below the
# Barten contrast threshold and 10-bit steps sit at about it (Miller, Nezamabadi, Daly 2013,
# "Perceptual Signal Coding for More Efficient Usage of Bit Codes"). A temporal std below one
# step is not visible on the HDR10 output.
PQ10_STEP = 1.0 / 1023.0
# Sub-pixel camera jitter (DLSS/TAA Halton offsets within +-0.5 px): a static edge moves by
# less than 1 px between two frames.
JITTER_PX = 1.0


def z_one_sided(n_tests, alpha=ALPHA_FW):
    """Standard-normal quantile for a one-sided test at alpha / n_tests (Bonferroni)."""
    p = alpha / max(1.0, float(n_tests))
    return NormalDist().inv_cdf(1.0 - p)


def independent_px(shape, blur):
    """Independent samples in a buffer after a (2 blur + 1)^2 box: neighbours inside the box
    share their inputs, so at most one sample per box area is independent."""
    return shape[0] * shape[1] / float((2 * blur + 1) ** 2)


def k_sigma(shape, blur):
    """Change significance in units of the measured per-pixel sigma of a difference of two
    frames: Bonferroni over the buffer's independent samples at ALPHA_FW. 1280x533 with a
    3x3 box: 4.84; 3840x1600: 5.26; with a 5x5 box: 4.63 and 5.07."""
    return z_one_sided(independent_px(shape, blur))


def min_area(blur, r_open=1):
    """Smallest region worth reporting: a mask opened with radius r_open on a field boxed
    with radius `blur` cannot hold a component narrower than one structuring element grown
    by the box's support, (2 (blur + r_open) + 1)^2 px (25 px for a 3x3 box and a radius-1
    opening). Smaller components are the filters' own footprint, not a measured shape."""
    return int((2 * (blur + r_open) + 1) ** 2)


def edge_threshold(removed, blur):
    """|d log L| per px that makes an effect-off edge strong enough to explain a drop of
    `removed`: a step of that contrast, ln(1 / (1 - removed)), after the detector's own
    (2 blur + 1) box has its steepest central-difference gradient at contrast / (2 blur + 1).
    (removed 0.6, 3x3 box: 0.31; 5x5: 0.18.)"""
    return math.log(1.0 / (1.0 - removed)) / (2 * blur + 1)


def jitter_change(field, step):
    """Per-pixel change of a static field between two frames that the sub-pixel jitter
    alone can cause, plus one code step of its encoding: |grad field| (max over the 1 px the
    jitter moves) x JITTER_PX + step. `field` is the already box-filtered reference."""
    g, _, _ = im.grad_mag(field)
    return im.maxf(g, 1) * JITTER_PX + step


def code_step(a, sample=200000):
    """Smallest non-zero difference between the values of a decoded preview: its code step
    in the decoded units (0 for float data)."""
    v = np.asarray(a, np.float32).ravel()
    if v.size > sample:
        v = v[:: max(1, v.size // sample)]
    u = np.unique(v)
    d = np.diff(u)
    d = d[d > 0]
    return float(d.min()) if d.size else 0.0


def n_eff_box(residuals, mask, r):
    """Effective number of independent samples in a (2r+1)^2 box mean of the per-pixel
    variance, from the measured spatial autocorrelation rho of the temporal residuals (RR,
    the upscaler and the previews correlate neighbours). For Gaussian noise
    Cov(e_i^2, e_j^2) = 2 rho_ij^2 sigma^4, so n_eff = n^2 / sum_ij rho_ij^2 (n = box area),
    between 1 (fully correlated) and n (independent)."""
    m = np.asarray(mask, bool)
    if not m.any():
        return 1.0
    h, w = m.shape
    L = 2 * r
    shp = (h + L, w + L)
    acc = np.zeros(shp, np.float64)
    for x in residuals:
        x = np.where(m, np.asarray(x, np.float64), 0.0)
        f = np.fft.rfft2(x, s=shp)
        acc += np.fft.irfft2(f * np.conj(f), s=shp)
    fm = np.fft.rfft2(m.astype(np.float64), s=shp)
    cnt = np.fft.irfft2(fm * np.conj(fm), s=shp)
    if acc[0, 0] <= 0:
        return float((2 * r + 1) ** 2)
    rho0 = acc[0, 0] / max(cnt[0, 0], 1.0)
    n = 2 * r + 1
    s = 0.0
    for dy in range(-L, L + 1):
        for dx in range(-L, L + 1):
            c = cnt[dy % shp[0], dx % shp[1]]
            if c < 1:
                continue
            rho = (acc[dy % shp[0], dx % shp[1]] / c) / rho0
            s += rho * rho * (n - abs(dy)) * (n - abs(dx))
    return float(np.clip(n ** 4 / max(s, 1e-12), 1.0, n * n))


def psf_sigma(residuals):
    """Width (Gaussian sigma, px) of the pipeline's point spread at the analysis resolution,
    from the temporal residuals of repeated frames: white per-pixel noise (1 spp before RR)
    leaves the reconstruction (RR, upscaling, preview resampling) correlated by the PSF
    convolved with itself, rho(1) = exp(-1 / (4 sigma_p^2)) for a Gaussian PSF, so
    sigma_p = 1 / (2 sqrt(-ln rho(1))). rho(1) is the mean lag-1 autocorrelation along x and
    y. 0 when the noise is uncorrelated (rho <= 0)."""
    num = den = 0.0
    for x in residuals:
        x = np.asarray(x, np.float64)
        x = x - x.mean()
        num += float((x[:, 1:] * x[:, :-1]).sum() + (x[1:, :] * x[:-1, :]).sum())
        den += float((x[:, 1:] ** 2).sum() + (x[1:, :] ** 2).sum())
    if den <= 0:
        return 0.0
    rho = num / den
    if rho <= 0:
        return 0.0
    rho = min(rho, 0.999)
    return 1.0 / (2.0 * math.sqrt(-math.log(rho)))


def preview_quant_sigma(lin, kind):
    """Std of the rounding of an 8-bit preview, in the decoded linear units, per pixel and
    channel: step / sqrt(12) with step = decode(code + 1/255) - decode(code). `kind` "pq":
    the HDR10 final frame (PQ code, nits); "hdr": Reinhard + gamma 1/2.2 previews of float
    targets (layer/dump.cpp)."""
    from .decode import pq_to_linear
    lin = np.maximum(np.asarray(lin, np.float32), 0.0)
    if kind == "pq":
        m1, m2, c1, c2, c3 = 0.1593017578125, 78.84375, 0.8359375, 18.8515625, 18.6875
        y = np.power(np.clip(lin / 10000.0, 0, 1), m1)
        c = np.power((c1 + c2 * y) / (1 + c3 * y), m2)
        step = (pq_to_linear(np.clip(c + PREVIEW_STEP, 0, 1)) - pq_to_linear(c)) * 10000.0
    else:
        p = np.power(lin / (1.0 + lin), 1 / 2.2)
        t = np.clip(np.clip(p + PREVIEW_STEP, 0, 1) ** 2.2, 0, 0.9995)
        step = t / (1.0 - t) - lin
    return np.abs(step) / math.sqrt(12.0)


# ---------------------------------------------------------------- the registry
# Every constant the harness uses: class (derived / review / signature), source, where.
# "signature": a parameter of an artifact's definition that is not yet derived; listed so
# it stays visible (docs/QA.md has the proposed derivation of each).
REGISTRY = {
    "alpha_fw": ("derived", "standard: 5% family-wise false alarm (Fisher), Bonferroni per family"),
    "k_sigma": ("derived", "measured noise: z(ALPHA_FW / independent px) x per-pixel sigma"),
    "min_area": ("derived", "filter support: (2 (blur + r_open) + 1)^2 px"),
    "dark_fail_area": ("derived", "physics: any px below the light-off floor (energy) beyond k sigma -> 0"),
    "hard_dark floor": ("derived", "physics: qa_zero light-off traces (capture.FLOORS)"),
    "step_sharp": ("derived", "measured: the step response through psf_sigma() and the detector box"),
    "edge_thr": ("review", "edge_threshold(): the drop's own contrast through the detector's box"),
    "dark_removed": ("review", "dark-signature classifier: ranks crops"),
    "dark_sharp": ("review", "used only without a measured PSF"),
    "dark_unexplained": ("review", "no derivation (majority of the boundary): ranks crops"),
    "dark_black_shadows": ("review", "share at the floor: ranks crops"),
    "dark_black_level": ("review", "near-black without a floor trace: ranks crops"),
    "bright_ratio": ("review", "brightening is compared with the game's image (R0): crops only"),
    "sun_*": ("review", "sun mask vs game: crops only (R0)"),
    "sun_check_darker_fail": ("derived", "physics: darker than min(game, traced) is impossible -> 0"),
    "flicker_quant": ("derived", "display: half a preview code step"),
    "flicker_jnd": ("derived", "display: one 10-bit PQ step (ST 2084, Barten threshold)"),
    "flicker_fail_area": ("derived", "measured noise: per-pixel F-type test at ALPHA_FW, n_eff measured -> 0"),
    "anim_*": ("derived", "jitter: |grad| x 1 px + one code step"),
    "same_view": ("derived", "jitter: a global shift of >= 1 px (phase correlation) is a camera move"),
    "reference": ("derived", "measured noise: rank test of FLIP(ref, on_i) against FLIP(on_i, on_j)"),
    "roof_*": ("owned elsewhere", "SHaRC leak gate: being replaced by sharc_reference (other branch)"),
}
