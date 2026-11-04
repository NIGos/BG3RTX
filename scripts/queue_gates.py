"""Every-window gates for queue jobs ("expect_every"), evaluated by scripts/test-queue.ps1.

A job's "expect" regex passes when ANY line of the run matches, so one good window can hide
bad ones (m4i 093913: one 0.0000% clip window passed while the other ON windows showed
2.6-3.1%). An "expect_every" entry instead requires EVERY selected stats line inside the
named phase windows to match:

    "expect_every": [
      { "select": "hit material instanced clip \\(\\d+ frames\\)",   # which lines are judged
        "match":  "rejected \\d+ = 0\\.0[0-3]\\d\\d% of all lookups",  # each must match this
        "windows": ["diag-on"],     # move-plan phase labels (omit: every selected line of the run)
        "min": 1 }                  # complete lines required per window (default 1)
    ]

Windows come from the runner log: auto-run prints "HH:MM:SS.fff [pan-plan] window N ..." when
phase N of the -MovePlan starts; the plan gives its label. A window ends where the next one
starts (or at "[motion-plan] complete"). Its settings count as applied at the first
"[cfg] settings read" in the game log at or after the window start.

A stats line summarizes the frames since the previous line of its kind, so it is judged for a
window only when that previous line was also printed after the window's settings were applied:
then every frame it covers ran with the window's settings. The first line after a toggle
straddles two windows and is skipped (not passed). A motion-ready record naming
stats_reset=N plus the matching game `[stats] reset token=N frame=F` acknowledgment
starts a fresh period instead, so its first complete line counts. Frame plans which
request a reset fail if that acknowledgment is missing. Legacy logs retain the
straddle rule.

An entry with "event": true judges one-off lines instead (e.g. "[trace] frame N written" in each
shot-* window): every selected line from the window's applied settings to its end counts, with
no straddle rule, and "min" is the number of events the window must hold. A snapshot line printed by a query
recorded before the settings read still shows the old settings: "settle_s" (default 0) skips event lines
earlier than that after the read. Derive it as the frames in flight x the window's frame time (hig1 09:54: frame
1679 printed key-on values 28 ms after the key-off read; 8 scene slots x 0.11 s at rt_stats = 0.9 s -> 1 s).

An entry with "when_present": true judges a line that only a pass which ran prints: a window
without any selected line (not even a straddling one) passes, as the pass did not run there;
once the window has one, "min" and "match" apply as usual. The cascade-capacity gate is one: the
casters line counts the sun cascade's frames, and an interior has none (m5p2 Flymm 11-01: 4 lines
during the load, none in a cost window, the run INVALID for a buffer that never filled).

An entry with "trend": true (long sessions, tools/live_batches/long_session) has "value" (a regex whose
group 1 is a number), "baseline" and "tail" window labels, and optional "abs" / "rel" allowances: the
tail's median must not exceed the baseline's max plus the baseline's own spread plus the allowance
(trend_failures). It catches a pool, VRAM or frame time that keeps growing over a session.

Plan drift: the runner's "[pan-plan] window N ... set=<keys>" line carries the keys the phase
applied. When the plan's step N has a "set" and it differs, the plan file changed after the run
(m4p3 / m4p4 11:2x ran before cost-ao0 was inserted, so every later label was one phase off):
the windowed gates then fail with "plan drift" instead of judging the wrong windows.

    py -3 scripts/queue_gates.py --gates gates.json --log run.log [--runner runner.txt --plan plan.json]
    py -3 scripts/queue_gates.py --lint-exposure-hold [tools/live_batches]
    py -3 scripts/queue_gates.py --lint-scene-wait [tools/live_batches | tools/live_batches/<batch>]
    py -3 scripts/queue_gates.py --lint-capacity [tools/live_batches | tools/live_batches/<batch>]   (CAPACITY_GATES)

Exposure hold lint: a job whose plan has an energy gate (an ON shot between two OFF shots, as
tools/m4_promotion_gate.py compares them) must set qa_exposure_hold=1 in every phase from its first compared
shot to its last (not in phase 0: the hold latches the exposure of the moment) and expect the
"[rrpost] exposure held" line. Unheld, the eye adaptation moved the 2026-11-01 gi_far S8 ON shot by +3.2%.

Scene-wait lint: every lean job (a -MovePlan with cost-* windows) must gate "[rt] scene wait" with 0 timeouts
in every cost-* window with RT on and no float traces (rt_scene_wait on), with a log cadence that gives each such window
complete lines. A timeout means the RT passes traced the previous frame's scene (shadows detach while turning):
the hit-material runs of 2026-11-01 had 15-456 per 600 frames and no gate saw them (SCENE_WAIT_GATE below).

Prints one line per failure and exits 1 if any gate fails; exit 0 and "every-window gates: ok".
"""
import argparse
import json
import math
import re
import sys
from pathlib import Path

_TS = re.compile(r"(\d\d):(\d\d):(\d\d)\.(\d{3})")
_WINDOW = re.compile(r"(\d\d:\d\d:\d\d\.\d{3}) \[pan-plan\] window (\d+)\b")
_COMPLETE = re.compile(r"(\d\d:\d\d:\d\d\.\d{3}) \[motion-plan\] complete")
_CFG = re.compile(r"\[cfg\] settings read")
_SET = re.compile(r" set=(\S*)")


def _sec(stamp):
    m = _TS.search(stamp)
    if not m:
        return None
    h, mi, s, ms = (int(x) for x in m.groups())
    return h * 3600 + mi * 60 + s + ms / 1000.0


class _Clock:
    """Turns HH:MM:SS.fff into monotonic seconds across a midnight rollover."""

    def __init__(self, origin=None):
        self.origin = origin
        self.last = None
        self.offset = 0.0

    def __call__(self, t):
        if t is None:
            return None
        if self.last is None and self.origin is not None:
            self.offset = round((self.origin - t) / 86400) * 86400
        if self.last is not None and t + self.offset < self.last - 43200:
            self.offset += 86400
        self.last = t + self.offset
        return self.last


def stamped_lines(text):
    """[(seconds, line)] for the log lines that start with a timestamp."""
    clock = _Clock()
    out = []
    for line in text.splitlines():
        m = _TS.match(line)
        if m:
            out.append((clock(_sec(m.group(0))), line))
    return out


def windows(runner_text, plan, game_lines):
    """[{label, index, start, applied, end, set}] from the runner's [pan-plan] lines."""
    clock = _Clock(game_lines[0][0] if game_lines else None)
    starts, complete, ready, reset_tokens = [], None, {}, {}
    for line in runner_text.splitlines():
        timestamp = clock(_sec(line))
        rm = re.search(r"\[motion-ready\] phase=(\d+)", line)
        if rm:
            ready[int(rm[1])] = timestamp
            token = re.search(r" stats_reset=(\d+)", line)
            if token:
                reset_tokens[int(rm[1])] = int(token[1])
        m = _WINDOW.search(line)
        if m:
            sm = _SET.search(line)
            starts.append((timestamp, int(m.group(2)), sm.group(1) if sm else None))
            continue
        m = _COMPLETE.search(line)
        if m:
            complete = timestamp
    out = []
    for k, (start, index, applied_set) in enumerate(starts):
        end = starts[k + 1][0] if k + 1 < len(starts) else (complete if complete is not None else float("inf"))
        label = plan[index].get("label", str(index)) if 0 <= index < len(plan) else str(index)
        applied = next((t for t, l in game_lines if start <= t < end and _CFG.search(l)), start)
        settings_applied = applied
        waiting = 0 <= index < len(plan) and bool(plan[index].get("wait_log") or plan[index].get("stats_reset"))
        if waiting:
            applied = max(applied, ready.get(index, float("inf")))
        # Only the exact reset acknowledged for this phase establishes a fresh
        # producer period. Legacy logs keep the conservative straddle rule;
        # reset-requesting plans require the matching acknowledgment.
        token = reset_tokens.get(index)
        reset = next((t for t, l in game_lines if token is not None and settings_applied <= t < end
                      and re.search(r"\[stats\] reset token=" + str(token) + r" frame=\d+\b", l)), None)
        out.append({"stats_reset": reset, "settings_applied": settings_applied, "ready": (not waiting or index in ready) and (not (0 <= index < len(plan) and plan[index].get("stats_reset")) or reset is not None), "label": label, "index": index, "start": start, "applied": applied, "end": end, "set": applied_set})
    return out


def complete_period_lines(picked, window):
    boundary = window.get("stats_reset")
    previous = boundary
    if boundary is None:
        boundary = window["applied"]
    out = []
    for t, line in picked:
        if t >= window["end"]:
            break
        if t >= boundary and previous is not None and previous >= boundary:
            out.append(line)
        if t >= boundary:
            previous = t
    return out


def plan_drift(wins, plan):
    """Windows whose runner `set=` differs from the plan step's "set" (the plan changed after the run)."""
    out = []
    for w in wins:
        i = w["index"]
        if w.get("set") is None or not (0 <= i < len(plan)) or "set" not in plan[i]:
            continue
        if str(plan[i]["set"]) != w["set"]:
            out.append(w)
    return out


def _median(v):
    s = sorted(v)
    n = len(s)
    return s[n // 2] if n % 2 else 0.5 * (s[n // 2 - 1] + s[n // 2])


def trend_failures(g, name, picked, wins):
    """A "trend" gate (long sessions): a value must not grow between early and late windows.

    Every selected line inside the "baseline" windows and inside the "tail" windows counts (event
    semantics: from the window's applied settings to its end); "value" is a regex whose first group is
    the number. The baseline's own spread (max - min) is the noise, derived from the run itself: the tail
    median must stay <= baseline max + spread + allow, where allow = max("abs", "rel" x baseline median)
    covers the value's print precision or a documented allowance. "min" lines are needed on each side.
    """
    val = re.compile(g["value"])
    need = int(g.get("min", 2))
    out = []
    side = {}
    for part in ("baseline", "tail"):
        labels = set(g[part])
        found = {w["label"] for w in wins if w["label"] in labels}
        if found != labels:
            out.append(f"trend {name}: {part} window(s) not in the runner log: {sorted(labels - found)[:4]}")
        vals = []
        for w in wins:
            if w["label"] not in labels:
                continue
            for t, l in picked:
                if w["applied"] <= t < w["end"]:
                    m = val.search(l)
                    if m:
                        vals.append(float(m.group(1)))
        if len(vals) < need:
            out.append(f"trend {name}: {len(vals)} {part} value(s), need {need}")
        side[part] = vals
    if out:
        return out
    base, tail = side["baseline"], side["tail"]
    allow = max(float(g.get("abs", 0.0)), float(g.get("rel", 0.0)) * abs(_median(base)))
    limit = max(base) + (max(base) - min(base)) + allow
    if _median(tail) > limit:
        out.append(f"trend {name}: grew: tail median {_median(tail):.4g} over {len(tail)} lines > limit {limit:.4g} "
                   f"(baseline {min(base):.4g}-{max(base):.4g} median {_median(base):.4g} over {len(base)} lines, allow {allow:.4g})")
    return out


def line_matches(gate, line):
    """items: repeated named value/active groups, bound regex group 1.

    Every active item must meet the bound (and the upper bound, regex group 1 of items.upper, when given).
    Empty inventories fail.
    required_if requires this gate only with complete matching trigger periods in
    the same window. A separate gate must require the trigger producer.
    first_only judges the first selected startup line.
    share: regex group 1 / product(remaining groups + offsets) <= max on
    every line, in addition to match/items. Missing or nonpositive denominators fail.
    """
    if not re.search(gate.get("match", ""), line):
        return False
    share = gate.get('share')
    if share:
        # Group 1 is the numerator; remaining groups multiply to its denominator.
        # Offsets allow a conservative lower bound for rounded counters.
        found = re.search(share['pattern'], line)
        if not found:
            return False
        try:
            values = [float(v) for v in found.groups()]
            offsets = share.get('offsets', [0] * (len(values) - 1))
            factors = [v + o for v, o in zip(values[1:], offsets)]
            denominator = math.prod(factors)
            if (len(offsets) != len(values) - 1 or not all(math.isfinite(v) and v >= 0 for v in values)
                    or not all(v > 0 for v in factors) or not math.isfinite(denominator)
                    or not 0 <= float(share['max']) < math.inf
                    or values[0] > float(share['max']) * denominator):
                return False
        except (ValueError, TypeError, OverflowError):
            return False
    rule = gate.get("items")
    if not rule:
        return True
    bound = re.search(rule["bound"], line)
    items = list(re.finditer(rule["pattern"], line))
    if not items and rule.get('empty') and re.search(rule['empty'], line):
        return True
    upper = re.search(rule["upper"], line) if rule.get("upper") else None
    if rule.get("upper") and not upper:
        return False
    try:
        return bool(bound and items) and all(float(m["active"]) == 0 or (
            float(m["value"]) >= float(bound[1]) and (upper is None or float(m["value"]) <= float(upper[1]))) for m in items)
    except ValueError:
        return False


def check(gates, game_text, runner_text="", plan=None):
    """Failure strings for the expect_every gates (empty list: every gate holds)."""
    plan = plan or []
    lines = stamped_lines(game_text)
    wins = None
    drift = []
    failures = []
    for g in gates:
        sel = re.compile(g["select"])
        match = re.compile(g.get("match", ""))
        need = int(g.get("min", 1))
        picked = [(t, l) for t, l in lines if sel.search(l)]
        name = g.get("name") or g["select"]
        if g.get("trend"):
            if wins is None:
                wins = windows(runner_text, plan, lines)
                drift = plan_drift(wins, plan)
            if drift:
                failures.append(f"trend {name}: plan drift, the windows are not trusted")
            else:
                failures += trend_failures(g, name, picked, wins)
            continue
        labels = g.get("windows")
        if not labels:
            bad = [l for _, l in (picked[:1] if g.get("first_only") else picked) if not line_matches(g, l)]
            if len(picked) < need:
                failures.append(f"every-window {name}: {len(picked)} line(s) in the run, need {need}")
            if bad:
                failures.append(f"every-window {name}: {len(bad)} of {len(picked)} line(s) fail /{g['match']}/; first: {bad[0][:200]}")
            continue
        if wins is None:
            wins = windows(runner_text, plan, lines)
            drift = plan_drift(wins, plan)
            if drift:
                w = drift[0]
                failures.append(f"plan drift: {len(drift)} window(s) ran other keys than the plan says (first: window "
                                f"{w['index']} '{w['label']}' ran set={w['set'][:120]}); the plan changed after the run, "
                                f"window labels are not trusted")
        if drift:
            continue
        event = bool(g.get("event"))
        settle = float(g.get("settle_s", 0.0))   # event lines this soon after the settings read are skipped (docstring)
        for label in labels:
            hits = [w for w in wins if w["label"] == label]
            if not hits:
                failures.append(f"every-window {name}: window '{label}' not found in the runner log")
                continue
            for w in hits:
                if not w.get("ready", True):
                    failures.append(f"every-window {name}: window '{label}' missing motion-ready completion")
                    continue
                if g.get("when_present") and not any((w.get("stats_reset") if not event and w.get("stats_reset") is not None else w["applied"]) <= t < w["end"] for t, _ in picked):
                    continue   # the pass did not run in this window (docstring)
                condition = g.get("required_if")
                if condition:
                    trigger = [(t, l) for t, l in lines if re.search(condition["select"], l)]
                    complete = complete_period_lines(trigger, w)
                    if not any(re.search(condition["match"], l) for l in complete):
                        continue
                if event:
                    judged = [l for t, l in picked
                              if w["applied"] + (0 if w.get("stats_reset") is not None else settle) <= t < w["end"]]
                else:
                    judged = complete_period_lines(picked, w)
                bad = [l for l in (judged[:1] if g.get("first_only") else judged) if not line_matches(g, l)]
                where = f"window '{label}' (phase {w['index']})"
                if len(judged) < need:
                    kind = "event line(s)" if event else "complete line(s)"
                    failures.append(f"every-window {name}: {where} has {len(judged)} {kind}, need {need}")
                if bad:
                    failures.append(f"every-window {name}: {where}: {len(bad)} of {len(judged)} line(s) fail /{g['match']}/; first: {bad[0][:200]}")
    return failures


# Exposure hold lint. The lighting is pre-exposed by s1b13[0] = 0.8333 / 2^EV and the game's eye adaptation
# moves EV between an OFF and an ON shot (gi_far S8 2026-11-01: s1b13[0] +3.2% in the ON shot, raw scene colour
# +2.87%, -0.32% at a fixed exposure; m5w4 +2.2% for a key that only removes light, -0.55% at a fixed
# exposure). qa_exposure_hold=1 (layer/rrpost.cpp) latches the exposure when it is switched on, so a job with an
# energy gate must hold it, unbroken, over every shot-* phase it compares. The energy gate is the one of
# tools/m4_promotion_gate.py: an ON shot between at least two OFF shots of one family (energy_triples).
_HOLD = re.compile(r"(?:^|,)qa_exposure_hold=(\d+)")


def _held(step):
    m = _HOLD.search(str(step.get("set", "")))
    return bool(m and m.group(1) != "0")


def exposure_hold_problems(plan, job=None):
    """Problems (strings) of a plan with an energy gate and no correct exposure hold; [] when fine or no gate."""
    root = str(Path(__file__).resolve().parent.parent)
    if root not in sys.path:
        sys.path.insert(0, root)
    from tools.m4_promotion_gate import energy_triples   # one definition of "energy-gated"
    labels = [str(p.get("label", "")) for p in plan]
    trip = energy_triples(labels)
    if not trip:
        return []
    out = []
    shots = sorted({labels.index(x) for t in trip for x in t[:3]})
    first, last = shots[0], shots[-1]
    if plan and _held(plan[0]):
        out.append(f"exposure hold: phase 0 '{labels[0]}' holds; the hold latches the current exposure, so it needs a warm-up phase without it")
    # Unbroken from the first compared shot to the last: a release and a new latch would hold another EV.
    gaps = [labels[i] for i in range(first, last + 1) if not _held(plan[i])]
    if gaps:
        out.append(f"exposure hold: energy-gated shots without qa_exposure_hold=1 (or released between them): {', '.join(gaps[:6])}"
                   f"{' ...' if len(gaps) > 6 else ''}")
    if job is not None and not out:
        exp = " ".join(job.get("expect", []))
        if "exposure held" not in exp:
            out.append("exposure hold: the job's expect has no '[rrpost] exposure held' line (a hold that never latched would pass)")
    return out


# Scene-wait gate (layer/rtscene.cpp wait_for_frame_scene). The canonical entry; a job may name it differently
# but its select/match must behave like this one on SCENE_WAIT_GOOD / SCENE_WAIT_BAD. 0 timeouts is the bound:
# without rt_hit_material the cost windows of lr1 camp 08:39 had 0 in every steady line (the only ones were the
# first line after the load, which no cost window judges), so any timeout in a cost window is a regression.
SCENE_WAIT_GATE = {
    "name": "scene wait: every cost window traces its own frame's scene (0 timeouts; stale shadows otherwise)",
    "select": r"\[rt\] scene wait: \d+ frames",
    "match": r"got this frame's scene, 0 timeouts",
    "min": 1,
}
SCENE_WAIT_GOOD = ("08:40:14.044  [rt] scene wait: 120 frames, 120 waited (120 got this frame's scene, 0 timeouts, "
                   "0 skipped), wait mean 2.005 ms, max 6.789 ms")
SCENE_WAIT_BAD = ("17:28:29.254  [rt] scene wait: 120 frames, 120 waited (119 got this frame's scene, 1 timeouts, "
                  "0 skipped), wait mean 2.169 ms, max 12.993 ms")
# The lowest frame rate of a lean cost window: the unfocused / FG 2 pacing floor of 30 rendered fps (the
# "30 fps ceiling" of every unfocused run, TRACK_D_SCENE_BUILD.md). A window judges a line only when the line
# before it was also printed after the window's settings were applied (straddle rule), so it needs two
# lines: 2 x cadence / 30 s, plus the settings read latency (~1 s) and one line of slack for the toggle's
# shader rebuilds.
SCENE_WAIT_MIN_FPS = 30.0
SCENE_WAIT_SETTLE_S = 1.0


def _apply(state, set_string):
    for kv in str(set_string or "").split(","):
        if "=" in kv:
            k, v = kv.split("=", 1)
            state[k.strip()] = v.strip()


def phase_states(job, plan):
    """Effective settings of every plan phase: the job's set, then the phases' sets in order (cumulative)."""
    state = {}
    _apply(state, job.get("set", ""))
    out = []
    for step in plan:
        _apply(state, step.get("set", ""))
        out.append(dict(state))
    return out


def _on(state, key, default="1"):
    return str(state.get(key, default)) not in ("0", "")


def scene_wait_windows(job, plan, *, include_traces=False):
    """Cost phases with RT scene waits; capacity checks also include captures."""
    states = phase_states(job, plan)
    return [str(p.get("label", "")) for p, st in zip(plan, states)
            if str(p.get("label", "")).startswith("cost-") and (include_traces or not p.get("trace_at"))
            and _on(st, "rt") and _on(st, "rt_scene_wait")]


def _scene_wait_entry(entry):
    try:
        sel, match = re.compile(entry["select"]), re.compile(entry["match"])
    except (KeyError, re.error, TypeError):
        return False
    return (not entry.get("event") and bool(sel.search(SCENE_WAIT_GOOD)) and bool(sel.search(SCENE_WAIT_BAD))
            and bool(match.search(SCENE_WAIT_GOOD)) and not match.search(SCENE_WAIT_BAD))


def scene_wait_problems(job, plan):
    """Problems (strings) of a lean job without a correct scene-wait gate; [] when fine or not a lean job."""
    need = scene_wait_windows(job, plan)
    if not need:
        return []
    out = []
    entries = [e for e in job.get("expect_every", []) if isinstance(e, dict) and _scene_wait_entry(e)]
    if not entries:
        out.append("scene wait: no expect_every entry gates '[rt] scene wait' lines with 0 timeouts "
                   f"(add queue_gates.SCENE_WAIT_GATE with windows {need})")
    else:
        covered = set()
        for e in entries:
            covered |= set(e.get("windows") or need)   # no windows: every line of the run (covers all)
        missing = [w for w in need if w not in covered]
        if missing:
            out.append(f"scene wait: the gate does not cover the cost window(s) {', '.join(missing)}")
    states = {str(p.get("label", "")): st for p, st in zip(plan, phase_states(job, plan))}
    seconds = {str(p.get("label", "")): float(p.get("seconds", 0)) for p in plan}
    for w in need:
        frames = int(states[w].get("rt_stats_period_frames", 0)) or int(states[w].get("rt_scene_wait_log_frames", 600))
        step = next(p for p in plan if str(p.get("label", "")) == w)
        if step.get("frames"):
            if not step.get("stats_reset") or int(step["frames"])-int(step.get("settle_frames", 0)) < frames:
                out.append(f"scene wait: cost window {w} needs a reset and one complete {frames}-frame period")
            continue
        want = 2.0 * frames / SCENE_WAIT_MIN_FPS + SCENE_WAIT_SETTLE_S
        if seconds[w] < want:
            out.append(f"scene wait: cost window {w} ({seconds[w]:.0f} s) cannot hold two {frames}-frame lines at "
                       f"{SCENE_WAIT_MIN_FPS:.0f} fps (needs {want:.0f} s): set rt_scene_wait_log_frames=120")
            break
    exp = " ".join(job.get("expect", []))
    cadence = {int(st.get("rt_stats_period_frames", 0)) or int(st.get("rt_scene_wait_log_frames", 600)) for st in states.values()}
    stale = [c for c in re.findall(r"scene wait: (\d+) frames", exp) if int(c) not in cadence]
    if stale:
        out.append(f"scene wait: expect names '{stale[0]} frames' lines but the job logs every {sorted(cadence)} frames")
    return out


def _job_files(batches_dir):
    d = Path(batches_dir)
    return sorted((d / "jobs").glob("*.json")) if (d / "jobs").is_dir() else sorted(d.glob("*/jobs/*.json"))


def lint_scene_wait(batches_dir, problems_of=None):
    """{job file: problems} for every lean job (-MovePlan with cost-* windows) under batches_dir (or one batch).
    problems_of(job, plan): the check (default scene_wait_problems; lint_capacity passes capacity_problems)."""
    scene_wait = problems_of or scene_wait_problems
    out = {}
    for jf in _job_files(batches_dir):
        job = json.loads(jf.read_text(encoding="utf-8-sig"))
        args = job.get("args", [])
        if "-MovePlan" not in args:
            continue
        pf = jf.parent.parent / "plans" / Path(args[args.index("-MovePlan") + 1]).name
        if not pf.exists():
            continue
        problems = scene_wait(job, json.loads(pf.read_text(encoding="utf-8-sig")))
        if problems:
            out[str(jf)] = problems
    return out


# Capacity gates, next to the scene-wait gate and judged the same way (traced cost-* windows, canonical entries,
# behaviour checked on a good and a bad real line). A full RT capture buffer drops opaque draws from the RT scene;
# a full cascade buffer drops roof casters (missing shadows). Both counters are per-frame means of integer
# events, printed 0.00 / 0.0 only when no frame of the line had one, so the bound is 0. The cascade gate applies
# to the windows that set rt_cascade_casters=1 (in the job or the plan; a preset alone does not count). Lines every rt_timing_frames (default 300) frames.
# The cascade line counts the sun cascade pass's frames, so it is "when_present" (an interior has no cascade pass).
CAPACITY_GATES = [
    ({"name": "capacity: the RT capture drops no opaque draw (buffer full 0.00) in every traced cost window",
      "select": r"\[geo\] RT capture, last \d+ frames:",
      "match": r"opaque draws dropped \(buffer full\) 0\.00,", "min": 1},
     None,
     "16:19:41.796  [geo] RT capture, last 120 frames: alpha region 336 MB (peak demand 217.5 MB), opaque peak 98.4 MB of a "
     "1024 MB buffer; per frame: alpha draws made opaque (region full) 0.00, opaque draws dropped (buffer full) 0.00, indirect",
     "16:19:41.796  [geo] RT capture, last 120 frames: alpha region 336 MB (peak demand 217.5 MB), opaque peak 1024.0 MB of a "
     "1024 MB buffer; per frame: alpha draws made opaque (region full) 0.00, opaque draws dropped (buffer full) 0.02, indirect"),
    ({"name": "capacity: the cascade casters buffer is never full (buffer full 0.0) in every traced cost window with the casters on",
      "select": r"\[cascade\] casters: cascade \d+, \d+ frames;",
      "match": r", buffer full 0\.0, added ", "min": 1, "when_present": True},
     "rt_cascade_casters",
     "16:19:41.589  [cascade] casters: cascade 2, 120 frames; per frame: draws seen 128.0, skipped as duplicates 1.0, "
     "alpha-tested skipped 62.0, not sizeable 0.0, other matrix 0.0, buffer full 0.0, added 65.0; triangles 1092822",
     "16:19:41.589  [cascade] casters: cascade 2, 120 frames; per frame: draws seen 128.0, skipped as duplicates 1.0, "
     "alpha-tested skipped 62.0, not sizeable 0.0, other matrix 0.0, buffer full 0.5, added 65.0; triangles 1092822"),
]


def capacity_problems(job, plan):
    """Problems of a lean job without the capacity gates over its traced cost windows; [] when fine."""
    need = scene_wait_windows(job, plan, include_traces=True)
    if not need:
        return []
    states = {str(p.get("label", "")): st for p, st in zip(plan, phase_states(job, plan))}
    out = []
    for gate, key, good, bad in CAPACITY_GATES:
        want = [w for w in need if key is None or _on(states[w], key, "0")]
        if not want:
            continue
        covered = set()
        for e in job.get("expect_every", []):
            try:
                sel, match = re.compile(e["select"]), re.compile(e["match"])
            except (KeyError, re.error, TypeError):
                continue
            # "when_present" as the canonical entry: the cascade line is printed only where the cascade pass ran.
            if (not e.get("event") and bool(e.get("when_present")) == bool(gate.get("when_present")) and sel.search(good)
                    and sel.search(bad) and match.search(good) and not match.search(bad)):
                covered |= set(e.get("windows") or want)
        missing = [w for w in want if w not in covered]
        if missing:
            out.append(f"{gate['name'].split(':')[0]}: no gate like queue_gates.CAPACITY_GATES ('{gate['select']}') "
                       f"over {', '.join(missing)}")
    return out


def lint_batches(batches_dir):
    """{job file: problems} for every tools/live_batches/*/jobs/*.json whose -MovePlan has an energy gate."""
    out = {}
    for jf in sorted(Path(batches_dir).glob("*/jobs/*.json")):
        job = json.loads(jf.read_text(encoding="utf-8-sig"))
        args = job.get("args", [])
        if "-MovePlan" not in args:
            continue
        pf = jf.parent.parent / "plans" / Path(args[args.index("-MovePlan") + 1]).name
        if not pf.exists():
            continue
        problems = exposure_hold_problems(json.loads(pf.read_text(encoding="utf-8-sig")), job)
        if problems:
            out[str(jf)] = problems
    return out


def _read(path):
    # utf-8-sig: Windows PowerShell 5 writes JSON with a BOM.
    return Path(path).read_text(encoding="utf-8-sig", errors="replace") if path and Path(path).exists() else ""


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--lint-exposure-hold", nargs="?", const=str(Path(__file__).resolve().parent.parent / "tools" / "live_batches"),
                    default=None, metavar="DIR", help="lint every job under DIR/*/jobs: energy-gated plans must hold the exposure")
    ap.add_argument("--lint-scene-wait", nargs="?", const=str(Path(__file__).resolve().parent.parent / "tools" / "live_batches"),
                    default=None, metavar="DIR", help="lint every lean job under DIR (*/jobs or one batch's jobs): "
                    "the scene-wait gate in every cost window")
    ap.add_argument("--lint-capacity", nargs="?", const=str(Path(__file__).resolve().parent.parent / "tools" / "live_batches"),
                    default=None, metavar="DIR", help="lint every lean job under DIR: the capacity gates in every traced cost window")
    ap.add_argument("--gates", help="JSON file: the job's expect_every list")
    ap.add_argument("--log", help="the run's kept game log slice")
    ap.add_argument("--runner", default="", help="the run's runner log ([pan-plan] window lines)")
    ap.add_argument("--plan", default="", help="the job's -MovePlan JSON (phase labels)")
    a = ap.parse_args(argv)
    if a.lint_exposure_hold:
        bad = lint_batches(a.lint_exposure_hold)
        for jf, problems in bad.items():
            for p in problems:
                print(f"{jf}: {p}")
        if not bad:
            print("exposure hold lint: ok")
        return 1 if bad else 0
    if a.lint_capacity:
        bad = lint_scene_wait(a.lint_capacity, capacity_problems)
        for jf, problems in bad.items():
            for p in problems:
                print(f"{jf}: {p}")
        if not bad:
            print("capacity lint: ok")
        return 1 if bad else 0
    if a.lint_scene_wait:
        bad = lint_scene_wait(a.lint_scene_wait)
        for jf, problems in bad.items():
            for p in problems:
                print(f"{jf}: {p}")
        if not bad:
            print("scene wait lint: ok")
        return 1 if bad else 0
    if not a.gates or not a.log:
        ap.error("--gates and --log are required (or --lint-exposure-hold / --lint-scene-wait)")
    gates = json.loads(_read(a.gates) or "[]")
    if isinstance(gates, dict):
        gates = [gates]
    plan = json.loads(_read(a.plan) or "[]") if a.plan else []
    if not Path(a.log).exists():
        print(f"every-window gates: no game log {a.log}")
        return 1
    failures = check(gates, _read(a.log), _read(a.runner), plan)
    for f in failures:
        print(f)
    if not failures:
        print(f"every-window gates: ok ({len(gates)})")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
