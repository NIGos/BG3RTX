"""Fail closed on incomplete/mismatched vg3 motion or float-energy evidence."""
import argparse
import json
import math
from pathlib import Path
import re
import sys
import statistics

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO))
from scripts.queue_gates import stamped_lines, windows, plan_drift, check
from tools.m4_promotion_gate import Run

HERE = Path(__file__).resolve().parent
CAPTURE = re.compile(r"\[geo\] RT capture, last 120 frames:.*alpha draws made opaque \(region full\) ([0-9.]+), opaque draws dropped \(buffer full\) ([0-9.]+)")


def compare_turns(series):
    """Turn-wide peak and mean must fit OFF-OFF spread; no angle alignment claim."""
    errors, results = [], {}
    for direction in ("r", "l"):
        a, on, b = [series.get(f"turn-{state}-{direction}", []) for state in ("off-a", "on", "off-b")]
        if len(a) < 2 or not (len(a) == len(on) == len(b)):
            errors.append(f"{direction}: unmatched complete summaries {len(a)}/{len(on)}/{len(b)} (need equal >=2)")
            continue
        for metric, index in [("alpha_fallback", 0), ("opaque_drop", 1)]:
            for statistic, reduce in [("peak", max), ("mean", statistics.mean)]:
                av, ov, bv = [reduce(row[index] for row in rows) for rows in (a, on, b)]
                tolerance = abs(av - bv) + 0.02
                excess = ov - max(av, bv)
                results[f"{direction}_{metric}_{statistic}"] = dict(tolerance=tolerance, on_excess=excess, off_a=a, on=on, off_b=b)
                if excess > tolerance + 1e-9:
                    errors.append(f"{direction} {metric} {statistic}: extra {excess:.2f} > OFF-OFF tolerance {tolerance:.2f}")
    return errors, results


def evaluate(log, runner):
    plan_path = HERE / "plans/vg3_s8.json"
    plan = json.loads(plan_path.read_text())
    job = json.loads((HERE / "jobs/vg3-s8-low-long-view-fg2.json").read_text())
    text = log.read_text(encoding="utf-8-sig", errors="replace")
    runtext = runner.read_text(encoding="utf-8-sig", errors="replace")
    lines = stamped_lines(text)
    wins = windows(runtext, plan, lines)
    errors = check(job["expect_every"], text, runtext, plan)
    if plan_drift(wins, plan) or len(wins) != len(plan):
        errors.append("missing phases or plan drift")
    series = {}
    previous = None
    for t, line in lines:
        m = CAPTURE.search(line)
        if not m:
            continue
        for w in wins:
            if previous is not None and w["applied"] <= previous < t < w["end"]:
                series.setdefault(w["label"], []).append(tuple(map(float, m.groups())))
        previous = t
    failures, turns = compare_turns(series)
    errors += failures
    # Reuse the production float scene-colour reader; never read PNG/bloom energy.
    if runner.resolve() != log.with_suffix(".runner.txt").resolve():
        errors.append("runner must be the log's sibling .runner.txt for float capture attribution")
    energy = {}
    run = Run(log, plan=plan_path)
    for label in ("shot-off-a", "shot-on", "shot-off-b"):
        value = run.scene_colour(label)
        if not value or not isinstance(value[0], (float, int)) or not math.isfinite(value[0]) or value[0] <= 0 or value[1] != 0 or (value[2] is None or not math.isfinite(value[2]) or value[2] <= 0):
            errors.append(f"{label}: missing/nonfinite float scene colour or exposure probe")
        else:
            energy[label] = value[0] / value[2]
    if len(energy) == 3:
        baseline = (energy["shot-off-a"] + energy["shot-off-b"]) / 2
        delta = 100 * (energy["shot-on"] / baseline - 1)
        energy["on_delta_pct"] = delta
        energy["off_off_pct"] = 100 * (energy["shot-off-b"] / energy["shot-off-a"] - 1)
        if abs(delta) > 0.92:
            errors.append(f"energy {delta:+.4f}% outside +/-0.92%")
    memory = {w["label"]: [l for t,l in lines if w["applied"] + 1 <= t < w["end"] and ("[mem] ours (" in l or "[bvh] mesh cache:" in l)] for w in wins if w["label"] in ("turn-on-r", "turn-on-l", "recover-on")}
    return dict(passed=not errors, failures=errors, turns=turns, energy=energy, per_consumer_memory=memory)


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--log", type=Path, required=True)
    p.add_argument("--runner", type=Path, required=True)
    args = p.parse_args()
    try:
        result = evaluate(args.log, args.runner)
    except (OSError, ValueError, SystemExit) as e:
        result = dict(passed=False, failures=[str(e)])
    args.log.with_suffix(".vg3.json").write_text(json.dumps(result, indent=1) + "\n")
    print("vg3 post gate: " + ("PASS" if result["passed"] else "FAIL: " + "; ".join(result["failures"])))
    raise SystemExit(0 if result["passed"] else 1)
