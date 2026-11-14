"""handout_qual verdict: rt_scene_handout ON against the OFF pair of the same run, per window group.

    py -3 tools/live_batches_staged/handout_qual/analyze.py --job <job.json> --log <game.log> --runner <runner.txt> --out <md>

Reads a finished run; no game or queue access. Window medians are perf_ab's (complete 120-frame periods after each
window's acknowledged stats reset). Every threshold is the OFF repeat of the same group (predicted.json "noise_rule"):
noise = |off-b - off-a|; verdicts by tools/live_batches/sane_gates.py (noise and a materiality floor; FAIL, EXPLAIN, INFO). Energy: gi_1ray_qual's float samples
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
from tools.live_batches import sane_gates as sane
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
    ("GI trace", "ms", r"gi: trace pass \(ray-traced.*GPU median ([\d.]+) ms"),
]
# (row, rule), judged by tools/live_batches/sane_gates.py (user rule 2026-11-14): FAIL only beyond the OFF pair's noise
# and the materiality floor in the bad direction. "lower" = the deliverable (a rise is bad, the drop is the design);
# "not higher" = a per-pass cost (a rise is bad; a drop beyond the floor is EXPLAIN: the handout removes a CPU wait, no
# GPU work, and the unexplained GI trace drop of 2026-11-14 was dropped hit-material work); "equal" = a count;
# "not lower" = a match percentage; "zero" = an event counter (one event is a fault).
JUDGED = [("scene wait mean", "lower"), ("frame time median", "lower"), ("frame time p99", "not higher"),
          ("RT", "not higher"), ("GI", "not higher"), ("GI trace", "not higher"), ("scene build", "not higher"), ("strand G-buffer draws", "equal"),
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


DECISIVE = ("scene wait mean", "frame time median", "primary-ray check match")
PREDICTED_LOWER = ("scene wait mean", "frame time median", "frame time p99")   # the design: the wait leaves the frame


def floor_of(name, off):
    if name == "primary-ray check match":
        return sane.MATCH_FLOOR_POINTS
    return sane.count_floor(off) if name == "strand G-buffer draws" else sane.cost_floor(off)


def judge(measured, plan):
    """(table rows, {status: [text]}, a decisive row was judged, ["... not judged"])."""
    rows, found, decisive, notes = [], {sane.FAIL: [], sane.EXPLAIN: [], sane.INFO: []}, False, []
    for group, states in groups(plan).items():
        for name, rule in JUDGED:
            a, on, b = (pooled(measured, states[s], name) for s in ("off-a", "on", "off-b"))
            if rule == "zero":
                if on is None:
                    continue
                status, text = (sane.OK if on == 0 else sane.FAIL), f"ON {on:g}"
            elif on is None or a is None or b is None:
                if name in DECISIVE and any(x is not None for x in (a, on, b)):
                    notes.append(f"{group}: {name} not judged (a state has no complete period)")
                continue
            else:
                floor = floor_of(name, (a + b) / 2)
                off_values = [v for st in ("off-a", "off-b") for label in states[st] for v in measured.get(label, {}).get(name, [])]
                status, d, noise = sane.compare(a, b, on, floor, rule, name in PREDICTED_LOWER, off_values)
                decisive = decisive or name in DECISIVE
                gain = " (gain beyond noise and floor)" if rule == "lower" and d < -max(noise, floor) else ""
                text = f"OFF {a:.3f} / {b:.3f}, ON {on:.3f}, ON - OFF {d:+.3f}, noise {noise:.3f}, floor {floor:.3f}{gain}"
            rows.append(f"| {group} | {name} | {rule} | {text} | {status} |")
            if status in found:
                found[status].append(f"{group}: {name} ({rule}): {text}")
    return rows, found, decisive, notes


# Per-pass float means of the energy traces (dumps.json, exposure held): where a difference first appears.
PASSES = [("G-buffer base colour (control: the same scene)", "aa4efa4d", 1), ("diffuse light after the composite", "d1669194", 0),
          ("scene colour before post (the energy quantity)", "5c3fdabc", 0), ("final frame before the UI", "d25b8086", 0)]
# Log lines per state window (energy and hit-* windows): the native hit-material path at work.
STATE_LINES = [("GI trace GPU median ms", r"gi: trace pass \(ray-traced.*GPU median ([\d.]+) ms"),
               ("scene wait mean ms", r"\[rt\] scene wait:.*wait mean ([\d.]+) ms"),
               ("frame time median ms", r"\[pace\] rendered frame time over \d+ frames: median ([\d.]+) ms"),
               ("GI far: ambient kept at qualified hits", r"GI far \(\d+ frames\):.*ambient kept at qualified hits ([\d.]+)"),
               ("GI far: bounce per ray", r"GI far \(\d+ frames\):.*bounce ([\d.]+) \(old rule"),
               ("GI hit direct: light rays traced", r"GI hit direct lights \(\d+ frames\): light rays traced (\d+)"),
               ("hit material query published (ordered)", r"\[hit material submit\].* ordered (\d+)"),
               ("hit material passes on the committed scene", r"\[core\] scene handout:.*committed scene (\d+) \(rt_scene_handout_hit_wait"),
               ("scene wait timeouts (max)", r"got this frame's scene, (\d+) timeouts"),
               ("handout fallbacks (max)", r"\[rt\] scene handout: \d+ frames,.*fallbacks (\d+) \(")]


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
    """(markdown rows, {line: {window: median}}, {line: {window: [period values]}}): per energy / hit-* window the
    median [n] of each STATE_LINES value."""
    lines = stamped_lines(Path(log).read_text(encoding="utf-8-sig", errors="replace"))
    wins = [w for w in windows(Path(runner).read_text(encoding="utf-8-sig", errors="replace"), plan, lines)
            if w["label"].startswith(("energy-", "hit-"))]
    rows, medians, periods = ["| Line | " + " | ".join(w["label"] for w in wins) + " |", "| --- |" + " --- |" * len(wins)], {}, {}
    for name, rx in STATE_LINES:
        cells = []
        for w in wins:
            v = [float(m[1]) for t, s in lines if w["applied"] <= t < w["end"] and (m := re.search(rx, s))]
            cells.append(f"{max(v) if name.endswith('(max)') else statistics.median(v):g} [n{len(v)}]" if v else "-")
            if v:   # "(max)" lines are event counters: the worst period of the window
                medians.setdefault(name, {})[w["label"]] = max(v) if name.endswith("(max)") else statistics.median(v)
                periods.setdefault(name, {})[w["label"]] = v
        rows.append(f"| {name} | " + " | ".join(cells) + " |")
    return (rows if wins else []), medians, periods


def hit_checks(medians, plan, expect=None):
    """({status: [text]}, ["... not judged"]) of the state windows' counters, where one event is a fault: a wait
    timeout or a handout fallback; the native hit path off (no qualified hit) in a state that does not predict it; the
    hit-wait fix not taking the committed scene in its own windows."""
    found, notes = {sane.FAIL: [], sane.EXPLAIN: [], sane.INFO: []}, []
    for name in ("scene wait timeouts (max)", "handout fallbacks (max)"):
        found[sane.FAIL] += [f"{label}: {name} {v:g}" for label, v in medians.get(name, {}).items() if v > 0]
    name = "GI far: ambient kept at qualified hits"
    for p in plan:
        label = p["label"]
        if not label.startswith("hit-"):
            continue
        state, v = label[len("hit-"):], medians.get(name, {}).get(label)
        if v is None:
            notes.append(f"{label}: window not judged (no '{name}' line)")
        elif (expect or {}).get(state) == "lower":
            found[sane.INFO].append(f"{label}: {name} {v:g} (the job predicts 0: the unpublished commit-time query)")
        elif v == 0:
            found[sane.FAIL].append(f"{label}: {name} 0: the native hit-material path is off in this state")
    # The hit work itself: light rays traced at hits equal to the OFF pair (a state that loses them is the 8.6 defect).
    name = "GI hit direct: light rays traced"
    rays = {k[len("hit-"):]: x for k, x in medians.get(name, {}).items() if k.startswith("hit-")}
    if "off-a" in rays and "off-b" in rays:
        off = (rays["off-a"] + rays["off-b"]) / 2
        for p in plan:
            state = p["label"][len("hit-"):]
            if not p["label"].startswith("hit-") or state.startswith("off") or (expect or {}).get(state) == "lower":
                continue
            if state not in rays:
                found[sane.FAIL].append(f"hit-{state}: no '{name}' line: no light ray at a hit (OFF {off:g})")
                continue
            status, d, noise = sane.compare(rays["off-a"], rays["off-b"], rays[state], sane.count_floor(off), "equal")
            if status in (sane.EXPLAIN, sane.FAIL):   # a count beyond noise and 1%: the hit work differs
                found[sane.FAIL].append(f"hit-{state}: {name} {rays[state]:g} against OFF {rays['off-a']:g} / "
                                        f"{rays['off-b']:g} (delta {d:+g}, noise {noise:g})")
    name = "hit material passes on the committed scene"
    for state in sorted({p["label"].split("-", 1)[1] for p in plan
                         if p["label"].startswith(("energy-", "hit-")) and "rt_scene_handout_hit_wait=1" in p.get("set", "")
                         and "rt_scene_handout=2" in p.get("set", "")}):
        v = [x for label, x in medians.get(name, {}).items() if label.split("-", 1)[1] == state]
        if not v:
            notes.append(f"{state}: hit-wait fix not judged (no '[core] scene handout' line in its windows)")
        elif max(v) == 0:
            found[sane.FAIL].append(f"{state}: {name} 0 with rt_scene_handout_hit_wait=1: the fix did not run")
    return found, notes


# (line, rule) of the energy windows (rt_stats off: the cost windows of a modes job) against the OFF pair.
STATE_COSTS = [("GI trace GPU median ms", "not higher"), ("scene wait mean ms", "lower"), ("frame time median ms", "lower")]


def state_costs(medians, expect=None, periods=None):
    """{status: [text]}: the costs of every energy state against the OFF pair. GI trace GPU ms is "not higher" (the
    handout predicts no GPU change, so a drop beyond the floor is EXPLAIN unless the job predicts it); the scene wait
    and the frame time are the deliverable ("lower": a rise beyond noise and floor FAILS; the delta is always shown).
    periods: {line: {window: [period values]}}: the noise then includes the spread of the single OFF periods, as in the
    window groups (judge); the OFF pair's two medians alone under-estimate it (hq-s8-modes 205520: mode 1 frame time
    +0.230 ms "failed" against a pair difference of 0.200 and a floor of 0.214)."""
    found = {sane.FAIL: [], sane.EXPLAIN: [], sane.INFO: []}
    for name, rule in STATE_COSTS:
        v = {k[len("energy-"):]: x for k, x in medians.get(name, {}).items() if k.startswith("energy-")}
        if "off-a" not in v or "off-b" not in v:
            continue
        for state, on in v.items():
            if state.startswith("off"):
                continue
            floor = sane.cost_floor((v["off-a"] + v["off-b"]) / 2)
            off_values = [x for w in ("energy-off-a", "energy-off-b") for x in (periods or {}).get(name, {}).get(w, [])]
            status, d, noise = sane.compare(v["off-a"], v["off-b"], on, floor, rule, (expect or {}).get(state) == "lower", off_values)
            text = (f"energy-{state}: {name}: OFF {v['off-a']:.3f} / {v['off-b']:.3f}, {state} {on:.3f}, delta {d:+.3f}, "
                    f"noise {noise:.3f}, floor {floor:.3f}")
            if status in found:
                found[status].append(text)
            elif rule == "lower":
                found[sane.INFO].append(text)
            elif (expect or {}).get(state) == "lower" and abs(d) > max(noise, floor):
                found[sane.INFO].append(text + " (predicted)")
    return found


def energy(log, runner, plan_path, plan, expect=None):
    """(text, {status: [text]} or None when not evaluated, per-pass rows). Each state's mean against the OFF samples
    (sane_gates.energy): inside their range ok; outside it under the visibility floor EXPLAIN; beyond it FAIL.
    expect: {state: "lower"}: a loss the run was built to reproduce is INFO. A state with fewer traces than planned is
    judged on what it has (2 at least), with a note."""
    if not any(p.get("trace_at") for p in plan):
        return "no energy phases in this job", None, []
    try:
        from tools import m4_promotion_gate as m4
        from tools.live_batches.gi_1ray_qual.analyze import float_samples
        run = m4.Run(Path(log), None, Path(plan_path))
        lines = stamped_lines(Path(log).read_text(encoding="utf-8-sig", errors="replace"))
        run.phases = [(w["label"], w["applied"], w["end"])
                      for w in windows(Path(runner).read_text(encoding="utf-8-sig", errors="replace"), plan, lines)]
        asked = {p["label"]: len(p["trace_at"]) for p in plan if p.get("trace_at")}
        v = {s: [x[0] / x[2] for x in float_samples(run, f"energy-{s}", None)] for s in energy_states(plan)}
        passes = pass_means(run, plan)
    except (Exception, SystemExit) as e:   # no traces or dumps at all: energy is not judged
        return f"not evaluated: {e}", None, []
    short = [f"energy-{s}: {len(x)} of {asked['energy-' + s]} float traces" for s, x in v.items() if len(x) != asked["energy-" + s]]
    v = {s: x for s, x in v.items() if len(x) >= 2}
    off = [x for s, xs in v.items() if s.startswith("off") for x in xs]
    others = [s for s in v if not s.startswith("off")]
    if sum(s.startswith("off") for s in v) < 2 or not others:
        return "not evaluated: fewer than 2 float traces in an OFF repeat or in every other state" + "".join("; " + t for t in short), None, []
    found, parts = {sane.FAIL: [], sane.EXPLAIN: [], sane.INFO: []}, []
    for s, mean, pct, z in against_off(v):
        status, _ = sane.energy(off, mean, (expect or {}).get(s) == "lower")
        text = (f"energy-{s}: mean {mean:.5f}, {pct:+.2f}% of the OFF mean ({z:+.1f} standard errors), OFF samples "
                f"{min(off):.5f} to {max(off):.5f}, visibility floor {sane.ENERGY_FLOOR_REL:.1%}" +
                (" (predicted by the job)" if (expect or {}).get(s) == "lower" and status == sane.INFO else ""))
        parts.append(f"{text}: {status}")
        if status in found:
            found[status].append(text)
    per = "; ".join(f"{s} n={len(x)} mean {statistics.mean(x):.5f} sd {statistics.stdev(x):.5f} "
                    f"({min(x):.5f} to {max(x):.5f})" for s, x in v.items())
    rows = ["| Pass (float mean of rgb) | OFF mean | " + " | ".join(others) + " |", "| --- | --- |" + " --- |" * len(others)]
    for name, values in passes.items():
        off_p = [x for s, xs in values.items() if s.startswith("off") for x in xs]
        cells = {s: f"{pct:+.3f}% ({z:+.1f} se)" for s, _, pct, z in against_off(values)}
        if off_p:
            rows.append(f"| {name} | {statistics.mean(off_p):.5f} | " + " | ".join(cells.get(s, "-") for s in others) + " |")
    return ("Normalized scene colour mean. " + ". ".join(parts) + f". Per state: {per}" +
            "".join(f". Note: {t}" for t in short)), found, rows


def verdict_of(found, invalid):
    """INVALID (no decisive window judged), FAIL, EXPLAIN (no failure, unexplained differences listed) or PASS."""
    if invalid:
        return "INVALID"
    if found[sane.FAIL]:
        return "FAIL"
    return f"EXPLAIN ({len(found[sane.EXPLAIN])} unexplained, no failure)" if found[sane.EXPLAIN] else "PASS"


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
    # A window without it is not judged; the run is judged on the others.
    judged, notes = sane.split_windows(
        [p["label"] for p in timed], measured,
        lambda label: ("primary-ray check match",) if label.startswith("check") else pab.REQUIRED)
    rows, found, decisive, group_notes = judge(measured, plan)
    etext, efound, epasses = energy(a.log, a.runner, plan_path, plan, job.get("energy_expect"))
    states, medians, periods = state_lines(a.log, a.runner, plan)
    has_energy = any(p.get("trace_at") for p in plan)
    hfound, hnotes = hit_checks(medians, plan, job.get("energy_expect"))
    notes += hnotes
    for more in (efound or {}), state_costs(medians, job.get("energy_expect"), periods), hfound:
        for status, texts in more.items():
            found[status] += texts
    if has_energy and efound is None:
        notes.append("energy not judged: " + etext)
    # Decisive: a judged scene wait / frame time / primary-ray row of an OFF / ON / OFF group, or the energy states;
    # a job with neither (one state, the smoke) needs one window with its required lines.
    if groups(plan) or has_energy:
        invalid = not (decisive or (has_energy and efound is not None))
    else:
        invalid = not judged
    verdict = verdict_of(found, invalid)
    text = "\n".join([f"# {job['name']}: {verdict}", ""] +
                     [f"- {status}: {t}" for status in (sane.FAIL, sane.EXPLAIN) for t in found[status]] +
                     [f"- not judged: {n}" for n in notes + group_notes] +
                     [f"- INFO (under the floor, or predicted): {t}" for t in found[sane.INFO]] +
                     ["", sane.floors_text(),
                      "", "## ON against the OFF pair (noise = the larger of |off-b - off-a| and half the range of the OFF periods)", "",
                      "| Group | Line | Rule | Values | |", "| --- | --- | --- | --- | --- |"] + rows +
                     ["", "## Energy (static, exposure held, float traces per state as planned)", "", etext, ""] + epasses +
                     (["", "## Native hit-material path per state window (median [n] of the lines inside)", ""] + states
                      if states else []) + ["", table])
    if a.out:
        Path(a.out).write_text(text, encoding="utf-8")
    else:
        sys.stdout.reconfigure(encoding="utf-8")
        print(text)
    print(f"{job['name']}: {verdict}; {len(found[sane.FAIL])} failing, {len(found[sane.EXPLAIN])} to explain, "
          f"{len(notes + group_notes)} not judged, {len(found[sane.INFO])} info" + (f"; {a.out}" if a.out else ""))
    return int(invalid or bool(found[sane.FAIL]))


if __name__ == "__main__":
    sys.exit(main())
