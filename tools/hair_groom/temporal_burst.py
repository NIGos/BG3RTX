"""Temporal grain of strand vs card hair from pan bursts (pan_capture + pan_inputs: 16 consecutive frames with RR
inputs; layer/dump.cpp pan_dir, burst.json).

  py -3 tools/hair_groom/temporal_burst.py <pan dir> [<pan dir> ...] [--labels a,b] [--crop out.png]

Inside the hair region (class 5 of the burst's `material` RGBA8 = G-buffer RT2, alpha bits 0-2, in at least half
of the frames), per burst:
  flicker  = mean over pixels of the temporal std / temporal mean of luminance (raw, no warp);
  mc       = rms of (I_t - I_{t-1} warped by the RR motion vectors) / mean (motion-compensated frame difference;
             bilinear fetch at p - mv, mv = rr_mvec in pixels, current minus previous);
  hf       = spatial rms(x - box3(x)) / mean per frame, averaged.
For rr_color (pre-RR, render size, B10G11R11) and final (RR output after the native tonemap, display size,
A2B10G10R10; mask and MVs scaled by the display/render ratio).
"""
import json
import sys
from pathlib import Path

import numpy as np


def uf(bits, mbits):
    e = (bits >> mbits) & 31
    m = bits & ((1 << mbits) - 1)
    v = np.where(e == 0, m / float(1 << mbits) * 2.0 ** -14, (1 + m / float(1 << mbits)) * 2.0 ** (e.astype(np.float64) - 15))
    return np.where(e == 31, 0.0, v).astype(np.float32)


def load(d, s):
    raw = np.fromfile(d / s["file"], np.uint32 if s["format"] in (122, 64, 37) else np.uint16)
    w, h = s["width"], s["height"]
    f = s["format"]
    if f == 122:    # B10G11R11_UFLOAT
        x = raw.reshape(h, w)
        return np.stack([uf(x & 0x7FF, 6), uf((x >> 11) & 0x7FF, 6), uf((x >> 22) & 0x3FF, 5)], -1)
    if f == 64:     # A2B10G10R10_UNORM
        x = raw.reshape(h, w)
        return np.stack([(x & 1023), (x >> 10) & 1023, (x >> 20) & 1023], -1).astype(np.float32) / 1023.0
    if f == 37:     # R8G8B8A8_UNORM
        return raw.view(np.uint8).reshape(h, w, 4)
    if f == 83:     # R16G16_SFLOAT
        return raw.view(np.float16).reshape(h, w, 2).astype(np.float32)
    raise ValueError(f"format {f}")


def lum(c):
    return 0.2126 * c[..., 0] + 0.7152 * c[..., 1] + 0.0722 * c[..., 2]


def box3(x):
    p = np.pad(x, 1, mode="edge")
    h, w = x.shape
    return sum(p[i:i + h, j:j + w] for i in range(3) for j in range(3)) / 9.0


def scale_mask(m, shape):
    return m[(np.arange(shape[0]) * m.shape[0] // shape[0])][:, (np.arange(shape[1]) * m.shape[1] // shape[1])]


def warp(prev, mv):
    """prev sampled at p - mv (bilinear, clamped)."""
    h, w = prev.shape
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float32)
    x = np.clip(xs - mv[..., 0], 0, w - 1.001)
    y = np.clip(ys - mv[..., 1], 0, h - 1.001)
    x0, y0 = x.astype(int), y.astype(int)
    fx, fy = x - x0, y - y0
    return ((prev[y0, x0] * (1 - fx) + prev[y0, x0 + 1] * fx) * (1 - fy) +
            (prev[y0 + 1, x0] * (1 - fx) + prev[y0 + 1, x0 + 1] * fx) * fy)


def frames(d):
    j = json.loads((d / "burst.json").read_text())
    by = {}
    for s in j["samples"]:
        by.setdefault(s["record_frame"], {})[s["name"]] = s
    return j, [by[k] for k in sorted(by)]


def analyse(d: Path):
    j, F = frames(d)
    cls = [load(d, f["material"])[..., 3] & 7 for f in F]
    share = np.mean([c == 5 for c in cls], 0)
    region = share >= 0.5
    res = {"frames": len(F), "valid_transport": j.get("valid_transport"), "hair_px": int(region.sum()),
           "class5_share_in_region": float(share[region].mean())}
    mvs = [load(d, f["rr_mvec"]) for f in F]
    for name in ("rr_color", "final"):
        L = np.stack([lum(load(d, f[name])) for f in F])
        h, w = L.shape[1:]
        M = region if (h, w) == region.shape else scale_mask(region, (h, w))
        k = w / region.shape[1]
        mean = L.mean(0)
        flick = (L.std(0) / np.maximum(mean, 1e-4))[M]
        mc = []
        for t in range(1, len(F)):
            mv = mvs[t] if k == 1 else scale_mask(mvs[t].reshape(*mvs[t].shape[:2], 2), (h, w)) * k
            dlt = (L[t] - warp(L[t - 1], mv))[M]
            mc.append(np.sqrt((dlt ** 2).mean()) / L[t][M].mean())
        hf = [np.sqrt(((x - box3(x))[M] ** 2).mean()) / x[M].mean() for x in L]
        res[name] = {"flicker": float(flick.mean()), "flicker_p90": float(np.percentile(flick, 90)),
                     "mc": float(np.mean(mc)), "hf": float(np.mean(hf)), "mean": float(L[:, M].mean())}
        if name == "final":
            res["_final0"] = load(d, F[len(F) // 2]["final"])
            res["_box"] = np.argwhere(M)
    mvh = np.stack([np.hypot(m[..., 0], m[..., 1])[region] for m in mvs])
    res["mv_px_median"] = float(np.median(mvh))
    return res


def main(argv):
    labels, crop = None, None
    if "--labels" in argv:
        i = argv.index("--labels"); labels = argv[i + 1].split(","); argv = argv[:i] + argv[i + 2:]
    if "--crop" in argv:
        i = argv.index("--crop"); crop = argv[i + 1]; argv = argv[:i] + argv[i + 2:]
    dirs = [Path(a) for a in argv]
    labels = labels or [p.name for p in dirs]
    R = []
    for l, d in zip(labels, dirs):
        r = analyse(d)
        R.append(r)
        print(f"{l}: frames {r['frames']} valid_transport {r['valid_transport']} hair px {r['hair_px']} "
              f"class-5 share {r['class5_share_in_region']:.3f} |mv| median {r['mv_px_median']:.2f} px", flush=True)
        for n in ("rr_color", "final"):
            s = r[n]
            print(f"  {n:9s} flicker {s['flicker']:.4f} (p90 {s['flicker_p90']:.4f})  mc-diff {s['mc']:.4f}  "
                  f"HF {s['hf']:.4f}  mean {s['mean']:.4f}", flush=True)
    if crop and len(R) >= 2:
        from PIL import Image
        tiles = []
        for r in R[:2]:
            b = r["_box"]
            y0, x0 = b.min(0); y1, x1 = b.max(0)
            tiles.append(r["_final0"][y0:y1 + 1, x0:x1 + 1])
        hh = max(t.shape[0] for t in tiles)
        out = np.zeros((hh, sum(t.shape[1] for t in tiles) + 8, 3), np.float32)
        x = 0
        for t in tiles:
            out[:t.shape[0], x:x + t.shape[1]] = t
            x += t.shape[1] + 8
        Image.fromarray((np.clip(out, 0, 1) * 255 + 0.5).astype(np.uint8)).save(crop)
        print("crop", crop)
    return R


if __name__ == "__main__":
    main(sys.argv[1:])
