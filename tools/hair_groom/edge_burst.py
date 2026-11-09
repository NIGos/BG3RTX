"""Hair edge band vs core flicker of pan bursts (rr_hair_strand_aa judgement, docs/RTX_HAIR_PRIMARY.md 5n).

  py -3 tools/hair_groom/edge_burst.py <burst off> <burst aa> [--labels off,aa] [--crop out.png]
  py -3 tools/hair_groom/edge_burst.py <burst> ... --modes off,aa,aa,off,...   (still-pixel judgement, 5n he2)

Bursts with the RR inputs (pan_inputs=1). Hair region = class 5 (G-buffer RT2 alpha bits 0-2) in at least half of the
frames (temporal_burst.py's region). Edge band = the region's pixels within 2 render pixels of a non-region pixel (and
the non-region pixels within 2 of the region: the silhouette both sides); core = the region minus the band. For
rr_color (RR input, render size) and final (display size, masks scaled): flicker (temporal std / mean per pixel, mean
over the mask), mc (motion-compensated frame difference rms / mean, temporal_burst.warp with the RR motion vectors) and
hf (rms of x - box3(x) / mean, per frame), over the band, the core and the whole frame. --crop writes the burst-mean
final image (off | aa) around the edge band's densest region, x3.

Still pixels (5n audit, 2026-11-09): `flicker` and `mc` count animation. The band is the silhouette, so a character that
moves by a fraction of a pixel turns hair/background coverage into temporal variance, and mc adds the bilinear warp's
error and the disocclusions there. The `*_still` masks keep only the pixels whose summed RR motion over the burst is at
most STILL_PX (the game's MVs exclude the jitter, shaders/rt/hair_primary_velocity.hlsl, so a still pixel reads 0):
`still` = sqrt(mean over the mask of the temporal variance) / mean (ratio of means: a dark background pixel in the band
no longer weighs like a bright strand), `drift` = |mean of the first half - mean of the second half| / mean (RR history
still converging after a mode switch). judge() compares the bursts of each mode against the key-off repeats' spread.
"""
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.hair_groom.temporal_burst import box3, frames, load, lum, scale_mask, warp  # noqa: E402

# A displacement D (px) of an edge of contrast C over the burst adds about (C D)^2 / 12 of temporal variance (a linear
# sweep); the binary sub-pixel coverage we measure gives C^2 c (1 - c), C^2 / 6 averaged over the coverage c. At
# D = 1/4 px motion adds at most (1/192) / (1/6) ~ 3% of that variance: well below the 10-50% effect sought.
STILL_PX = 0.25
# Per-pixel variance from 16 frames has a relative sd of sqrt(2 / 15) (half that for the std); over N pixels with the
# RR output's ~3 x 3 spatial correlation, a 10% std change is 2 sigma of two bursts' sampling error at N ~ 240.
MIN_STILL_PX = 256
Z = 2.0   # two-sided ~95% on the key-off repeats' spread


def dilate(m, r):
    out = m.copy()
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            out |= np.roll(np.roll(m, dy, 0), dx, 1)
    return out


def still_mask(mvs, tau=STILL_PX):
    """Pixels whose summed motion over the burst (frames 1.., each frame's MV to its predecessor) is <= tau px."""
    path = sum(np.hypot(m[..., 0], m[..., 1]) for m in mvs[1:])
    return path <= tau


def still_stats(L, M):
    """Temporal std (ratio of means), mean and half-burst drift of the frames L (T, h, w) over the mask M."""
    n = int(M.sum())
    if n == 0:
        return {"px": 0, "still": float("nan"), "mean": float("nan"), "drift": float("nan")}
    x = L[:, M]
    mean = float(x.mean())
    h = len(L) // 2
    return {"px": n, "still": float(np.sqrt(x.var(0).mean()) / max(mean, 1e-8)), "mean": mean,
            "drift": float(abs(x[:h].mean() - x[h:].mean()) / max(mean, 1e-8))}


def analyse(d):
    j, F = frames(d)
    cls = [load(d, f["material"])[..., 3] & 7 for f in F]
    region = np.mean([c == 5 for c in cls], 0) >= 0.5
    band = dilate(region, 2) & dilate(~region, 2)
    core = region & ~band
    mvs = [load(d, f["rr_mvec"]) for f in F]
    still = still_mask(mvs)
    times = sorted({s["record_frame"]: s.get("time_ms", 0) for s in j["samples"]}.values())
    res = {"frame_ms": float(np.median(np.diff(times))) if len(times) > 1 else 0.0}
    for name in ("rr_color", "final"):
        L = np.stack([lum(load(d, f[name])) for f in F])
        h, w = L.shape[1:]
        k = w / region.shape[1]
        masks = {"band": band, "core": core, "frame": np.ones_like(region)}
        masks.update({m + "_still": v & still for m, v in list(masks.items())})
        masks = {m: (v if (h, w) == v.shape else scale_mask(v, (h, w))) for m, v in masks.items()}
        mean = L.mean(0)
        flick = L.std(0) / np.maximum(mean, 1e-4)
        dl = []
        for t in range(1, len(F)):
            mv = mvs[t] if k == 1 else scale_mask(mvs[t].reshape(*mvs[t].shape[:2], 2), (h, w)) * k
            dl.append(L[t] - warp(L[t - 1], mv))
        dl = np.stack(dl)
        hf = np.stack([x - box3(x) for x in L])
        res[name] = {}
        for m, M in masks.items():
            if not M.any():
                res[name][m] = dict(still_stats(L, M), flicker=float("nan"), mc=float("nan"), hf=float("nan"))
                continue
            res[name][m] = dict(still_stats(L, M), flicker=float(flick[M].mean()),
                                mc=float(np.sqrt((dl[:, M] ** 2).mean()) / L[:, M].mean()),
                                hf=float(np.sqrt((hf[:, M] ** 2).mean()) / L[:, M].mean()))
        if name == "final":
            res["_mean_img"] = np.mean([load(d, f["final"]) for f in F], 0)
            res["_band"] = masks["band"]
    return res


def judge(values, modes, metric="still", min_px=MIN_STILL_PX):
    """values: per burst {mask: {metric, px, mean, drift}} (one image); modes: 'off' / 'aa' per burst.

    Per mask: delta = mean(aa) / mean(off) - 1 and its noise sigma = sd(off) / mean(off) * sqrt(1/n_aa + 1/n_off)
    (the key-off repeats' spread: the only noise estimate that includes the scene's own burst-to-burst variation).
    INVALID if a burst has fewer than min_px still pixels in the band or core, or there are fewer than two off bursts;
    PASS if the band falls by more than Z sigma while the core and the band's mean (energy) stay within Z sigma; else FAIL.
    """
    off = [v for v, m in zip(values, modes) if m == "off"]
    aa = [v for v, m in zip(values, modes) if m == "aa"]
    out = {"n_off": len(off), "n_aa": len(aa), "problems": []}
    if len(off) < 2 or not aa:
        out["problems"].append("need >= 2 off bursts and >= 1 aa burst")
        out["verdict"] = "INVALID"
        return out
    for mask in ("band_still", "core_still"):
        if any(v[mask]["px"] < min_px for v in values):
            out["problems"].append(f"{mask}: a burst has < {min_px} still px")
    for mask in ("band_still", "core_still", "frame_still"):
        for key in (metric, "mean"):
            a = np.array([v[mask][key] for v in aa], float)
            o = np.array([v[mask][key] for v in off], float)
            mo = o.mean()
            sig = o.std(ddof=1) / mo * np.sqrt(1 / len(a) + 1 / len(o))
            out[f"{mask}.{key}"] = {"delta": float(a.mean() / mo - 1), "sigma": float(sig), "off": o.tolist(), "aa": a.tolist()}
    if out["problems"]:
        out["verdict"] = "INVALID"
        return out
    b, c, e = out[f"band_still.{metric}"], out[f"core_still.{metric}"], out["band_still.mean"]
    ok = b["delta"] < -Z * b["sigma"] and abs(c["delta"]) <= Z * c["sigma"] and abs(e["delta"]) <= Z * e["sigma"]
    out["verdict"] = "PASS" if ok else "FAIL"
    return out


def main(argv):
    labels, crop, modes = ["off", "aa"], None, None
    if "--labels" in argv:
        i = argv.index("--labels"); labels = argv[i + 1].split(","); argv = argv[:i] + argv[i + 2:]
    if "--crop" in argv:
        i = argv.index("--crop"); crop = argv[i + 1]; argv = argv[:i] + argv[i + 2:]
    if "--modes" in argv:
        i = argv.index("--modes"); modes = argv[i + 1].split(","); argv = argv[:i] + argv[i + 2:]
        assert len(modes) == len(argv), "one mode per burst"
        labels = [f"{m}-{k}" for k, m in enumerate(modes, 1)]
    R = []
    for a in argv:
        R.append(analyse(Path(a)))
        print(f"analysed {len(R)}/{len(argv)}: {a} (frame interval {R[-1]['frame_ms']:.1f} ms)", flush=True)
    out = {}
    for lab, r in zip(labels, R):
        out[lab] = {n: r[n] for n in ("rr_color", "final")}
        for n in ("rr_color", "final"):
            print(lab, n, "  ".join(f"{m}: flicker {v['flicker']:.4f} mc {v['mc']:.4f} hf {v['hf']:.4f} still {v['still']:.4f} "
                                    f"drift {v['drift']:.4f} mean {v['mean']:.4f} ({v['px']} px)"
                                    for m, v in r[n].items()), flush=True)
    if modes:
        for n in ("final", "rr_color"):
            j = judge([r[n] for r in R], modes)
            out["judge_" + n] = j
            print(f"judge {n}: {j['verdict']} {'; '.join(j['problems'])}")
            for k, v in j.items():
                if isinstance(v, dict):
                    print(f"  {k}: aa/off {v['delta']:+.1%} (sigma {v['sigma']:.1%})")
        # Positive control: mode 2 lowers the single-frame HF of the RR input on the band (he1 04:02: -13 to -15%).
        hf = [(r["rr_color"]["band"]["hf"], m) for r, m in zip(R, modes)]
        off_hf = float(np.mean([h for h, m in hf if m == "off"]))
        out["aa_active"] = all(h < off_hf for h, m in hf if m == "aa")
        print(f"mode 2 active in every aa burst (rr_color band hf below the off mean {off_hf:.4f}): {out['aa_active']}")
    elif len(R) == 2:
        for n in ("rr_color", "final"):
            for m in ("band", "core", "frame", "band_still", "core_still"):
                a, b = R[0][n][m], R[1][n][m]
                print(f"{labels[1]}/{labels[0]} {n} {m}: flicker {b['flicker'] / a['flicker'] - 1:+.1%} mc {b['mc'] / a['mc'] - 1:+.1%} "
                      f"hf {b['hf'] / a['hf'] - 1:+.1%} still {b['still'] / a['still'] - 1:+.1%} mean {b['mean'] / a['mean'] - 1:+.2%}")
    if crop and len(R) >= 2:
        from PIL import Image
        band = R[0]["_band"] | R[1]["_band"]
        # the densest 160 x 120 window of edge-band pixels (coarse search)
        best, at = -1, (0, 0)
        h, w = band.shape
        for y0 in range(0, h - 120, 20):
            for x0 in range(0, w - 160, 20):
                s = int(band[y0:y0 + 120, x0:x0 + 160].sum())
                if s > best:
                    best, at = s, (y0, x0)
        y0, x0 = at
        tiles = [np.kron(np.clip(r["_mean_img"][y0:y0 + 120, x0:x0 + 160], 0, 1), np.ones((3, 3, 1))) for r in R[:2]]
        img = np.concatenate([tiles[0], np.ones((tiles[0].shape[0], 12, 3)), tiles[1]], 1)
        Image.fromarray((img * 255 + 0.5).astype(np.uint8)).save(crop)
        print("crop", crop, "region", (y0, x0, 120, 160))
    return out


if __name__ == "__main__":
    main(sys.argv[1:])
