"""Save or compare CPU validation renders of cached LOD0 grooms.

The default shares card rasterisation via card_fragment_builders; --separate-cards
measures the original batch call pattern. Both compare to the same reference arrays.
"""
from __future__ import annotations

import argparse
from dataclasses import fields
import json
from pathlib import Path
import sys
import time

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.low_priority import lower_priority
from tools.hair_groom import batch, convert, mesh, render


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("inputs", type=Path)
    ap.add_argument("--save", action="store_true")
    ap.add_argument("--separate-cards", action="store_true", help="measure the existing batch call pattern without paired rasterisation")
    args = ap.parse_args()
    lower_priority()
    dest = args.inputs / "render_ref"
    if args.save:
        dest.mkdir(exist_ok=True)
    failed = False
    for style in json.loads((args.inputs / "index.json").read_text(encoding="utf-8")):
        tag = style["tag"]
        hair = mesh.load_gr2_dump(args.inputs / (tag + ".dump.bin"), style["mesh"])
        mask = np.load(args.inputs / (tag + ".mask.npy"), allow_pickle=False)
        with np.load(args.inputs / (tag + ".reference.npz"), allow_pickle=False) as z:
            groom = convert.Groom(**{f.name: z[f.name] for f in fields(convert.Groom)})
        head = batch.head_proxy(hair)
        arrays = {}
        start = time.perf_counter()
        for name, (view, light) in batch.bbox_views(hair.positions, 0.002).items():
            if args.separate_cards:
                fibres = lambda v: render.card_fragments(hair.positions, hair.uv, hair.tris, mask, v, fibres=True)
                plain = lambda v: render.card_fragments(hair.positions, hair.uv, hair.tris, mask, v)
            else:
                fibres, plain = render.card_fragment_builders(hair.positions, hair.uv, hair.tris, mask)
            for kind, build in (
                ("fibres", fibres),
                ("plain", plain),
                ("strands", lambda v: render.strand_fragments(groom.points, groom.radii, groom.offsets, groom.root_param, v)),
            ):
                image, coverage = render.render(build, view, light, head)
                arrays[f"{name}_{kind}_image"] = image
                arrays[f"{name}_{kind}_coverage"] = coverage
            print(f"{tag}: {name} done", flush=True)
        seconds = time.perf_counter() - start
        path = dest / (tag + ".npz")
        if args.save:
            np.savez(path, **arrays)
            (dest / (tag + ".json")).write_text(json.dumps({"seconds": seconds}), encoding="utf-8")
            print(f"{tag}: saved, {seconds:.6f} s", flush=True)
        else:
            maximum = 0.0
            with np.load(path, allow_pickle=False) as ref:
                for key, value in arrays.items():
                    old = ref[key]
                    diff = float(np.max(np.abs(value - old))) if value.shape == old.shape else float("inf")
                    if not np.isfinite(diff):
                        diff = float("inf")
                    ok = diff <= 1e-6 if value.dtype.kind == "f" else np.array_equal(value, old)
                    if key.endswith("_image") and value.shape == old.shape:
                        # Preview encodings are integer arrays and must remain bit-identical too.
                        ok &= np.array_equal(render.to_png(value), render.to_png(old))
                    failed |= not ok
                    maximum = max(maximum, diff)
                    print(f"  {key}: max_abs_diff={diff:.12g} {'PASS' if ok else 'FAIL'}", flush=True)
            old_s = json.loads((dest / (tag + ".json")).read_text())["seconds"]
            print(f"{tag}: old={old_s:.6f} s new={seconds:.6f} s speedup={old_s / seconds:.3f}x max_abs_diff={maximum:.12g}", flush=True)
    return int(failed)


if __name__ == "__main__":
    sys.exit(main())
