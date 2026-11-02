"""tools/live_batches/dialogue_shadows/char_energy.py on synthetic traces (CTest char_energy_test).

A trace = dumps.json + the compose pass's *_p.f32 + the G-buffer's RT2 *_a.png (class bits), as
layer/dump.cpp writes them with dump_preview_f32=1.
"""
import importlib.util
import json
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("char_energy", ROOT / "tools/live_batches/dialogue_shadows/char_energy.py")
ce = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ce)
FAILS = []


def check(name, ok, info=""):
    print(f"{'PASS' if ok else 'FAIL'} {name}{(': ' + str(info)) if not ok else ''}")
    if not ok:
        FAILS.append(name)


def make_trace(d: Path, char_lum: float, rest_lum: float, w=8, h=4):
    (d / "img").mkdir(parents=True)
    cls = np.zeros((h, w), np.uint8)
    cls[:, :2] = 3            # skin
    cls[0, 2] = 5             # hair
    cls[1, 2] = 2             # another class: not the mask
    mask = np.isin(cls, (3, 5))
    rgb = np.zeros((h, w, 4), np.float32)
    rgb[..., :3] = np.where(mask, char_lum, rest_lum)[..., None]
    rgb.tofile(d / "img" / "d9_c0_p_p.f32")
    Image.fromarray((cls | 0x10).astype(np.uint8), "L").save(d / "img" / "d3_rt2_a.png")
    mean = float(rgb[..., 0].mean())
    dumps = {
        "d9": {"png": "img/d9_c0_p.png", "pass_fs": "5c3fdabc477f316a", "slot": 0, "fmt": "R11G11B10F", "w": w, "h": h,
               "mean": [mean] * 3, "min": [0] * 3, "max": [1] * 3, "nonfinite": 0},
        # the G-buffer signature (scripts/qa/decode.py gbuffer_fs): RG16F slot 0 and RGBA8 slots 1-3
        "d1": {"png": "img/d1.png", "pass_fs": "aa4efa4d00000000", "slot": 0, "fmt": "RG16F", "w": w, "h": h},
        "d2": {"png": "img/d2.png", "pass_fs": "aa4efa4d00000000", "slot": 1, "fmt": "RGBA8", "w": w, "h": h},
        "d3": {"png": "img/d3_rt2.png", "pass_fs": "aa4efa4d00000000", "slot": 2, "fmt": "RGBA8", "w": w, "h": h},
        "d4": {"png": "img/d4.png", "pass_fs": "aa4efa4d00000000", "slot": 3, "fmt": "RGBA8", "w": w, "h": h},
    }
    (d / "dumps.json").write_text(json.dumps(dumps), encoding="utf-8")


def main():
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        for stamp, c, r in (("a", 1.0, 2.0), ("on", 1.5, 2.0), ("b", 1.2, 2.0)):
            make_trace(root / stamp, c, r)
        e = ce.shot_energy(root / "a")
        check("mask mean is the character pixels' float luminance", abs(e["mask"] - 1.0) < 1e-6, e)
        check("mask share: 9 of 32 texels (skin 8 + hair 1, class 2 excluded)", abs(e["share"] - 9 / 32) < 1e-9, e)
        check("frame mean over every texel", abs(e["frame"] - (9 * 1.0 + 23 * 2.0) / 32) < 1e-6, e)
        shots = [(lab, ce.shot_energy(root / st)) for lab, st in
                 (("shot-off-a", "a"), ("shot-noao-on", "on"), ("shot-off-b", "b"))]
        rows = ce.compare(shots)
        check("one ON row against both OFF neighbours", len(rows) == 1 and rows[0]["off"] == ["shot-off-a", "shot-off-b"], rows)
        check("ON vs the OFF mean: 1.5 / 1.1 - 1", abs(rows[0]["mask_d"] - (1.5 / 1.1 - 1)) < 1e-6, rows)
        check("OFF spread: relative std of 1.0 and 1.2", abs(rows[0]["spread"] - np.std([1.0, 1.2], ddof=1) / 1.1) < 1e-6, rows)
        (root / "a" / "img" / "d9_c0_p_p.f32").unlink()
        try:
            ce.shot_energy(root / "a")
            check("a missing float preview is an error, never a PNG fallback", False)
        except SystemExit as x:
            check("a missing float preview is an error, never a PNG fallback", "dump_preview_f32" in str(x), x)
    print("all checks passed" if not FAILS else f"{len(FAILS)} FAILED")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
