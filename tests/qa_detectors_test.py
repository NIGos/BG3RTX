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

# ---- hard_dark energy bound: the image = floor (everything but the effect's light) + the
# effect's light x visibility. Any visibility in [0, 1] keeps every pixel at or above the floor.
def cx(g):
    return (g["bbox"][2] + g["bbox"][3]) / 2


poly = (np.abs(xx - 400) + np.abs(yy - 260) < 30).astype(np.float32)
d = np.hypot(xx - 900, yy - 260) - 60
soft = np.clip(0.5 - d / 40.0, 0, 1)
amb = 0.25 * base                 # the floor: the light the effect does not own
own = base - amb                  # the effect's own light
psf = 1.0                         # the pipeline's spread: white noise and edges boxed with radius 1


def render(vis, extra=1.0, n=3, std=0.01):
    return [np.maximum((amb * extra + own * vis) + correlated_noise(rng, base.shape, 1, std), 0) for _ in range(n)]


vis = im.box((1 - poly) * (1 - soft), int(psf))   # a black polygon and a soft shadow, as rendered
on, off, flo = render(vis), render(np.ones_like(base)), render(np.zeros_like(base))
sig = sigma_from([on, off], 1)
p_sig = th.psf_sigma([y - np.mean(on, 0) for y in on])
res = det.hard_dark(on[0], off[0], sig, 1, "rt_shadows", floor=flo[0], psf_sigma=p_sig)
check("psf_sigma: boxed (r=1) noise measured as a spread of 0.5-1.5 px", 0.5 < p_sig < 1.5, f"{p_sig:.2f} px")
check("hard_dark gate: a physical umbra (all of its own light gone) passes", res["pass"] and res["gate"],
      f"score {res['score']}‰")
sigr = [g for g in res["regions"] if g["kind"] == "signature"]
pg = [g for g in sigr if abs(cx(g) - 400) < 20]
sg = [g for g in sigr if abs(cx(g) - 900) < 40]
check("hard_dark review: the polygon is step-like and at its floor", bool(pg) and pg[0]["step_like"] and pg[0]["black"] > 0.5,
      str(pg[:1]))
check("hard_dark review: the 40-px soft shadow is not step-like", all(g["step_like"] is False for g in sg), str(sg[:1]))

# An effect that also removes light it does not own (here the ambient under a patch) goes below
# the floor: an energy violation, the gate fails there and only there.
patch = ((np.abs(xx - 640) < 25) & (np.abs(yy - 120) < 25)).astype(np.float32)
on_bad = [y * (1 - patch) + patch * 0.2 * amb for y in on]   # all its own light and 80% of the ambient
res = det.hard_dark(on_bad[0], off[0], sig, 1, "rt_shadows", floor=flo[0], psf_sigma=p_sig)
eg = [g for g in res["regions"] if g["kind"] == "energy"]
check("hard_dark gate: light removed below the floor fails", not res["pass"] and any(abs(cx(g) - 640) <= 25 for g in eg),
      f"score {res['score']}‰, {len(eg)} region(s)")
check("hard_dark gate: nothing else below the floor", all(abs(cx(g) - 640) <= 25 for g in eg), str([(g["bbox"], g["area"]) for g in eg]))

# No false alarm on noise alone: on and floor differ only by noise where vis = 0 everywhere.
false_e = 0
for seed in range(8):
    rng = np.random.default_rng(100 + seed)
    on0, fl0 = render(np.zeros_like(base), std=0.03), render(np.zeros_like(base), std=0.03)
    s0 = sigma_from([on0, fl0], 1)
    false_e += det.hard_dark(on0[0], render(np.ones_like(base))[0], s0, 1, "gi", floor=fl0[0])["metrics"]["energy_regions"]
check("hard_dark gate: no energy region on pure noise (8 seeds)", false_e == 0, f"regions {false_e}")
res = det.hard_dark(on[0], off[0], sig, 1, "gi")
check("hard_dark without a floor is review only", res["gate"] is False and res["threshold"] is None)
rng = np.random.default_rng(1)

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

# ---- tlas_front: alpha-tested TLAS geometry in front of the raster (rt_debug=8), through the
# full code chain: RGBA8 code, x0.5 into the diffuse, the 8-bit Reinhard + gamma preview, x2.
def view8(l, kind):
    """l: log2(front / tolerance) (0 where not in front); kind 2 alpha, 1 opaque, 0 none."""
    def chain(x):
        x = np.round(np.clip(x, 0, 1) * 255) / 255                     # rtVis RGBA8
        lin = 0.5 * x                                                 # rt_debug.hlsl writes c * 0.5
        p = np.round(np.power(lin / (1 + lin), 1 / 2.2) * 255) / 255   # dump.cpp preview
        t = np.clip(p ** 2.2, 0, 0.9995)
        return 2 * (t / (1 - t))                                      # decode.py, then x 2 (analyze)
    g = np.where(kind == 2, 1.0, np.where(kind == 1, 0.5, 0.0))
    return np.stack([chain(l / 8.0), chain(g), chain(np.full_like(l, 0.25))], -1)


rng = np.random.default_rng(7)
card = (np.abs(xx - 300) < 20) & (np.abs(yy - 200) < 12)          # a grass card the trace keeps solid
wall = (np.abs(xx - 900) < 40) & (np.abs(yy - 300) < 40)          # a cut-away wall (opaque)
views = []
for t in range(3):
    l = np.zeros((H, W), np.float32)
    kind = np.zeros((H, W), np.int32)
    l[card] = 2.0 + 0.1 * rng.standard_normal(int(card.sum()))     # 4 tolerances in front, every trace
    kind[card] = 2
    l[wall] = 5.0
    kind[wall] = 1
    # Stochastic alpha at the cutout edges: in front in one trace of three, at random px.
    flick = rng.random((H, W)) < 0.002
    l[flick] = rng.uniform(0.1, 3.0, int(flick.sum()))
    kind[flick] = 2
    views.append(view8(l, kind))
tf, lm = det.tlas_front(views)
ag, op = tf["alpha"], tf["opaque"]
check("tlas_front: the solid grass card fails the gate", not ag["pass"] and any(abs(cx(g) - 300) < 5 for g in ag["regions"]),
      f"{ag['score']}‰, {len(ag['regions'])} region(s)")
check("tlas_front: stochastic alpha flicker (1 of 3 traces) is not significant", len(ag["regions"]) == 1,
      str([(g["bbox"], g["area"]) for g in ag["regions"]]))
check("tlas_front: the opaque cut-away wall is review only", op.get("gate") is False and any(abs(cx(g) - 900) < 5 for g in op["regions"]))
check("tlas_front: the front distance is recovered (about 4 tolerances)",
      all(3.0 < g["front_tol"] < 5.0 for g in ag["regions"]), str([g["front_tol"] for g in ag["regions"]]))
clean = [view8(np.zeros((H, W), np.float32), np.zeros((H, W), np.int32)) for _ in range(3)]
check("tlas_front: nothing in front passes", det.tlas_front(clean)[0]["alpha"]["pass"])

print(f"\n{len(FAILS)} failure(s)" if FAILS else "\nall passed")
sys.exit(1 if FAILS else 0)
