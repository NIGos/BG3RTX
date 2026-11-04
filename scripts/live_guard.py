"""Early rejection using the same window semantics as the final queue gates.

Read-only CLI used by auto-run; also exercised with synthetic logs. Never launches
anything or writes a queue. Each poll opens and closes the game log.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from scripts import queue_gates as gates
from tools.live_batches import live_flow as flow


def producer_period(pattern, state, fps, job):
    # Explicit gate cadence is authoritative for producers whose signatures are
    # new (for example hair primary). Do not silently drop their startup check.
    related = next((g for g in job.get("expect_every", [])
                    if not g.get("event") and (g.get("match") == pattern or g.get("select") == pattern)), None)
    return flow.period(related or dict(select=pattern), state, fps)


def evaluate(job, plan, game, runner, phase, now, failed=()):
    """Return the first irreversible failure, with a slice-local recovery target."""
    if job.get("fail_fast", True) is False:
        return None
    lines = gates.stamped_lines(game)
    wins = gates.windows(runner, plan, lines)
    readiness = {}
    clock = gates._Clock(lines[0][0] if lines else None)
    for line in runner.splitlines():
        timestamp = clock(gates._sec(line))
        match = re.search(r"\[motion-ready\] phase=(\d+)", line)
        if match:
            readiness[int(match[1])] = timestamp
    origin = lines[0][0] if lines else now
    now += round((origin - now) / 86400) * 86400
    if wins and now < wins[-1]["start"] - 43200:
        now += 86400
    slices = job.get("fail_fast_slices", [])
    current = next((s for s in slices if s["offset"] <= phase < s["offset"] + s["count"]), None)
    if slices and current is None:
        current = next((s for s in slices if s["offset"] > phase and s["offset"] not in failed), None)

    def failure(check, failed_phase=phase, segment=current, global_failure=False):
        offset = segment["offset"] if segment and not global_failure else -1
        end = offset + segment["count"] if offset >= 0 else len(plan)
        # combine inserts the next settings-reset immediately after this slice.
        return dict(check=check, phase=failed_phase, slice=offset,
                    next_phase=max(end, phase) if end < len(plan) and offset >= 0 else -1)

    # Device loss and crashes poison the entire launch, including during load.
    if re.search(r"\[fault\] VK_ERROR_DEVICE_LOST from\b|\[aftermath\] device lost: waiting\b|\[crash\] exception 0x[0-9a-f]+", game, re.I):
        return failure("global game failure: crash or device lost", global_failure=True)
    if current and current.get("fail_fast", True) is False:
        return None
    start = next((w["start"] for w in wins if current and w["index"] >= current["offset"]), None)
    # Extraction includes the shared launch preamble in every slice. A failure
    # there invalidates each affected slice immediately, including before load.
    preamble_end = wins[0]["start"] if wins else float("inf")
    local = game if not slices else "\n".join(l for t, l in lines if t < preamble_end or start is not None and t >= start)
    for pattern in (current or job).get("forbid", []):
        if re.search(pattern, local, re.I | re.M):
            return failure("forbidden log pattern: " + pattern)

    for gate in job.get("expect_every", []):
        if not gate.get("windows") and not gate.get("trend"):
            selected = [line for _, line in lines if re.search(gate["select"], line)]
            if any(not re.search(gate.get("match", ""), line) for line in selected):
                return failure("every-window " + gate.get("name", gate["select"]) + ": nonmatching line")

    margin = float(job.get("live_flow", {}).get("settling_margin_s", 1))
    # Only closed windows are final. Reuse final gate checking, including cfg,
    # skipped straddling samples, event settling, min=0 and when_present.
    for w in wins:
        if not w.get("ready", True) and not math.isfinite(w["end"]):
            continue  # readiness timeout belongs to the runner, not an early sample
        segment = next((s for s in slices if s["offset"] <= w["index"] < s["offset"] + s["count"]), None)
        if segment and (segment["offset"] in failed or segment.get("fail_fast", True) is False):
            continue
        for gate in job.get("expect_every", []):
            if gate.get("trend") or w["label"] not in gate.get("windows", []):
                continue
            one = dict(gate, windows=[w["label"]])
            problems = gates.check([one], game, runner, plan)
            # A fully attributed bad sample cannot be repaired by future lines.
            # Missing required samples become final only after the endpoint.
            irreversible = [p for p in problems if "line(s) fail /" in p or p.startswith("plan drift:")]
            if irreversible:
                return failure(irreversible[0], w["index"], segment)
            if not math.isfinite(w["end"]) or now < w["end"] + margin:
                continue
            if problems:
                return failure(problems[0], w["index"], segment)

    # Periodic *expect* patterns must actually succeed. A generic producer line
    # containing attempts=0 cannot satisfy an expectation requiring useful draws.
    source = current or job
    fps = 30 if str(job.get("window_mode", "")).startswith("visible_unfocused") else float(job.get("live_flow", {}).get("expected_fps", 30))
    states = flow.phase_states(job, plan)
    first = current["offset"] if current else 0
    if 0 <= phase < len(plan) and re.search(r"(?:^|--)producer-sanity-", plan[phase].get("label", "")):
        window = next((w for w in wins if w["index"] == phase), None)
        requested = plan[phase].get("wait_log", [])
        requested = [requested] if isinstance(requested, str) else requested
        for pattern in requested:
            dt = producer_period(pattern, states[phase], flow.fps_windows.expected(job, states[phase])["fps"], job)
            if window and dt is not None and not re.search(pattern, local, re.I | re.M):
                deadline = window["settings_applied"] + dt + margin + float(source.get("live_flow", {}).get("settings_latency_s", 1))
                if now >= deadline:
                    return failure("producer silent: " + pattern)
    for pattern in source.get("expect", []):
        requirements = flow.producer_requirements(pattern)
        related = [g for g in job.get("expect_every", []) if g.get("windows") and not g.get("event")
                   and pattern.replace("\\", "").startswith(g["select"].replace("\\", ""))]
        # Gate windows identify when an ON-only producer is actually requested.
        # Do not demand its full expect line while warming its OFF value.
        producer_windows = {label for g in related for label in g["windows"]}
        # Only judged windows: warm-ups may legitimately show a producer before its values settle
        # (DLSS-G reports 1.00 until frame generation has run a full window).
        judged = flow.judged_labels(job, plan)
        eligible = [w for w in wins if w["index"] >= first and 0 <= w["index"] < len(states)
                    and (not judged or plan[w["index"]].get("label") in judged)
                    and (not producer_windows or w["label"] in producer_windows)
                    and (not plan[w["index"]].get("wait_log") or w["index"] in readiness)
                    and all(states[w["index"]].get(k) == v for k, v in requirements.items())]
        if not eligible or re.search(pattern, local, re.I | re.M):
            continue
        state = states[eligible[0]["index"]]
        dt = producer_period(pattern, state, flow.fps_windows.expected(job, state)["fps"], job)
        if dt is None:
            continue  # events and startup announcements have no periodic promise
        # The first complete period plus settings latency and settling margin.
        enabled_at = max(eligible[0]["applied"], readiness.get(eligible[0]["index"], eligible[0]["applied"]))
        deadline = enabled_at + dt + margin + float(source.get("live_flow", {}).get("settings_latency_s", 1))
        if now >= deadline:
            return failure("producer silent: " + pattern)
    return None


def evaluate_session(job, game, elapsed):
    """The session queue owns its settings boundary and passes only its log slice."""
    immediate = evaluate(job, [], game, "", -1, elapsed)
    if immediate or job.get("fail_fast", True) is False:
        return immediate
    state = {}
    gates._apply(state, job.get("set", ""))
    fps = 30 if str(job.get("window_mode", "")).startswith("visible_unfocused") else float(job.get("live_flow", {}).get("expected_fps", 30))
    margin = float(job.get("live_flow", {}).get("settling_margin_s", 1))
    latency = float(job.get("live_flow", {}).get("settings_latency_s", 1))
    for pattern in job.get("expect", []):
        dt = producer_period(pattern, state, flow.fps_windows.expected(job, state)["fps"], job)
        if dt is None or re.search(pattern, game, re.I | re.M):
            continue
        if not all(state.get(k) == v for k, v in flow.producer_requirements(pattern).items()):
            continue
        if elapsed >= dt + margin + latency:
            return dict(check="producer silent: " + pattern, phase=0, slice=-1, next_phase=-1)
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--context", required=True)
    ap.add_argument("--plan", default="")
    ap.add_argument("--log", required=True)
    ap.add_argument("--runner", required=True)
    ap.add_argument("--phase", type=int, default=-1)
    ap.add_argument("--now", type=float, default=0)
    ap.add_argument("--session-elapsed", type=float)
    ap.add_argument("--from", dest="offset", type=int, default=0)
    ap.add_argument("--failed", default="")
    a = ap.parse_args()
    read = lambda p: Path(p).read_text(encoding="utf-8-sig", errors="replace") if Path(p).is_file() else ""
    job = json.loads(read(a.context))["job"]
    if a.offset and Path(a.log).exists():
        with Path(a.log).open("rb") as stream:
            stream.seek(a.offset)
            game = stream.read().decode("utf-8-sig", errors="replace")
    else:
        game = read(a.log)
    result = (evaluate_session(job, game, a.session_elapsed) if a.session_elapsed is not None else
              evaluate(job, json.loads(read(a.plan)) if a.plan else [], game, read(a.runner), a.phase, a.now,
                       [int(s) for s in a.failed.split(",") if s]))
    print(json.dumps(result))
    return 0


if __name__ == "__main__":
    sys.exit(main())
