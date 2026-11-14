"""handout_qual verdict: rt_scene_handout ON against the OFF pair of the same run, per window group.

    py -3 tools/live_batches_staged/handout_qual/analyze.py --job <job.json> --log <game.log> --runner <runner.txt> --out <md>

Reads a finished run; no game or queue access. Window medians are perf_ab's (complete 120-frame periods after each
window's acknowledged stats reset). Every threshold is the OFF repeat of the same group (predicted.json "noise_rule"):
noise = |off-b - off-a|; a gain is ON below the OFF mean by more than it. Energy: gi_1ray_qual's float samples
(dumps.json scene colour mean / pre-exposure), ON mean inside the range of the OFF samples.
"""
import argparse
from pathlib import Path
import re
import statistics
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts.queue_gates import stamped_lines, windows
from tools.live_batches import live_flow as flow
from tools.live_batches_staged.perf_ab import analyze as pab

pab.ROWS += [
    ("handed out", "per period", r"\[rt\] scene wait:.*handed out (\d+)"),
    ("handout in order", "per period", r"\[rt\] scene handout: \d+ frames, .*in order (\d+)"),
    ("handout fallbacks", "per period", r"\[rt\] scene handout: \d+ frames, .*fallbacks (\d+) \("),
    ("handout pairing mismatches", "per period", r"\[rt\] scene handout: .*pairing mismatches (\d+)"),
    ("OMM accelerated commits", "per period", r"\[rt\] scene handout: .*OMM accelerated commits (\d+)"),
    ("OMM downgraded", "per period", r"\[rt\] scene handout: .*downgraded (\d+);"),
    ("hit material queries at the commit", "per period", r"\[rt\] scene handout: .*queries at the commit (\d+)"),
    ("hit material fallbacks (query)", "per period", r"\[rt\] scene handout: .*fallbacks: query (\d+)"),
    ("hit material fallbacks (camera)", "per period", r"\[rt\] scene handout: .*camera (\d+)\)"),
    ("primary-ray check match", "%", r"primary-ray check match ([\d.]+)%"),
]
# (row, rule): "lower" = the deliverable; "not higher" = the per-pass GPU costs may fall (the handout removes a wait, so a
# lower cost is fine), never rise beyond the OFF pair's noise; "equal" = a count within that noise; "zero" = a counter.
# 2026-11-14: the first run judged the per-pass costs "equal" and failed when they went down.
JUDGED = [("scene wait mean", "lower"), ("frame time median", "lower"), ("frame time p99", "not higher"),
          ("RT", "not higher"), ("GI", "not higher"), ("scene build", "not higher"), ("strand G-buffer draws", "equal"),
          ("scene wait timeouts", "zero"), ("handout fallbacks", "zero"), ("handout pairing mismatches", "zero"),
          ("OMM downgraded", "zero"), ("hit material fallbacks (query)", "zero"),
          ("hit material fallbacks (camera)", "zero"), ("primary-ray check match", "not lower")]


def med(row, name):
    return statistics.median(row[name]) if row.get(name) else None


def pooled(measured, labels, name):
    values = [v for label in labels for v in measured.get(label, {}).get(name, [])]
    return statistics.median(values) if values else None


def groups(plan):
    """{group: {state: [window labels]}} from labels '<group>-<off-a|on|off-b>[-q|-e]'."""
    out = {}
    for p in plan:
        m = re.fullmatch(r"(.+?)-(off-a|on|off-b)(-[qe])?", p["label"])
        if m and p.get("frames") and not p.get("trace_at"):
            out.setdefault(m[1], {}).setdefault(m[2], []).append(p["label"])
    return {g: s for g, s in out.items() if len(s) == 3}


def judge(measured, plan):
    rows, failures = [], []
    for group, states in groups(plan).items():
        for name, rule in JUDGED:
            a, on, b = (pooled(measured, states[s], name) for s in ("off-a", "on", "off-b"))
            if on is None or (rule != "zero" and (a is None or b is None)):
                continue
            if rule == "zero":
                ok, text = on == 0, f"ON {on:g}"
            else:
                off, noise = (a + b) / 2, abs(b - a)
                d = on - off
                ok = {"equal": abs(d) <= noise, "lower": d < 0 or abs(d) <= noise, "not higher": d <= noise,
                      "not lower": d >= -noise}[rule]
                gain = " (gain beyond noise)" if rule == "lower" and d < -noise else ""
                text = f"OFF {a:.3f} / {b:.3f}, ON {on:.3f}, ON - OFF {d:+.3f}, noise {noise:.3f}{gain}"
            rows.append(f"| {group} | {name} | {rule} | {text} | {'ok' if ok else 'FAIL'} |")
            if not ok:
                failures.append(f"{group}: {name} ({rule}): {text}")
    return rows, failures


def energy(log, runner, plan_path, plan):
    """ON mean inside the OFF samples' range; ('text', ok or None when not evaluated)."""
    if not any(p.get("trace_at") for p in plan):
        return "no energy phases in this job", None
    try:
        from tools import m4_promotion_gate as m4
        from tools.live_batches.gi_1ray_qual.analyze import float_samples
        run = m4.Run(Path(log), None, Path(plan_path))
        lines = stamped_lines(Path(log).read_text(encoding="utf-8-sig", errors="replace"))
        run.phases = [(w["label"], w["applied"], w["end"])
                      for w in windows(Path(runner).read_text(encoding="utf-8-sig", errors="replace"), plan, lines)]
        v = {s: [x[0] / x[2] for x in float_samples(run, f"energy-{s}")] for s in ("off-a", "on", "off-b")}
    except (Exception, SystemExit) as e:   # missing traces or dumps: the run is incomplete for energy, not judged
        return f"not evaluated: {e}", None
    off = v["off-a"] + v["off-b"]
    on = statistics.mean(v["on"])
    ok = min(off) <= on <= max(off)
    return (f"normalized scene colour mean: OFF samples {min(off):.5f} to {max(off):.5f} (mean {statistics.mean(off):.5f}), "
            f"ON mean {on:.5f} ({(on / statistics.mean(off) - 1) * 100:+.2f}%)"), ok


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--job", required=True)
    ap.add_argument("--log", required=True)
    ap.add_argument("--runner", required=True)
    ap.add_argument("--out")
    a = ap.parse_args()
    job, plan, plan_path = flow.load_job(a.job)
    measured, guard, guard_all, wide = pab.measure(a.log, a.runner, plan)
    timed = [p for p in plan if p.get("frames") and not p.get("trace_at")]
    table, _ = pab.report(job, timed, measured, guard, guard_all, wide)
    # One complete period at least per window (a pan side has 1-2); check windows carry the rt stats line only.
    missing = [f"{p['label']}: no complete period of {name}" for p in timed
               for name in (("primary-ray check match",) if p["label"].startswith("check") else pab.REQUIRED)
               if not measured.get(p["label"], {}).get(name)]
    rows, failures = judge(measured, plan)
    etext, eok = energy(a.log, a.runner, plan_path, plan)
    if eok is False:
        failures.append("energy: " + etext)
    if eok is None and any(p.get("trace_at") for p in plan):
        missing.append("energy " + etext)
    verdict = "INVALID" if missing else "FAIL" if failures else "PASS (review the frame-time rows: gain or none)"
    text = "\n".join([f"# {job['name']}: {verdict}", ""] + [f"- missing: {m}" for m in missing] +
                     [f"- FAIL: {f}" for f in failures] +
                     ["", "## ON against the OFF pair (noise = |off-b - off-a| of the same group)", "",
                      "| Group | Line | Rule | Values | |", "| --- | --- | --- | --- | --- |"] + rows +
                     ["", "## Energy (static, exposure held, 3 float traces per state)", "", etext, "", table])
    if a.out:
        Path(a.out).write_text(text, encoding="utf-8")
    else:
        sys.stdout.reconfigure(encoding="utf-8")
        print(text)
    print(f"{job['name']}: {verdict}; {len(missing)} missing, {len(failures)} failing" + (f"; {a.out}" if a.out else ""))
    return int(bool(missing or failures))


if __name__ == "__main__":
    sys.exit(main())
