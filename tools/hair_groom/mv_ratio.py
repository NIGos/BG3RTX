"""Strand motion vectors against their surroundings, from pan bursts with the RR inputs (docs/RTX_HAIR_PRIMARY.md 5x).

  py -3 tools/hair_groom/mv_ratio.py <pan dir> [<pan dir> ...] [--labels a,b]

Per burst (every other frame, or each frame with material of a pan_edge + pan_mvec burst; the burst.json reader of
temporal_burst.py), in render pixels of `rr_mvec` (edge bursts are half-size grids: the 6 and 60 px radii double):
  camera-only share = class-5 pixels (material alpha bits 0-2) whose motion vector is within 0.02 px of the static
                      background's median (non-hair pixels more than 60 px from any hair): the motion a strand pixel
                      gets when nothing wrote the strand's own motion;
  ring ratio        = median |mv| of the class-5 pixels / median |mv| of the non-hair pixels within 6 px of them
                      (head, neck, clothes of the same characters);
  background |mv|   = the static-camera check (above 0.05 px the camera moved and the shares mean nothing).
Measured 2026-11-10 on the hs1-s8-static-hair bursts: strands 0.83-0.86 camera-only, ratio 0-0.54; cards 0.055-0.068,
ratio 0.56-1.02.
"""
import sys
from pathlib import Path

import numpy as np
from scipy import ndimage as ndi

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.hair_groom.temporal_burst import frames, load  # noqa: E402

CAMERA_ONLY_PX = 0.02


def analyse(d: Path, step=2):
    _, F = frames(d)
    rows = []
    # Full bursts: every other frame. Edge bursts (pan_edge=1, pan_mvec=1: half-size, material in frames 1 and 8 only):
    # every frame with both buffers.
    have = [t for t in range(len(F)) if "rr_mvec" in F[t] and "material" in F[t]]
    if len(have) == len(F):
        have = have[1::step]
    for t in have:
        mv = load(d, F[t]["rr_mvec"])
        cls = load(d, F[t]["material"])[..., 3] & 7
        hair = cls == 5
        if not hair.any():
            continue
        ring = ndi.binary_dilation(hair, iterations=6) & ~hair & (cls != 0)
        bg = ~ndi.binary_dilation(hair, iterations=60) & (cls != 0)
        cam = np.median(mv[bg], 0) if bg.any() else np.zeros(2, np.float32)
        a = np.hypot(mv[..., 0], mv[..., 1])
        dh = np.hypot(*(mv[hair] - cam).T)
        ring_med = float(np.median(a[ring])) if ring.any() else float("nan")
        rows.append((float((dh < CAMERA_ONLY_PX).mean()), float(np.median(a[hair])), ring_med,
                     float(np.hypot(*cam)), int(hair.sum())))
    r = np.array(rows, dtype=np.float64)
    m = r.mean(0)
    return {"frames": len(rows), "camera_only_share": m[0], "hair_mv": m[1], "ring_mv": m[2],
            "ratio": m[1] / m[2] if m[2] > 0 else float("nan"), "background_mv": m[3], "hair_px": m[4]}


def main(argv):
    labels = None
    if "--labels" in argv:
        i = argv.index("--labels"); labels = argv[i + 1].split(","); argv = argv[:i] + argv[i + 2:]
    dirs = [Path(a) for a in argv]
    labels = labels or [p.name for p in dirs]
    out = []
    for i, (l, d) in enumerate(zip(labels, dirs)):
        r = analyse(d)
        out.append(r)
        print(f"[{i + 1}/{len(dirs)}] {l}: hair px {r['hair_px']:.0f}, camera-only share {r['camera_only_share']:.3f}, "
              f"hair |mv| {r['hair_mv']:.3f} / ring {r['ring_mv']:.3f} = ratio {r['ratio']:.2f}, "
              f"background |mv| {r['background_mv']:.4f} ({r['frames']} frames)", flush=True)
    return out


if __name__ == "__main__":
    main(sys.argv[1:])
