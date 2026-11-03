"""Reject mismatched dialogue shots before pair analysis; never register/warp beauty.

Usage: python tools/live_batches/dialogue_shadows/pair_registration.py manifest.json
Manifest maps every traced plan label to capture directory lists in trace order.
"""
import argparse
import json
import math
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from scripts.qa.decode import Frame, Trace
from scripts.qa.detectors import view_shift


def registration(a, b):
    if a is None or b is None or a.shape != b.shape:
        raise ValueError("missing albedo or image dimensions differ")
    dy, dx, peak, origin = view_shift(a, b)
    if not math.isfinite(peak) or peak <= 0:
        raise ValueError("albedo has no usable registration peak")
    shift = (round(dy), round(dx))
    return {"shift_px": shift, "refined_shift_px": (dy, dx),
            "peak": peak, "origin": origin, "valid": shift == (0, 0)}


def comparisons(plan):
    labels = [p["label"] for p in plan]
    off = [i for i, label in enumerate(labels) if label.startswith("shot-off-")]
    pairs = []
    for i, label in enumerate(labels):
        if label.startswith("shot-") and i not in off:
            before = [j for j in off if j < i]
            after = [j for j in off if j > i]
            if not before or not after:
                raise ValueError(f"{label}: missing OFF neighbour")
            pairs.extend(((labels[j], 0, label, 0) for j in (before[-1], after[0])))
        if label.startswith("motion-"):
            pairs.append((label, 0, label, 1))
    # The ON temporal pair also needs the same camera as its old-behavior and
    # RT-sun-off controls. This validates view only, not character pose equality.
    for control in ("motion-base-fix-off", "motion-nosun"):
        pairs.extend((control, i, "motion-base", i) for i in (0, 1))
    return pairs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    plan = json.loads(Path(__file__).with_name("plans").joinpath("ds_dialogue.json").read_text())
    cache, rows = {}, []

    def albedo(label, index):
        name = manifest[label][index]
        path = args.manifest.parent / name
        if path not in cache:
            cache[path] = Frame(Trace(path)).albedo()
        return cache[path]

    for a, ai, b, bi in comparisons(plan):
        row = {"off": f"{a}[{ai}]", "on": f"{b}[{bi}]", "valid": False}
        try:
            row.update(registration(albedo(a, ai), albedo(b, bi)))
        except (KeyError, IndexError, ValueError, OSError) as error:
            row["error"] = str(error)
        rows.append(row)
    valid = bool(rows) and all(row["valid"] for row in rows)
    print(json.dumps({"valid": valid, "pairs": rows}, indent=1))
    return 0 if valid else 1


if __name__ == "__main__":
    raise SystemExit(main())
