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
straddles two windows and is skipped (not passed). This rule is derived from the log cadence,
not tuned.

    py -3 scripts/queue_gates.py --gates gates.json --log run.log [--runner runner.txt --plan plan.json]

Prints one line per failure and exits 1 if any gate fails; exit 0 and "every-window gates: ok".
"""
import argparse
import json
import re
import sys
from pathlib import Path

_TS = re.compile(r"(\d\d):(\d\d):(\d\d)\.(\d{3})")
_WINDOW = re.compile(r"(\d\d:\d\d:\d\d\.\d{3}) \[pan-plan\] window (\d+)\b")
_COMPLETE = re.compile(r"(\d\d:\d\d:\d\d\.\d{3}) \[motion-plan\] complete")
_CFG = re.compile(r"\[cfg\] settings read")


def _sec(stamp):
    m = _TS.search(stamp)
    if not m:
        return None
    h, mi, s, ms = (int(x) for x in m.groups())
    return h * 3600 + mi * 60 + s + ms / 1000.0


class _Clock:
    """Turns HH:MM:SS.fff into monotonic seconds across a midnight rollover."""

    def __init__(self):
        self.last = None
        self.offset = 0.0

    def __call__(self, t):
        if t is None:
            return None
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
    """[{label, index, start, applied, end}] from the runner's [pan-plan] lines."""
    clock = _Clock()
    starts, complete = [], None
    for line in runner_text.splitlines():
        m = _WINDOW.search(line)
        if m:
            starts.append((clock(_sec(m.group(1))), int(m.group(2))))
            continue
        m = _COMPLETE.search(line)
        if m:
            complete = clock(_sec(m.group(1)))
    out = []
    for k, (start, index) in enumerate(starts):
        end = starts[k + 1][0] if k + 1 < len(starts) else (complete if complete is not None else float("inf"))
        label = plan[index].get("label", str(index)) if 0 <= index < len(plan) else str(index)
        applied = next((t for t, l in game_lines if start <= t < end and _CFG.search(l)), start)
        out.append({"label": label, "index": index, "start": start, "applied": applied, "end": end})
    return out


def check(gates, game_text, runner_text="", plan=None):
    """Failure strings for the expect_every gates (empty list: every gate holds)."""
    plan = plan or []
    lines = stamped_lines(game_text)
    wins = None
    failures = []
    for g in gates:
        sel = re.compile(g["select"])
        match = re.compile(g["match"])
        need = int(g.get("min", 1))
        picked = [(t, l) for t, l in lines if sel.search(l)]
        name = g.get("name") or g["select"]
        labels = g.get("windows")
        if not labels:
            bad = [l for _, l in picked if not match.search(l)]
            if len(picked) < need:
                failures.append(f"every-window {name}: {len(picked)} line(s) in the run, need {need}")
            if bad:
                failures.append(f"every-window {name}: {len(bad)} of {len(picked)} line(s) fail /{g['match']}/; first: {bad[0][:200]}")
            continue
        if wins is None:
            wins = windows(runner_text, plan, lines)
        for label in labels:
            hits = [w for w in wins if w["label"] == label]
            if not hits:
                failures.append(f"every-window {name}: window '{label}' not found in the runner log")
                continue
            for w in hits:
                judged = []
                prev = None
                for t, l in picked:
                    if t >= w["end"]:
                        break
                    if prev is not None and prev >= w["applied"]:
                        judged.append(l)
                    prev = t
                bad = [l for l in judged if not match.search(l)]
                where = f"window '{label}' (phase {w['index']})"
                if len(judged) < need:
                    failures.append(f"every-window {name}: {where} has {len(judged)} complete line(s), need {need}")
                if bad:
                    failures.append(f"every-window {name}: {where}: {len(bad)} of {len(judged)} line(s) fail /{g['match']}/; first: {bad[0][:200]}")
    return failures


def _read(path):
    return Path(path).read_text(encoding="utf-8", errors="replace") if path and Path(path).exists() else ""


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--gates", required=True, help="JSON file: the job's expect_every list")
    ap.add_argument("--log", required=True, help="the run's kept game log slice")
    ap.add_argument("--runner", default="", help="the run's runner log ([pan-plan] window lines)")
    ap.add_argument("--plan", default="", help="the job's -MovePlan JSON (phase labels)")
    a = ap.parse_args(argv)
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
