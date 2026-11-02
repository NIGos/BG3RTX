"""Flicker/stippling of gi_rr_rays 1 against 2 in one sh1 run: the QA temporal detector (scripts/qa/detectors.py
temporal(): per-pixel temporal std of the final frame's PQ code, static camera, derived limits) on the three traces
of shot-rays-on against the three of each OFF phase. The null is OFF-b against OFF-a in both directions: where it
fails on its own (S8: 3-21 per mille of excess regions between two key-off sets, sh1 043322 and sh2 055914), the noise floor
is the larger of its two excess areas, and the key passes when its excess area against each OFF set stays at or below
that floor and its mean std excess below one PQ step (the detector's own limit).

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
    """(pass, excess area per mille, mean excess) of the QA temporal detector."""
    shape = on[0].final_code().shape[:2]
    moving = det.animated([f.albedo() for f in on + off], [f.sun_mask() for f in on + off], shape,
                          [f.normal() for f in on + off])
    r = det.temporal([f.final_code() for f in on], [f.final_code() for f in off], moving)
    m = r["metrics"]
    print(f"{name}: {'PASS' if r['pass'] else 'FAIL'} mean std excess {r['score']} PQ (limit {r['threshold']}), "
          f"std {m['std_on']} vs {m['std_off']} (ratio {m['ratio']}), excess regions {m['excess_area']} per mille, "
          f"animated {m['animated_frac']}, frames {m['frames']}{', INCONCLUSIVE' if m['inconclusive'] else ''}")
    return r["pass"], m["excess_area"], r["score"], r["threshold"]


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
    n1 = compare(off_b, off_a, "null OFF-b vs OFF-a")
    n2 = compare(off_a, off_b, "null OFF-a vs OFF-b")
    k1 = compare(on, off_a, "rays 1 vs OFF-a")
    k2 = compare(on, off_b, "rays 1 vs OFF-b")
    if n1[0] and n2[0]:
        ok = k1[0] and k2[0]
        print(f"null passes: the detector's own gate decides: {'PASS' if ok else 'FAIL'}")
    else:
        floor = max(n1[1], n2[1])
        ok = all(k[1] <= floor and k[2] <= k[3] for k in (k1, k2))
        print(f"null fails on its own: noise floor {floor} per mille (the larger key-off excess area); "
              f"rays 1 excess {k1[1]} / {k2[1]} per mille, mean excess {k1[2]} / {k2[2]} PQ: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1

if __name__ == "__main__":
    sys.exit(main())
