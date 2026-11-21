"""Output-grade flicker of three K8 pan bursts (OFF-a, ON, OFF-b): r3-d3-jitter's metrics (docs/progress/
claude-r3-core-live.md), generalised. Masks come from the OFF windows' A (the rr_post_ac alpha).

    py -3 -I tools/live_batches_staged/r5_ultra_shift/analyze_jitter.py <pan-dir off-a> <pan-dir on> <pan-dir off-b> <out.json>

Reported per state (x 1000, luma of the output grade `final`):
  bg_absdiff_by_C_grad: frame-to-frame |diff| in the blurred background (A < 0.95), by C-gradient quantile bins
  blurred_x / blurred_y / blurred_flat: fully blurred (A < 0.05) pixels with an x / y C gradient, and flat ones
  in_focus_control: A > 0.999 (animation reference); dof_transition_std: per-pixel std over the burst, A 0.05-0.95
  final_mean: whole-image output mean (energy reference next to the float traces)
"""
import json
import sys
from pathlib import Path

import numpy as np


def load(d):
    d = Path(d)
    meta = json.loads((d / "burst.json").read_text())
    frames = {}
    for s in meta["samples"]:
        raw = np.fromfile(d / s["file"], dtype=np.uint8)
        w, h = s["width"], s["height"]
        if s["format"] == 64:   # A2B10G10R10_UNORM
            u = raw.view(np.uint32).reshape(h, w)
            v = np.stack([(u >> (10 * i)) & 1023 for i in range(3)], -1) / 1023.0
        elif s["format"] == 97:   # RGBA16F
            v = raw.view(np.float16).reshape(h, w, 4).astype(np.float32)
        else:
            continue
        frames.setdefault(s["record_frame"], {})[s["name"]] = v
    return [frames[k] for k in sorted(frames) if "final" in frames[k] and "rr_post_ac" in frames[k]]


def lum(v):
    return v[..., 0] * .2126 + v[..., 1] * .7152 + v[..., 2] * .0722


def main(dirs, out):
    data = dict(zip(("off-a", "on", "off-b"), (load(d) for d in dirs)))
    H, W = data["off-a"][0]["final"].shape[:2]
    h, w = data["off-a"][0]["rr_post_ac"].shape[:2]
    yy = (np.arange(H)[:, None] * h // H).repeat(W, 1)
    xx = (np.arange(W)[None, :] * w // W).repeat(H, 0)
    off = [f for k in ("off-a", "off-b") for f in data[k]]
    Am = np.mean([f["rr_post_ac"][..., 3] for f in off], 0)
    Cm = np.mean([lum(f["rr_post_ac"]) for f in off], 0)
    cy, cx = np.gradient(Cm)
    mag = np.hypot(cx, cy)
    A, GC = Am[yy, xx], mag[yy, xx]
    full = Am < 0.05
    sel = full & (mag > np.quantile(mag[full], .5)) & (mag < np.quantile(mag[full], .99))
    masks = {"blurred_x": (sel & (np.abs(cx) > 3 * np.abs(cy)))[yy, xx],
             "blurred_y": (sel & (np.abs(cy) > 3 * np.abs(cx)))[yy, xx],
             "blurred_flat": (full & (mag < np.quantile(mag[full], .2)))[yy, xx],
             "in_focus_control": A > .999}
    bg = A < 0.95
    q = np.quantile(GC[bg], [0, .5, .8, .95, .99, 1])
    res = {"pixels": {k: int(m.sum()) for k, m in masks.items()}, "states": {}}
    for k, v in data.items():
        L = np.stack([lum(f["final"]) for f in v])
        d = np.abs(np.diff(L, axis=0)).mean(0) * 1e3
        r = {name: round(float(d[m].mean()), 4) for name, m in masks.items()}
        r["bg_absdiff_by_C_grad"] = [round(float(d[bg & (GC >= lo) & (GC <= hi)].mean()), 4) for lo, hi in zip(q[:-1], q[1:])]
        r["dof_transition_std"] = round(float(L.std(0)[(A > .05) & (A < .95)].mean()) * 1e3, 4)
        r["final_mean"] = round(float(L.mean()), 5)
        r["frames"] = len(v)
        res["states"][k] = r
    print(json.dumps(res, indent=1))
    Path(out).write_text(json.dumps(res, indent=1) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main(sys.argv[1:4], sys.argv[4])
