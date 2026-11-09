"""Hair edge band vs core flicker of pan bursts (rr_hair_strand_aa judgement, docs/RTX_HAIR_PRIMARY.md 5n).

  py -3 tools/hair_groom/edge_burst.py <burst off> <burst aa> [--labels off,aa] [--crop out.png]

Bursts with the RR inputs (pan_inputs=1). Hair region = class 5 (G-buffer RT2 alpha bits 0-2) in at least half of the
frames (temporal_burst.py's region). Edge band = the region's pixels within 2 render pixels of a non-region pixel (and
the non-region pixels within 2 of the region: the silhouette both sides); core = the region minus the band. For
rr_color (RR input, render size) and final (display size, masks scaled): flicker (temporal std / mean per pixel, mean
over the mask), mc (motion-compensated frame difference rms / mean, temporal_burst.warp with the RR motion vectors) and
hf (rms of x - box3(x) / mean, per frame), over the band, the core and the whole frame. --crop writes the burst-mean
final image (off | aa) around the edge band's densest region, x3.
"""
import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.hair_groom.temporal_burst import box3, frames, load, lum, scale_mask, warp  # noqa: E402


def dilate(m, r):
    out = m.copy()
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            out |= np.roll(np.roll(m, dy, 0), dx, 1)
    return out


def analyse(d):
    j, F = frames(d)
    cls = [load(d, f["material"])[..., 3] & 7 for f in F]
    region = np.mean([c == 5 for c in cls], 0) >= 0.5
    band = dilate(region, 2) & dilate(~region, 2)
    core = region & ~band
    mvs = [load(d, f["rr_mvec"]) for f in F]
    res = {}
    for name in ("rr_color", "final"):
        L = np.stack([lum(load(d, f[name])) for f in F])
        h, w = L.shape[1:]
        k = w / region.shape[1]
        masks = {"band": band, "core": core, "frame": np.ones_like(region)}
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
            res[name][m] = {"px": int(M.sum()), "flicker": float(flick[M].mean()),
                            "mc": float(np.sqrt((dl[:, M] ** 2).mean()) / L[:, M].mean()),
                            "hf": float(np.sqrt((hf[:, M] ** 2).mean()) / L[:, M].mean()),
                            "mean": float(L[:, M].mean())}
        if name == "final":
            res["_mean_img"] = np.mean([load(d, f["final"]) for f in F], 0)
            res["_band"] = masks["band"]
    return res


def main(argv):
    labels, crop = ["off", "aa"], None
    if "--labels" in argv:
        i = argv.index("--labels"); labels = argv[i + 1].split(","); argv = argv[:i] + argv[i + 2:]
    if "--crop" in argv:
        i = argv.index("--crop"); crop = argv[i + 1]; argv = argv[:i] + argv[i + 2:]
    R = [analyse(Path(a)) for a in argv]
    out = {}
    for lab, r in zip(labels, R):
        out[lab] = {n: r[n] for n in ("rr_color", "final")}
        for n in ("rr_color", "final"):
            print(lab, n, "  ".join(f"{m}: flicker {v['flicker']:.4f} mc {v['mc']:.4f} hf {v['hf']:.4f} mean {v['mean']:.4f} ({v['px']} px)"
                                    for m, v in r[n].items()), flush=True)
    if len(R) == 2:
        for n in ("rr_color", "final"):
            for m in ("band", "core", "frame"):
                a, b = R[0][n][m], R[1][n][m]
                print(f"{labels[1]}/{labels[0]} {n} {m}: flicker {b['flicker'] / a['flicker'] - 1:+.1%} mc {b['mc'] / a['mc'] - 1:+.1%} "
                      f"hf {b['hf'] / a['hf'] - 1:+.1%} mean {b['mean'] / a['mean'] - 1:+.2%}")
    if crop and len(R) == 2:
        from PIL import Image
        band = R[0]["_band"] | R[1]["_band"]
        ys, xs = np.nonzero(band)
        # the densest 160 x 120 window of edge-band pixels (coarse search)
        best, at = -1, (0, 0)
        h, w = band.shape
        for y0 in range(0, h - 120, 20):
            for x0 in range(0, w - 160, 20):
                s = int(band[y0:y0 + 120, x0:x0 + 160].sum())
                if s > best:
                    best, at = s, (y0, x0)
        y0, x0 = at
        tiles = [np.kron(np.clip(r["_mean_img"][y0:y0 + 120, x0:x0 + 160], 0, 1), np.ones((3, 3, 1))) for r in R]
        img = np.concatenate([tiles[0], np.ones((tiles[0].shape[0], 12, 3)), tiles[1]], 1)
        Image.fromarray((img * 255 + 0.5).astype(np.uint8)).save(crop)
        print("crop", crop, "region", (y0, x0, 120, 160))
    return out


if __name__ == "__main__":
    main(sys.argv[1:])
