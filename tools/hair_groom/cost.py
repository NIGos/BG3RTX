"""Cost per groom and per character (docs/RTX_HAIR_PRIMARY.md increment 5): VRAM and GPU ms from the converted files.

python -m tools.hair_groom.cost [--dir data/assetdb/hair_grooms] [--out <report.json>] [--crowd]

Reads every .hgrm's LOD headers (points, segments) and applies the per-unit costs measured on the RTX 5090
(tools/hair_lss_bench, docs/RTX_HAIR_GROOM_EXPERIMENT.md "Optimisation sweep", Shadowheart 2 mm, 856,802 segments):
  BLAS compacted 60 MB at LOD 0 -> bytes per segment; refit 0.10 ms -> ms per segment; shadow-ray trace
  0.91 / 0.71 / 0.63 ms per 3.7M rays leaving the hair at LOD 0 / 1/2 / 1/4 -> ns per ray.
Per character: mesh-pool arrays (static per style, shared), points (per instance; + UV and previous points with
rt_hair_groom_primary), BLAS, refit ms, and trace ms per 100k hair rays. --crowd adds a scenario table: N
characters at a screen-size LOD mix with and without the crowd budget (hair_groom::plan_crowd's rule).
Estimates, not measurements of a scene: the live job tools/live_batches/hair_crowd measures the real lines.
"""
from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BENCH_SEGMENTS = 856802
BLAS_BYTES_PER_SEGMENT = 60e6 / BENCH_SEGMENTS
REFIT_MS_PER_SEGMENT = 0.10 / BENCH_SEGMENTS
NS_PER_RAY = (0.91e6 / 3.7e6, 0.71e6 / 3.7e6, 0.63e6 / 3.7e6)   # LOD 0, 1/2, 1/4 (shadow rays from the hair)
STATIC_BYTES_PER_POINT = 4 + 8 + 4          # triangle, barycentrics, radius (mesh pool, per style and LOD)
STATIC_BYTES_PER_SEGMENT = 4 + 4            # segment start, segment -> card triangle
POINT_BYTES = 12                            # per instance: this frame's points
PRIMARY_POINT_BYTES = 8 + 12                # rt_hair_groom_primary: point UV, previous points


def lods_of(path):
    """[(keep, points, segments)] from the file's LOD headers (common/hair_groom_asset.h layout). Reads only the
    headers (the arrays are skipped by their sizes; the file size must end exactly after the last LOD)."""
    size = Path(path).stat().st_size
    with open(path, "rb") as f:
        head = f.read(40)
        if head[:4] != b"HGRM" or len(head) < 40:
            raise ValueError(f"{path}: not a groom file")
        version, _, _, _, indices, lods, _ = struct.unpack_from("<IQQIIII", head, 4)
        at = 40 + (32 * 8 if version == 2 else 0)
        out = []
        for _ in range(lods):
            f.seek(at)
            keep, points, segments, _ = struct.unpack("<fIII", f.read(16))
            out.append((keep, points, segments))
            at += 16 + points * 17 + segments * 4
    if at != size:
        raise ValueError(f"{path}: {size - at} trailing bytes")
    return indices, out


def groom_cost(points, segments, lod, primary=False):
    static = points * STATIC_BYTES_PER_POINT + segments * STATIC_BYTES_PER_SEGMENT
    inst = points * (POINT_BYTES + (PRIMARY_POINT_BYTES if primary else 0)) + segments * BLAS_BYTES_PER_SEGMENT
    return {"static_mb": static / 1e6, "instance_mb": inst / 1e6, "refit_ms": segments * REFIT_MS_PER_SEGMENT,
            "trace_ms_per_100k_rays": 1e5 * NS_PER_RAY[min(lod, 2)] * 1e-6}


def report(directory, primary=False):
    rows = []
    for f in sorted(Path(directory).glob("*.hgrm")):
        try:
            indices, lods = lods_of(f)
        except (ValueError, struct.error) as e:   # a file the batch is still writing, or a malformed one
            rows.append({"file": f.name, "error": str(e)})
            continue
        rows.append({"file": f.name, "indices": indices,
                     "lods": [dict(keep=k, points=p, segments=s, **groom_cost(p, s, i, primary)) for i, (k, p, s) in enumerate(lods)]})
    return rows


def crowd(rows, n_close=2, n_mid=4, n_far=8, hair_rays=(300e3, 60e3, 12e3), budget_ksegments=3500):
    """Scenario: characters at three distances (screen-size LOD 0 / 1 / 2), the median style; hair rays per character
    per pass (close-up head ~300k at 2560x1067, mid 60k, far 12k). Per pass, for primary + shadow: one ray each."""
    import statistics
    seg = [statistics.median(r["lods"][i]["segments"] for r in rows if len(r["lods"]) > i) for i in range(3)]
    pts = [statistics.median(r["lods"][i]["points"] for r in rows if len(r["lods"]) > i) for i in range(3)]
    chars = [(0, hair_rays[0])] * n_close + [(1, hair_rays[1])] * n_mid + [(2, hair_rays[2])] * n_far

    def total(plan):
        ms = mb = 0.0
        for (want, rays), lod in zip(chars, plan):
            if lod < 0:
                continue
            c = groom_cost(pts[lod], seg[lod], lod, primary=True)
            ms += c["refit_ms"] + 2 * rays * NS_PER_RAY[lod] * 1e-6
            mb += c["instance_mb"]
        return ms, mb
    free = [w for w, _ in chars]
    left, plan = budget_ksegments * 1000, []
    for w, _ in chars:   # already largest first
        lod = next((l for l in range(w, 3) if seg[l] <= left), -1)
        if lod >= 0:
            left -= seg[lod]
        plan.append(lod)
    ms0, mb0 = total(free)
    ms1, mb1 = total(plan)
    return {"characters": len(chars), "median_segments": seg, "budget_ksegments": budget_ksegments,
            "screen_lod": {"ms": ms0, "instance_mb": mb0},
            "crowd_plan": {"ms": ms1, "instance_mb": mb1, "lods": plan}}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dir", type=Path, default=None)
    ap.add_argument("--out", type=Path)
    ap.add_argument("--crowd", action="store_true")
    a = ap.parse_args()
    d = a.dir
    if d is None:
        for base in (ROOT, ROOT.parents[2] if len(ROOT.parents) > 2 else ROOT):
            if (base / "data" / "assetdb" / "hair_grooms").is_dir():
                d = base / "data" / "assetdb" / "hair_grooms"
                break
    all_rows = report(d, primary=True)
    rows = [r for r in all_rows if "error" not in r]
    out = {"dir": str(d), "grooms": len(rows), "unreadable": [r["file"] for r in all_rows if "error" in r], "rows": rows}
    if a.crowd and rows:
        out["crowd"] = crowd(rows)
    lod0 = [r["lods"][0] for r in rows]
    if lod0:
        out["summary"] = {k: {"min": min(x[k] for x in lod0), "median": sorted(x[k] for x in lod0)[len(lod0) // 2],
                              "max": max(x[k] for x in lod0)}
                          for k in ("segments", "static_mb", "instance_mb", "refit_ms")}
    text = json.dumps(out, indent=1)
    if a.out:
        a.out.write_text(text + "\n", encoding="utf-8")
    print(json.dumps({k: out[k] for k in out if k != "rows"}, indent=1))


if __name__ == "__main__":
    main()
