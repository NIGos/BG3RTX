"""Float energy of the character mask per shot (dialogue_shadows batch, docs/DIALOGUE_SHADOWS.md).

    py -3 tools/live_batches/dialogue_shadows/char_energy.py --root <captures dir> shot-off-a=<stamp> shot-noao-on=<stamp> ...

For each trace: the scene colour before post (pass 5c3fdabc) from its float32 preview-size dump
(*_p.f32, dump_preview_f32=1, layer/dump.cpp: the same nearest texels as the PNG previews), the
character mask from the G-buffer RT2 alpha preview (class = round(a 255) & 7 in {3 skin, 5 hair}; the
8-bit class value is exact), and the float means of luminance (0.2126 r + 0.7152 g + 0.0722 b) over the
mask and over the frame. Never a PNG decode of the colour (AGENTS.md "Measuring correctly").
Each ON shot is compared with the mean of its nearest OFF shot (label containing "-off-") before and
after it in the argument order; the OFF spread is the relative standard deviation of the OFF shots'
mask means (the noise of the dialogue's animation at a held shot).
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts.qa.decode import PASS_COMPOSE, Trace  # noqa: E402

KY = np.array([0.2126, 0.7152, 0.0722])
CHARACTER = (3, 5)


def shot_energy(trace_dir: Path) -> dict:
    """{'mask': mean luminance over the character mask, 'frame': over the frame, 'share': mask fraction,
    'float_frame': dumps.json's full-resolution mean (check of the preview grid)}."""
    t = Trace(trace_dir)
    k = t.find(PASS_COMPOSE, "R11G11B10F")
    if not k:
        raise SystemExit(f"{trace_dir}: no {PASS_COMPOSE} dump")
    d = t.dumps[k]
    raw = (trace_dir / d["png"]).with_name(Path(d["png"]).stem + "_p.f32")
    g = t.gbuffer_fs()
    rk = t.find(g, "RGBA8", slot=2) if g else None
    if not rk:
        raise SystemExit(f"{trace_dir}: no G-buffer RT2 dump")
    ap = (trace_dir / t.dumps[rk]["png"]).with_name(Path(t.dumps[rk]["png"]).stem + "_a.png")
    if not raw.exists() or not ap.exists():
        raise SystemExit(f"{trace_dir}: missing {raw.name if not raw.exists() else ap.name} (dump_preview_f32=1?)")
    cls = np.asarray(Image.open(ap).convert("L"), dtype=np.uint8) & 7
    h, w = cls.shape
    rgb = np.fromfile(raw, np.float32)
    if rgb.size != h * w * 4:
        raise SystemExit(f"{trace_dir}: {raw.name} is not {w}x{h} RGBA (the colour and RT2 previews must share the grid)")
    lum = rgb.reshape(h, w, 4)[..., :3] @ KY
    lum = np.where(np.isfinite(lum), lum, 0.0)
    mask = np.isin(cls, CHARACTER)
    return {"mask": float(lum[mask].mean()) if mask.any() else float("nan"), "frame": float(lum.mean()),
            "share": float(mask.mean()), "float_frame": float(np.dot(d["mean"][:3], KY))}


def compare(shots: list[tuple[str, dict]]) -> list[dict]:
    """Per ON shot: d = (ON - mean of the nearest OFF before and after) / that mean, for the mask and the frame."""
    off_idx = [i for i, (n, _) in enumerate(shots) if "-off-" in n]
    off_mask = np.array([shots[i][1]["mask"] for i in off_idx])
    spread = float(off_mask.std(ddof=1) / off_mask.mean()) if len(off_mask) > 1 else float("nan")
    rows = []
    for i, (name, e) in enumerate(shots):
        if i in off_idx:
            continue
        before = [j for j in off_idx if j < i][-1:]
        after = [j for j in off_idx if j > i][:1]
        ref = before + after
        if not ref:
            continue
        m = np.mean([shots[j][1]["mask"] for j in ref])
        f = np.mean([shots[j][1]["frame"] for j in ref])
        rows.append({"shot": name, "mask_d": e["mask"] / m - 1.0, "frame_d": e["frame"] / f - 1.0,
                     "off": [shots[j][0] for j in ref], "spread": spread})
    return rows


def main() -> int:
    a = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    a.add_argument("--root", type=Path, required=True, help="the captures directory holding the trace folders")
    a.add_argument("shots", nargs="+", metavar="LABEL=STAMP")
    a.add_argument("--json", type=Path, help="also write the numbers here")
    a.add_argument("--moved", default="", help="comma-separated shot labels whose window logged a 'camera moving' "
                   "scene build: excluded from every comparison (as ON and as OFF neighbour), counted")
    args = a.parse_args()
    moved = {m for m in args.moved.split(",") if m}
    pairs = [x.split("=", 1) for x in args.shots]
    print(f"camera moved in {sum(lab in moved for lab, _ in pairs)} of {len(pairs)} shots (excluded): "
          f"{', '.join(lab for lab, _ in pairs if lab in moved) or 'none'}")
    shots = [(lab, shot_energy(args.root / st)) for lab, st in pairs if lab not in moved]
    print(f"{'shot':22s} {'mask mean':>10s} {'frame':>10s} {'mask %':>7s} {'float frame':>11s}")
    for n, e in shots:
        print(f"{n:22s} {e['mask']:10.5g} {e['frame']:10.5g} {100 * e['share']:6.2f}% {e['float_frame']:11.5g}")
    rows = compare(shots)
    if rows:
        print(f"OFF spread of the mask mean (relative std): {rows[0]['spread']:.2%}")
    for r in rows:
        print(f"{r['shot']:22s} mask {r['mask_d']:+8.2%} frame {r['frame_d']:+8.2%} vs {'/'.join(r['off'])}")
    if args.json:
        args.json.write_text(json.dumps({"shots": dict(shots), "compare": rows}, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
