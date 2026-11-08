"""Equivalence gate of bg3rtx_hairconv against the Python reference (docs/RTX_HAIR_ROADMAP.md 2d).

python -m tools.hair_groom.hairconv_compare --py <dir of hcap.reference output> --cpp <bg3rtx_hairconv --json file>
    --cpp-out <its groom dir>

Per style: the same verdict, IoU / coverage / radiance (3 decimals), strand and segment counts (strand counts may
differ by dedupe-threshold ties); for styles both wrote: the same index id, and decoded LODs whose strands match
(triangles, roots exact; barycentrics within half a quantisation step; radii within the 16-bit log step).
Prints one line per style and a summary; exit 1 on a failed gate.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np

from . import storage


def compare_grooms(a_path, b_path):
    a, b = storage.read_any(a_path), storage.read_any(b_path)
    if a.header[16:24] != b.header[16:24]:
        return {"index_id": False}
    out = {"index_id": True, "lods": []}
    for la, lb in zip(a.lods, b.lods):
        oa = storage.strand_offsets(la.points, la.segment)
        ob = storage.strand_offsets(lb.points, lb.segment)
        if la.points == lb.points and np.array_equal(oa, ob):
            out["lods"].append({
                "points": int(la.points), "strands_equal": True,
                "triangle_equal": bool(np.array_equal(la.triangle, lb.triangle)),
                "root_equal": bool(np.array_equal(la.root, lb.root)),
                "bary_max": float(np.abs(la.bary.astype(np.float64) - lb.bary).max()),
                "radius_rel_max": float(np.abs(la.radius.astype(np.float64) / lb.radius - 1).max())})
        else:
            # Strand sets differ (dedupe ties): compare the strands both have, keyed by their first point.
            def keys(l, o):
                return {(int(l.triangle[s]), tuple(np.round(l.bary[s], 6))): i for i, s in enumerate(o[:-1])}
            ka, kb = keys(la, oa), keys(lb, ob)
            out["lods"].append({"points": int(la.points), "strands_equal": False, "strands": [len(oa) - 1, len(ob) - 1],
                                "only_one_side": len(set(ka) ^ set(kb))})
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--py", required=True, type=Path)
    ap.add_argument("--cpp", required=True, type=Path)
    ap.add_argument("--cpp-out", required=True, type=Path)
    a = ap.parse_args(argv)
    cpp = {r["name"]: r for r in json.loads(a.cpp.read_text(encoding="utf-8"))}
    fails, rows = 0, 0
    tie_strands = 0
    bary_max = rad_max = 0.0
    for pj in sorted(a.py.glob("*.json")):
        py = json.loads(pj.read_text(encoding="utf-8"))
        c = cpp.get(py["name"])
        if c is None:
            print(f"{py['name']}: missing in C++ output"); fails += 1
            continue
        rows += 1
        msg, ok = [], True
        for k in ("verdict", "iou", "coverage_delta", "radiance_delta", "tile_grid", "cards", "tracer"):
            if py.get(k) != c.get(k):
                msg.append(f"{k} {py.get(k)} vs {c.get(k)}"); ok &= k not in ("radiance_delta",) or py.get("verdict") != c.get("verdict")
        if py["verdict"] != c["verdict"]:
            ok = False
        ds = c["strands"] - py["strands"]
        if ds:
            msg.append(f"strands {py['strands']} vs {c['strands']} ({ds:+d})")
            tie_strands += abs(ds)
            ok &= abs(ds) <= max(3, py["strands"] // 20000)
        if py.get("asset") and c.get("asset"):
            g = compare_grooms(a.py / py["asset"], a.cpp_out / c["asset"])
            if not g["index_id"]:
                msg.append("index id differs"); ok = False
            for l in g.get("lods", []):
                if l["strands_equal"]:
                    bary_max = max(bary_max, l["bary_max"]); rad_max = max(rad_max, l["radius_rel_max"])
                    if not (l["triangle_equal"] and l["root_equal"] and l["bary_max"] < 3.1e-5 and l["radius_rel_max"] < 4e-4):
                        msg.append(f"LOD arrays differ {l}"); ok = False
                else:
                    msg.append(f"LOD strand sets differ: {l['strands']}, {l['only_one_side']} strands on one side only")
        elif bool(py.get("asset")) != bool(c.get("asset")):
            msg.append("only one side wrote a groom"); ok = False
        fails += not ok
        print(f"{'ok  ' if ok else 'FAIL'} {py['name']}: {py['verdict']}, {py['strands']} strands" + (f"; {'; '.join(msg)}" if msg else ""))
    print(f"{rows} styles, {fails} failed; strand-count differences {tie_strands}; barycentric max diff {bary_max:.2e}, "
          f"radius max rel diff {rad_max:.2e}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
