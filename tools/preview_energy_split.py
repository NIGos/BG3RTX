"""Where a scene-colour change sits: animated lava/emitter pixels vs the rest (LOCALIZATION ONLY).

    py -3 -m tools.preview_energy_split --root <captures dir> --off <stamp> <stamp> name=<stamp> [...]
    e.g. S6 m4p3 (2026-11-01, traces in the test tree's captures):
    py -3 -m tools.preview_energy_split --root ...\\test-main\\captures --off 20261101-112913 20261101-113135 \\
        gi=20261101-112920 sun=20261101-112929 inst=20261101-112945 emoffa=20261101-113012 on=20261101-113040 \\
        emoffb=20261101-113107

Decodes each trace's PNG preview of the scene colour (pass 5c3fdabc slot 0; layer/dump.cpp writes HDR as
t = (v / (1 + v))^(1/2.2), so v = s / (1 - s), s = t^2.2) back to linear luminance and splits the change against
the mean of the OFF traces into "lava" pixels (red-dominant and brighter than the frame median in the OFF mean,
R > 3 B, dilated 4 px because the flow moves) and the rest. 8-bit previews: this localizes, it never replaces the
float mean of dumps.json as the energy number (AGENTS.md "Measuring correctly"); the decoded frame mean is
printed next to the float mean so the decoding error is visible. Also prints the glow buffer (pass 48eedb24, the
bloom source the lava and fires write before the lighting) as a measure of the emitters' animation state.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image

KY = np.array([0.2126, 0.7152, 0.0722])


def dump_of(d: dict, fs: str) -> dict | None:
    return next((v for v in d.values() if v["pass_fs"].startswith(fs) and v["slot"] == 0), None)


def load(trace: Path) -> tuple[np.ndarray, float, float]:
    d = json.loads((trace / "dumps.json").read_text(encoding="utf-8"))
    v = dump_of(d, "5c3fdabc")
    if not v:
        raise SystemExit(f"{trace}: no 5c3fdabc dump")
    t = np.asarray(Image.open(trace / v["png"]).convert("RGB")).astype(np.float64) / 255.0
    s = np.clip(t, 0.0, 0.999) ** 2.2
    g = dump_of(d, "48eedb24")
    return s / (1.0 - s), float(np.dot(v["mean"][:3], KY)), float(np.dot(g["mean"][:3], KY)) if g else float("nan")


def main() -> int:
    a = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    a.add_argument("--root", type=Path, required=True)
    a.add_argument("--off", nargs=2, required=True, metavar="STAMP")
    a.add_argument("shots", nargs="*", metavar="NAME=STAMP")
    args = a.parse_args()
    shots = [("off-a", args.off[0]), ("off-b", args.off[1])] + [tuple(s.split("=", 1)) for s in args.shots]
    rgb, fmean, glow = {}, {}, {}
    for name, stamp in shots:
        rgb[name], fmean[name], glow[name] = load(args.root / stamp)
    off_rgb = (rgb["off-a"] + rgb["off-b"]) / 2
    off_l = off_rgb @ KY
    lava = (off_rgb[..., 0] > 3 * off_rgb[..., 2]) & (off_l > np.median(off_l))
    grown = lava.copy()
    for dy in range(-4, 5):
        for dx in range(-4, 5):
            grown |= np.roll(np.roll(lava, dy, 0), dx, 1)
    lava = grown
    tot, rest_tot = off_l.sum(), off_l[~lava].sum()
    fo, go = (fmean["off-a"] + fmean["off-b"]) / 2, (glow["off-a"] + glow["off-b"]) / 2
    print(f"lava mask: {lava.mean():.1%} of the pixels, {off_l[lava].sum() / tot:.1%} of the OFF luminance")
    print("change vs the OFF mean, in % of the OFF frame luminance; 'rest rel' relative to the rest's own luminance")
    print(f"{'shot':8s} {'float':>8s} {'decoded':>8s} {'lava':>8s} {'rest':>8s} {'rest rel':>9s} {'glow':>8s}")
    for name, _ in shots:
        d = rgb[name] @ KY - off_l
        print(f"{name:8s} {fmean[name] / fo - 1:+8.3%} {d.sum() / tot:+8.3%} {d[lava].sum() / tot:+8.3%} "
              f"{d[~lava].sum() / tot:+8.3%} {d[~lava].sum() / rest_tot:+9.3%} {glow[name] / go - 1:+8.2%}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
