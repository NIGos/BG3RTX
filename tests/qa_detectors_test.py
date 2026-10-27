"""Synthetic checks of the QA harness's derived thresholds (no game, no captures).

    python tests/qa_detectors_test.py

Each case states what the derivation promises: no false alarm on pure measured noise, a
detection on an artifact that the gate is defined for, and the closed-form values quoted in
docs/QA.md "Thresholds and their derivation".
"""
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from scripts.qa import detectors as det, imgops as im, thresholds as th  # noqa: E402

H, W = 533, 1280
FAILS = []


def check(name, ok, info=""):
    print(f"{'ok  ' if ok else 'FAIL'} {name} {info}")
    if not ok:
        FAILS.append(name)


def correlated_noise(rng, shape, r, std):
    """White noise boxed with radius r (RR-like spatial correlation), rescaled to `std`."""
    n = im.box(rng.standard_normal(shape).astype(np.float32), r)
    return n * (std / max(float(n.std()), 1e-12))


def frames(rng, base, n, std, r=1):
    return [np.maximum(base + correlated_noise(rng, base.shape, r, std), 0) for _ in range(n)]


def sigma_from(sets, blur):
    s = [np.stack([im.box(y, blur) for y in st]).std(0) for st in sets]
    return im.maxf(np.maximum.reduce(s), blur + 1) * np.sqrt(2.0)


# ---- closed-form values quoted in the docs
check("k_sigma 1280x533, 3x3", abs(th.k_sigma((533, 1280), 1) - 4.84) < 0.01, f"{th.k_sigma((533, 1280), 1):.3f}")
check("k_sigma 3840x1600, 3x3", abs(th.k_sigma((1600, 3840), 1) - 5.26) < 0.01, f"{th.k_sigma((1600, 3840), 1):.3f}")
check("min_area 3x3 box, radius-1 opening", th.min_area(1, 1) == 25)
check("min_area flicker 5x5 box, radius-2 opening", th.min_area(2, 2) == 81)
check("edge threshold 0.6 removed, 3x3", abs(th.edge_threshold(0.6, 1) - 0.3054) < 1e-3, f"{th.edge_threshold(0.6, 1):.4f}")
q = th.preview_quant_sigma(np.array([[[0.0, 1.0, 100.0]]], np.float32), "pq")
check("PQ preview rounding finite and positive", bool(np.all(np.isfinite(q)) and np.all(q > 0)), str(q.ravel()))

rng = np.random.default_rng(1)
yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
base = (0.4 + 0.3 * np.sin(xx / 97.0) * np.cos(yy / 53.0)).astype(np.float32)

# ---- hard_dark: no false alarm on noise alone (on and off are the same setting)
false_px = 0
for seed in range(8):
    r_ = np.random.default_rng(100 + seed)
    on, off = frames(r_, base, 3, 0.03), frames(r_, base, 3, 0.03)
    sig = sigma_from([on, off], 1)
    res = det.hard_dark(on[0], off[0], sig, 1, "gi")
    false_px += res["metrics"]["qualified"]
check("hard_dark: no qualified region on pure noise (8 seeds)", false_px == 0, f"qualified {false_px}")

# ---- hard_dark: a black polygon with a sharp edge the off image does not have (the grass-card
# artifact of QA.md finding 1) is caught; a wide soft shadow of the same depth is not.
poly = (np.abs(xx - 400) + np.abs(yy - 260) < 30).astype(np.float32)
d = np.hypot(xx - 900, yy - 260) - 60
soft = np.clip(0.5 - d / 40.0, 0, 1)
on = [f * (1 - 0.95 * poly) * (1 - 0.95 * soft) for f in frames(rng, base, 3, 0.01)]
off = frames(rng, base, 3, 0.01)
sig = sigma_from([on, off], 1)
res = det.hard_dark(on[0], off[0], sig, 1, "gi")
hits = [g for g in res["regions"] if g["qualified"]]
check("hard_dark: sharp polygon flagged", any(abs((g["bbox"][2] + g["bbox"][3]) / 2 - 400) < 20 for g in hits),
      f"score {res['score']}‰")
check("hard_dark: 40-px soft shadow not flagged", not any(abs((g["bbox"][2] + g["bbox"][3]) / 2 - 900) < 40 for g in hits))
check("hard_dark: gate fails on the artifact", not res["pass"])

# ---- flicker: equal correlated noise in both sets passes; a flickering patch fails
def codes(ys):
    return [np.repeat(np.clip(y, 0, 1)[..., None], 3, -1) for y in ys]


nulls = []
for seed in range(12):
    r_ = np.random.default_rng(200 + seed)
    a, b = frames(r_, base, 3, 0.004, r=2), frames(r_, base, 3, 0.004, r=2)
    t = det.temporal(codes(a), codes(b))
    nulls.append((t["pass"], t["metrics"]["excess_area"], t["metrics"]["n_eff"]))
check("flicker: equal noise passes (12 seeds)", all(p for p, _, _ in nulls), str(nulls))
neff = nulls[0][2][0]
check("flicker: measured n_eff below the box area for correlated noise", 1.0 <= neff < 25.0, f"n_eff {neff}")
a, b = frames(rng, base, 3, 0.004, r=2), frames(rng, base, 3, 0.004, r=2)
patch = (np.abs(xx - 640) < 40) & (np.abs(yy - 260) < 40)
a = [y + patch * s for y, s in zip(a, (0.0, 0.02, -0.02))]
t = det.temporal(codes(a), codes(b))
check("flicker: a flickering 80x80 patch fails", not t["pass"] and t["metrics"]["excess_area"] > 0,
      f"excess {t['metrics']['excess_area']}‰")

# ---- same view: a whole-pixel camera pan is a move, the sub-pixel jitter is not
alb = np.clip(0.5 + 0.25 * np.sign(np.sin(xx / 7.0) * np.sin(yy / 11.0)) + 0.1 * rng.standard_normal((H, W)), 0, 1)
alb = np.round(alb * 255) / 255
jit = np.round(np.clip(0.5 * alb + 0.5 * np.roll(alb, 1, axis=1), 0, 1) * 255) / 255   # half-pixel shift
pan = np.roll(np.roll(alb, 3, axis=0), -5, axis=1)
s_j, s_p = det.view_shift(alb, jit)[:2], det.view_shift(alb, pan)[:2]
check("same_view: half-pixel jitter is the same view", max(abs(c) for c in s_j) < th.JITTER_PX, str(s_j))
check("same_view: a 3/-5 px pan is a move", max(abs(c) for c in s_p) >= 1, str(s_p))

# ---- scene motion: the jitter alone marks nothing, a moved block is marked
al3 = np.repeat(alb[..., None], 3, -1)
jt3 = np.repeat(jit[..., None], 3, -1)
mv = al3.copy()
mv[200:260, 600:660] = mv[200:260, 640:700]
mot_j = det.animated([al3, jt3], [], (H, W))
mot_m = det.animated([al3, mv], [], (H, W))
check("animated: jitter marks < 1% of px", mot_j.mean() < 0.01, f"{100 * mot_j.mean():.2f}%")
check("animated: a moved block is marked", mot_m[230, 630])
# A dithered input (the game's PCF sun mask differs on static pixels between any two frames)
# marks nothing by its noise alone; a shadow that moved still is.
rs = np.random.default_rng(7)
sm0 = np.clip(0.6 + rs.normal(0, 0.05, (H, W)), 0, 1).astype(np.float32)
sm1 = np.clip(0.6 + rs.normal(0, 0.05, (H, W)), 0, 1).astype(np.float32)
sm2 = sm1.copy()
sm2[300:360, 300:360] = 0.0
mot_d = det.animated([], [sm0, sm1], (H, W))
mot_s = det.animated([], [sm0, sm2], (H, W))
check("animated: a dithered sun mask marks < 1% of px", mot_d.mean() < 0.01, f"{100 * mot_d.mean():.2f}%")
check("animated: a moved shadow on a dithered mask is marked", mot_s[330, 330])

print(f"\n{len(FAILS)} failure(s)" if FAILS else "\nall passed")
sys.exit(1 if FAILS else 0)
