"""handout_qual verdict: rt_scene_handout ON against the OFF pair of the same run, per window group.

    py -3 tools/live_batches_staged/handout_qual/analyze.py --job <job.json> --log <game.log> --runner <runner.txt> --out <md>

Reads a finished run; no game or queue access. Window medians are perf_ab's (complete 120-frame periods after each
window's acknowledged stats reset). Every threshold is the OFF repeat of the same group (predicted.json "noise_rule"):
noise = |off-b - off-a|; a gain is ON below the OFF mean by more than it. Energy: gi_1ray_qual's float samples
(dumps.json scene colour mean / pre-exposure), each state's mean inside the range of the OFF samples
(states are the energy-<state> phases; OFF repeats start with "off"), with the per-pass means and the hit-material lines.
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


# Per-pass float means of the energy traces (dumps.json, exposure held): where a difference first appears.
PASSES = [("G-buffer base colour (control: the same scene)", "aa4efa4d", 1), ("diffuse light after the composite", "d1669194", 0),
          ("scene colour before post (the energy quantity)", "5c3fdabc", 0), ("final frame before the UI", "d25b8086", 0)]
# Log lines per state window (energy and hit-* windows): the native hit-material path at work.
STATE_LINES = [("GI trace GPU median ms", r"gi: trace pass \(ray-traced.*GPU median ([\d.]+) ms"),
               ("scene wait mean ms", r"\[rt\] scene wait:.*wait mean ([\d.]+) ms"),
               ("GI far: ambient kept at qualified hits", r"GI far \(\d+ frames\):.*ambient kept at qualified hits ([\d.]+)"),
               ("GI far: bounce per ray", r"GI far \(\d+ frames\):.*bounce ([\d.]+) \(old rule"),
               ("GI hit direct: light rays traced", r"GI hit direct lights \(\d+ frames\): light rays traced (\d+)"),
               ("hit material query published (ordered)", r"\[hit material submit\].* ordered (\d+)"),
               ("hit material passes on the committed scene", r"\[core\] scene handout:.*committed scene (\d+) \(rt_scene_handout_hit_wait")]


def energy_states(plan):
    return [p["label"][len("energy-"):] for p in plan if p.get("trace_at") and p["label"].startswith("energy-")]


def pass_means(run, plan):
    """{pass name: {state: [mean of the rgb means per trace]}} from each energy trace's dumps.json."""
    import json
    from tools import m4_promotion_gate as m4
    out = {name: {} for name, _, _ in PASSES}
    for state in energy_states(plan):
        _, start, end = run.phase_of("energy-" + state)
        for t, txt in run.lines:
            m = m4.TRACE.search(txt) if start <= t < end else None
            if not m:
                continue
            dumps = json.loads((Path(m.group(2).strip()) / "dumps.json").read_text(encoding="utf-8"))
            for name, fs, slot in PASSES:
                cand = [(int(k[1:]), d) for k, d in dumps.items()
                        if str(d.get("pass_fs", "")).startswith(fs) and d.get("slot") == slot and d.get("mean")]
                if cand:
                    rgb = max(cand, key=lambda c: c[0])[1]["mean"][:3]
                    out[name].setdefault(state, []).append(sum(rgb) / len(rgb))
    return out


def against_off(values):
    """[(state, mean, delta % of the OFF mean, delta / standard error)] for the states that are not OFF repeats."""
    off = [x for s, v in values.items() if s.startswith("off") for x in v]
    if len(off) < 2:
        return []
    rows = []
    for s, v in values.items():
        if s.startswith("off") or not v:
            continue
        d = statistics.mean(v) - statistics.mean(off)
        se = (statistics.variance(off) / len(off) + (statistics.variance(v) / len(v) if len(v) > 1 else 0)) ** .5
        rows.append((s, statistics.mean(v), d / statistics.mean(off) * 100, d / se if se else float("inf")))
    return rows


def state_lines(log, runner, plan):
    """Markdown rows: per energy / hit-* window the median [n] of each STATE_LINES value logged inside it."""
    lines = stamped_lines(Path(log).read_text(encoding="utf-8-sig", errors="replace"))
    wins = [w for w in windows(Path(runner).read_text(encoding="utf-8-sig", errors="replace"), plan, lines)
            if w["label"].startswith(("energy-", "hit-"))]
    rows = ["| Line | " + " | ".join(w["label"] for w in wins) + " |", "| --- |" + " --- |" * len(wins)]
    for name, rx in STATE_LINES:
        cells = []
        for w in wins:
            v = [float(m[1]) for t, s in lines if w["applied"] <= t < w["end"] and (m := re.search(rx, s))]
            cells.append(f"{statistics.median(v):g} [n{len(v)}]" if v else "-")
        rows.append(f"| {name} | " + " | ".join(cells) + " |")
    return rows if wins else []


def energy(log, runner, plan_path, plan, expect=None):
    """Each state's mean inside the OFF samples' range; ('text', ok or None when not evaluated, per-pass rows).
    expect: {state: "equal" (default) | "lower" (a predicted loss: reported, not judged)}."""
    if not any(p.get("trace_at") for p in plan):
        return "no energy phases in this job", None, []
    try:
        from tools import m4_promotion_gate as m4
        from tools.live_batches.gi_1ray_qual.analyze import float_samples
        run = m4.Run(Path(log), None, Path(plan_path))
        lines = stamped_lines(Path(log).read_text(encoding="utf-8-sig", errors="replace"))
        run.phases = [(w["label"], w["applied"], w["end"])
                      for w in windows(Path(runner).read_text(encoding="utf-8-sig", errors="replace"), plan, lines)]
        # As many samples as the phase's plan asks for (hq-s8-energy: 8; float_samples' default 3 made that run INVALID).
        asked = {p["label"]: len(p["trace_at"]) for p in plan if p.get("trace_at")}
        v = {s: [x[0] / x[2] for x in float_samples(run, f"energy-{s}", asked[f"energy-{s}"])] for s in energy_states(plan)}
        passes = pass_means(run, plan)
    except (Exception, SystemExit) as e:   # missing traces or dumps: the run is incomplete for energy, not judged
        return f"not evaluated: {e}", None, []
    off = [x for s, xs in v.items() if s.startswith("off") for x in xs]
    others = [s for s in v if not s.startswith("off")]
    if len(off) < 2 or not others:
        return "not evaluated: no OFF pair or no other state", None, []
    ok, parts = True, []
    for s, mean, pct, z in against_off(v):
        inside = min(off) <= mean <= max(off)
        rule = (expect or {}).get(s, "equal")
        ok = ok and (inside or rule != "equal")
        parts.append(f"{s} mean {mean:.5f} ({pct:+.2f}%, {z:+.1f} standard errors): " +
                     ("inside the OFF range" if inside else "OUTSIDE the OFF range") +
                     ("" if rule == "equal" else f" (predicted {rule}, not judged)"))
    per = "; ".join(f"{s} n={len(x)} mean {statistics.mean(x):.5f} sd {statistics.stdev(x):.5f} "
                    f"({min(x):.5f} to {max(x):.5f})" for s, x in v.items())
    rows = ["| Pass (float mean of rgb) | OFF mean | " + " | ".join(others) + " |", "| --- | --- |" + " --- |" * len(others)]
    for name, values in passes.items():
        off_p = [x for s, xs in values.items() if s.startswith("off") for x in xs]
        cells = {s: f"{pct:+.3f}% ({z:+.1f} se)" for s, _, pct, z in against_off(values)}
        if off_p:
            rows.append(f"| {name} | {statistics.mean(off_p):.5f} | " + " | ".join(cells.get(s, "-") for s in others) + " |")
    return (f"normalized scene colour mean: OFF samples {min(off):.5f} to {max(off):.5f} (mean {statistics.mean(off):.5f}). "
            + ". ".join(parts) + f". Per state: {per}"), ok, rows


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--job", required=True)
    ap.add_argument("--log", required=True)
    ap.add_argument("--runner", required=True)
    ap.add_argument("--out")
    a = ap.parse_args()
    job, plan, plan_path = flow.load_job(a.job)
    measured, guard, guard_all, wide = pab.measure(a.log, a.runner, plan)
    # hit-* windows (rt_stats on) carry the native hit lines only: their own table below, no cost rows.
    timed = [p for p in plan if p.get("frames") and not p.get("trace_at") and not p["label"].startswith("hit-")]
    table, _ = pab.report(job, timed, measured, guard, guard_all, wide)
    # One complete period at least per window (a pan side has 1-2); check windows carry the rt stats line only.
    missing = [f"{p['label']}: no complete period of {name}" for p in timed
               for name in (("primary-ray check match",) if p["label"].startswith("check") else pab.REQUIRED)
               if not measured.get(p["label"], {}).get(name)]
    rows, failures = judge(measured, plan)
    etext, eok, epasses = energy(a.log, a.runner, plan_path, plan, job.get("energy_expect"))
    states = state_lines(a.log, a.runner, plan)
    if eok is False:
        failures.append("energy: " + etext)
    if eok is None and any(p.get("trace_at") for p in plan):
        missing.append("energy " + etext)
    verdict = "INVALID" if missing else "FAIL" if failures else "PASS (review the frame-time rows: gain or none)"
    text = "\n".join([f"# {job['name']}: {verdict}", ""] + [f"- missing: {m}" for m in missing] +
                     [f"- FAIL: {f}" for f in failures] +
                     ["", "## ON against the OFF pair (noise = |off-b - off-a| of the same group)", "",
                      "| Group | Line | Rule | Values | |", "| --- | --- | --- | --- | --- |"] + rows +
                     ["", "## Energy (static, exposure held, float traces per state as planned)", "", etext, ""] + epasses +
                     (["", "## Native hit-material path per state window (median [n] of the lines inside)", ""] + states
                      if states else []) + ["", table])
    if a.out:
        Path(a.out).write_text(text, encoding="utf-8")
    else:
        sys.stdout.reconfigure(encoding="utf-8")
        print(text)
    print(f"{job['name']}: {verdict}; {len(missing)} missing, {len(failures)} failing" + (f"; {a.out}" if a.out else ""))
    return int(bool(missing or failures))


if __name__ == "__main__":
    sys.exit(main())
