"""Check real-style groom identity and timings: python tools/hair_groom/speed_check.py <inputs dir>."""
from __future__ import annotations

import argparse
import dataclasses
import json
from pathlib import Path
import sys
import threading
import time

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.hair_groom import convert, mesh  # noqa: E402


# Supplied measurements of the unchanged converter, not timings of a second code path.
BASELINES = {
    "HAIR_HUM_F_Shadowheart_Spring": (8.4, 5.9),
    "Beard_HUM_Gale": (4.8, 4.0),
    "ELF_MS_NKD_Hair_Halsin": (12.6, 12.5),
}


def timed(name, fn):
    """Long or contended runs still show progress without changing converter code."""
    print(f"{name}: starting", file=sys.stderr, flush=True)
    done = threading.Event()
    start = time.perf_counter()

    def progress():
        while not done.wait(30):
            print(f"{name}: running, {time.perf_counter() - start:.1f} s elapsed",
                  file=sys.stderr, flush=True)

    worker = threading.Thread(target=progress, daemon=True)
    worker.start()
    try:
        start = time.perf_counter()
        result = fn()
        elapsed = time.perf_counter() - start
    finally:
        done.set()
        worker.join()
    return result, elapsed


def compare(groom, dropped, ref):
    fields, max_abs, max_rel = {}, 0.0, 0.0
    for f in dataclasses.fields(convert.Groom):
        a, b = getattr(groom, f.name), ref[f.name]
        same = a.shape == b.shape and a.dtype == b.dtype
        if same and a.dtype.kind == "f":
            # Equality of bytes also catches signed zero and NaN payload changes.
            same = a.tobytes() == b.tobytes()
        elif same:
            same = np.array_equal(a, b)
        fields[f.name] = bool(same)
        if not same and a.dtype.kind == "f" and a.shape != b.shape:
            max_abs = max_rel = float("inf")
        elif not same and a.dtype.kind == "f" and a.size:
            delta = np.abs(a.astype(np.float64) - b.astype(np.float64))
            if not np.all(np.isfinite(delta)):
                max_abs = max_rel = float("inf")
            else:
                max_abs = max(max_abs, float(delta.max()))
                # A nonzero difference against zero has no finite relative error.
                rel = np.divide(delta, np.abs(b), out=np.zeros_like(delta), where=b != 0)
                rel[(b == 0) & (delta != 0)] = np.inf
                max_rel = max(max_rel, float(rel.max()))
    fields["dropped"] = dropped == int(ref["dropped"])
    return fields, max_abs, max_rel


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", type=Path)
    args = parser.parse_args()
    rows = json.loads((args.inputs / "index.json").read_text(encoding="utf-8"))
    results = []
    for row in rows:
        tag = row["tag"]
        hair = mesh.load_gr2_dump(args.inputs / f"{tag}.dump.bin", row["mesh"])
        mask = np.load(args.inputs / f"{tag}.mask.npy", allow_pickle=False)
        (groom, _), cv = timed(f"{tag} convert", lambda: convert.convert(hair, mask, tile_grid=tuple(row["grid"])))
        (groom, dropped), dd = timed(f"{tag} dedupe", lambda: convert.dedupe(groom))
        with np.load(args.inputs / f"{tag}.reference.npz", allow_pickle=False) as ref:
            fields, max_abs, max_rel = compare(groom, dropped, ref)
        old_cv, old_dd = BASELINES[tag]
        result = {"tag": tag, "identical": "yes" if all(fields.values()) else "no",
                  "fields": fields, "max_diff": max_abs, "max_relative_diff": max_rel,
                  "dropped": dropped, "strands": groom.strands,
                  "old_seconds": {"convert": old_cv, "dedupe": old_dd},
                  "new_seconds": {"convert": cv, "dedupe": dd},
                  "speedup": (old_cv + old_dd) / (cv + dd)}
        results.append(result)
        print(json.dumps(result), flush=True)
    halsin = next((r for r in results if r["tag"] == "ELF_MS_NKD_Hair_Halsin"), None)
    identical = all(r["identical"] == "yes" for r in results)
    target = halsin["speedup"] >= 3 if halsin else None
    print(json.dumps({"all_identical": identical, "halsin_target_3x": target}), flush=True)
    return 0 if identical and target is not False else 1


if __name__ == "__main__":
    sys.exit(main())
