"""Flicker/stippling of gi_rr_rays 1 against 2 in one sh1 run: the QA temporal detector (scripts/qa/detectors.py
temporal(): per-pixel temporal std of the final frame's PQ code, static camera, derived limits) on the three traces
of shot-rays-on against the three of each OFF phase, and OFF-b against OFF-a as the null (it must pass).

    py -3 tools/live_batches/sharc_far/rays_flicker.py <kept log> [--captures-root DIR]
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts.qa import decode, detectors as det  # noqa: E402
from tools.m4_promotion_gate import TRACE, Run  # noqa: E402


def traces(run: Run, label: str) -> list[Path]:
    p = run.phase_of(label)
    out = []
    for t, txt in run.lines if p else []:
        if p[1] <= t <= p[2] + 2.0:
            m = TRACE.search(txt)
            if m:
                cap = Path(m.group(2).strip())
                out.append(run.captures_root / cap.name if run.captures_root else cap)
    return out


def frames(paths):
    return [decode.Frame(decode.Trace(p)) for p in paths]


def compare(on, off, name):
    shape = on[0].final_code().shape[:2]
    moving = det.animated([f.albedo() for f in on + off], [f.sun_mask() for f in on + off], shape,
                          [f.normal() for f in on + off])
    r = det.temporal([f.final_code() for f in on], [f.final_code() for f in off], moving)
    m = r["metrics"]
    print(f"{name}: {'PASS' if r['pass'] else 'FAIL'} mean std excess {r['score']} PQ (limit {r['threshold']}), "
          f"std {m['std_on']} vs {m['std_off']} (ratio {m['ratio']}), excess regions {m['excess_area']} per mille, "
          f"animated {m['animated_frac']}, frames {m['frames']}{', INCONCLUSIVE' if m['inconclusive'] else ''}")
    return r["pass"]


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("log", type=Path)
    ap.add_argument("--captures-root", type=Path, default=None)
    a = ap.parse_args(argv)
    run = Run(a.log, a.captures_root)
    sets = {k: traces(run, k) for k in ("shot-rays-off-a", "shot-rays-on", "shot-rays-off-b")}
    for k, v in sets.items():
        print(f"{k}: {len(v)} traces")
        if len(v) < 2:
            print("INVALID: fewer than 2 traces")
            return 2
    off_a, on, off_b = (frames(sets[k]) for k in ("shot-rays-off-a", "shot-rays-on", "shot-rays-off-b"))
    null = compare(off_b, off_a, "null OFF-b vs OFF-a")
    ok = compare(on, off_a, "rays 1 vs OFF-a") & compare(on, off_b, "rays 1 vs OFF-b")
    if not null:
        print("INVALID: the OFF-OFF null fails (scene motion or drift); the rays verdict is not usable")
        return 2
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
