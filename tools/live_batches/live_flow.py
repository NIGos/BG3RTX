"""Shared offline live-job design checks. Never queues jobs or starts the game.

Cadences below come from the producers, not from a fast successful run. Override an
unknown producer with expect_every.period_frames / period_s (document the source).
live_flow contains expected_fps (required in foreground), settings_latency_s (1),
trace_stall_s (1 per trace), and startup_only (keys which cannot change after warmup).
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import re
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from scripts.queue_gates import phase_states


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def write(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")


def job_files(directory):
    directory = Path(directory)
    if directory.is_file():
        return [directory]
    return sorted(directory.glob("jobs/*.json")) + sorted(directory.glob("*/jobs/*.json"))


def load_job(path):
    path = Path(path)
    job = read(path)
    args = job.get("args", [])
    if "-MovePlan" not in args:
        raise ValueError("no -MovePlan")
    plan_arg = Path(args[args.index("-MovePlan") + 1])
    candidates = [path.parent.parent / "plans" / plan_arg.name, ROOT / plan_arg, plan_arg]
    plan_path = next((p for p in candidates if p.is_file()), None)
    if plan_path is None:
        raise ValueError(f"missing plan {plan_arg}")
    return job, read(plan_path), plan_path


def default_results():
    local = ROOT / "captures/queue/results.jsonl"
    if local.exists():
        return local
    # A linked worktree has read-only access to the main checkout's evidence.
    git = ROOT / ".git"
    if git.is_file():
        meta = Path(git.read_text().strip().removeprefix("gitdir: "))
        candidate = meta.parents[2] / "captures/queue/results.jsonl"
        if candidate.exists():
            return candidate
    return local


def signature(path):
    st = Path(path).stat()
    return [str(Path(path).resolve()), st.st_size, st.st_mtime_ns]


def corpus(results, cache, newest=3, inventory=None):
    """Cached newest N readable VALID logs per save. Unknown saves are never guessed.

    Old result rows lack save; resolve their exact job name through checked-in jobs
    and archived done jobs. Signatures include selected logs and the results file.
    The cache is only an index, never modifies the evidence or keeps handles open.
    """
    results, cache = Path(results), Path(cache)
    inventory = inventory or ROOT / "tools/live_batches"
    sources = job_files(inventory)
    if results.exists():
        sources += sorted((results.parent / "done").glob("*.json"))
    key = [2, newest, signature(results) if results.exists() else None,
           [signature(p) for p in sources]]
    try:
        old = read(cache)
        if old["key"] == key and all(signature(p) == sig for p, sig in old["logs"]):
            return old
    except (OSError, ValueError, KeyError, TypeError):
        pass
    saves = {}
    for p in sources:
        try:
            j = read(p)
            if j.get("save"):
                saves[j.get("name", p.stem)] = j["save"]
        except (OSError, ValueError):
            continue
    rows, warnings = [], []
    if results.exists():
        with results.open(encoding="utf-8-sig", errors="replace") as stream:
            for i, line in enumerate(stream):
                try:
                    row = json.loads(line)
                    if row.get("valid") is True:
                        rows.append((row.get("start", ""), i, row))
                except (ValueError, AttributeError):
                    warnings.append(f"unreadable result row {i + 1}")
    counts, logs, entries, seen = {}, [], [], set()
    skipped = 0
    tick = time.monotonic()
    for _, _, row in sorted(rows, key=lambda r: (r[0], r[1]), reverse=True):
        save = row.get("save") or saves.get(row.get("job"))
        if not save:
            skipped += 1
            continue
        if counts.get(save, 0) >= newest or not row.get("log"):
            continue
        p = Path(row["log"])
        if not p.is_absolute():
            p = results.parent.parent.parent / p
        if not p.is_file() or str(p.resolve()) in seen:
            continue
        seen.add(str(p.resolve()))
        counts[save] = counts.get(save, 0) + 1
        logs.append([str(p), signature(p)])
        entries.append(dict(save=save, path=str(p), lines=p.read_text(encoding="utf-8-sig", errors="replace").splitlines()))
        if time.monotonic() - tick >= 25:
            print(f"corpus: {len(entries)} logs indexed, {len(counts)} saves", flush=True)
            tick = time.monotonic()
    if skipped:
        warnings.append(f"{skipped} VALID rows have no resolvable save")
    out = dict(key=key, logs=logs, entries=entries, saves=counts, warnings=warnings)
    write(cache, out)
    return out


def regex_problems(job, evidence, memo=None):
    errors, coverage = [], {}
    memo = {} if memo is None else memo
    for kind in ("forbid", "expect"):
        for pattern in job.get(kind, []):
            try:
                rx = re.compile(pattern, re.IGNORECASE)  # Select-String default in test-queue.ps1
            except (re.error, TypeError) as e:
                errors.append(f"{kind}: invalid regex {pattern!r}: {e}")
                continue
            if pattern not in memo:
                count, first = 0, None
                for entry in evidence:
                    for line in entry["lines"]:
                        if rx.search(line):
                            count += 1
                            if first is None:
                                first = (entry["path"], line)
                memo[pattern] = count, first
            count, first = memo[pattern]
            coverage[f"{kind}: {pattern}"] = count
            if kind == "forbid" and first:
                p, line = first
                errors.append(f"forbid /{pattern}/ matches VALID log {p}: {line!r}")
    # A new effect need not appear in historical logs. Missing expect matches are
    # recorded as coverage, never silently rewritten or mistaken for a failed run.
    return errors, coverage


def period(gate, state, fps):
    if "period_s" in gate:
        return float(gate["period_s"])
    if "period_frames" in gate:
        return float(gate["period_frames"]) / fps
    sel = gate["select"].replace("\\", "")
    if "mem] device-local VRAM" in sel:
        return 600 / fps  # layer/layer.cpp, immediately after [pace]
    if "rrpost] gpu" in sel or "ngx] gpu RR" in sel or "watch]" in sel:
        return 5.0  # rrpost::timing_log, ngx::timing_collect, layer's watch timer
    if "scene wait:" in sel:
        return float(state.get("rt_scene_wait_log_frames", 600)) / fps
    if "DLSS-G frame" in sel:
        return float(state.get("fg_state_every", 60) if state.get("fg", "2") != "0" else 600) / fps
    if "occ stats" in sel:
        return float(state.get("rt_occ_stats_frames", 60)) / fps
    if any(s in sel for s in ("GPU cost", "hair groom]", "rf hit shade terms", "rf hit gi", "cpu]")):
        return 300 / fps
    if "pace]" in sel:
        return 600 / fps
    if any(s in sel for s in ("hit material instanced clip", "hit material (", "omm check", "ls box", "hit emission", "gi: trace pass")):
        return 120 / fps  # core/gi.cpp stats_collect
    if any(s in sel for s in ("scene build GPU", "geo] RT capture", "cascade] casters", "water]", "bvh]", "alpha hair]", "alpha scalp]", "hit material", "omm] last", "vt alpha]", "omm dyn]", "mem]")):
        return max(30, float(state.get("rt_timing_frames", 300))) / fps
    if "gi: background builds (" in sel:
        return max(1, float(state.get("rt_timing_frames", 120))) / fps
    return None


def judged_labels(job, plan):
    labels = {w for g in job.get("expect_every", []) for w in g.get("windows", [])}
    labels.update(p["label"] for p in plan if re.match(r"^(cost|shot|timing|energy|rendered|presented|diag|turn)-", p.get("label", "")))
    return labels


def window_problems(job, plan):
    meta = job.get("live_flow", {})
    labels = [p.get("label") for p in plan]
    if len(set(labels)) != len(labels):
        return ["windows: duplicate phase labels make gate attribution ambiguous"], []
    fps = 30.0 if str(job.get("window_mode", "")).startswith("visible_unfocused") else meta.get("expected_fps")
    if fps is None or float(fps) <= 0:
        return ["windows: declare live_flow.expected_fps for a foreground job"], []
    fps = float(fps)
    latency = float(meta.get("settings_latency_s", 1))
    trace_stall = float(meta.get("trace_stall_s", 1))
    if not all(math.isfinite(v) and v >= 0 for v in (latency, trace_stall)):
        return ["windows: settings latency and trace stall budgets must be finite and nonnegative"], []
    errors, budgets = [], []
    phases = {p.get("label"): (p, s) for p, s in zip(plan, phase_states(job, plan))}
    for g in job.get("expect_every", []):
        if not g.get("windows"):
            continue  # whole-run gates have no every-window promise
        for label in g["windows"]:
            if label not in phases:
                errors.append(f"windows: unknown gate window {label!r}")
                continue
            p, state = phases[label]
            need = int(g.get("min", 1))
            if need == 0:
                continue
            dt = period(g, state, fps)
            name = g.get("name", g["select"])
            if dt is None:
                if not g.get("event"):
                    errors.append(f"windows: {label}: {name}: unknown cadence; declare period_frames or period_s")
                continue
            if dt <= 0 or not math.isfinite(dt):
                errors.append(f"windows: {label}: {name}: invalid cadence {dt}")
                continue
            if p.get("trace_at") and "trace_stall_s" not in meta:
                errors.append(f"windows: {label}: {name}: declare live_flow.trace_stall_s, the upper stall budget per trace")
            # Endpoint is exclusive. Worst alignment puts the first line one full
            # period after apply; one further line is consumed by the straddle rule.
            skip = 0 if g.get("event") else 1
            overhead = latency + float(g.get("settle_s", 0)) + len(p.get("trace_at", [])) * trace_stall
            available = float(p.get("seconds", 0)) - overhead
            complete = max(0, math.ceil(available / dt - 1e-9) - 1 - skip)
            minimum = overhead + (need + skip) * dt
            budgets.append(dict(window=label, gate=name, complete=complete, required=need,
                                period_s=dt, minimum_seconds_exclusive=minimum))
            if complete < need:
                errors.append(f"windows: {label}: {name}: worst-case {complete} complete lines, need {need}; "
                              f"{dt:g} s/line at {fps:g} fps; minimum > {minimum:g} s (settings/trace stalls and straddle included)")
    return errors, budgets


# These schedule observations/captures and diagnostics; they do not select shading
# pipeline variants. All rendering keys (including fg/rr) still require warming.
CONTROL = {"qa_exposure_hold", "fg_snap", "pan_capture", "pan_prepare", "pan_post", "pan_edge", "pan_water",
           "rt_stats", "rt_primary_check", "rt_primary_identity", "rt_primary_epoch", "ls_stats", "ls_check",
           "rf_stats", "rt_debug", "ls_debug", "gi_debug", "rf_debug", "rt_occ_stats", "rr_show"}


def warmup_problems(job, plan):
    if job.get("live_flow", {}).get("segments"):
        problems = []
        for seg in job["live_flow"]["segments"]:
            subset = dict(job, set=seg["set"], live_flow=seg.get("live_flow", {}))
            problems += warmup_problems(subset, plan[seg["offset"]:seg["offset"] + seg["count"]])
        return problems
    labels = judged_labels(job, plan)
    first = next((i for i, p in enumerate(plan) if p.get("label") in labels), len(plan))
    if first == len(plan):
        return []
    states = phase_states(job, plan)
    keys = set().union(*(s.keys() for s in states))
    startup = set(job.get("live_flow", {}).get("startup_only", []))
    out = []
    for key in sorted(keys - CONTROL):
        values = {s.get(key) for p, s in zip(plan[first:], states[first:])
                  if p.get("label") in labels and s.get(key) is not None}
        if len(values) < 2:
            continue
        if key in startup:
            out.append(f"warmup: startup-only key {key} changes after the first measured window")
            continue
        warmed = {s.get(key) for p, s in zip(plan[:first], states[:first])
                  if float(p.get("seconds", 0)) > 0 and not p.get("trace_at") and not p.get("serial")}
        missing = sorted(values - warmed)
        if missing:
            out.append(f"warmup: {key} values {', '.join(missing)} not exercised before {plan[first]['label']}")
    return out


def repair_known_forbids(job, plan):
    """Narrow known false positives without dropping the measured-window gate."""
    scoped = []
    fixed = []
    for rx in job.get("forbid", []):
        if "mesh" in rx and "pool full" in rx:
            scoped.append(r"\[(?:rt|bvh)\] mesh.*pool full")
        elif rx == "nonfinite":
            fixed.append(r"\bnon-?finite(?: terms)?\s*[:=]?\s+[1-9]\d*\b")
        elif rx == r"\[crash\] exception":
            fixed.append(r"\[crash\] exception 0x[0-9a-f]+")
        else:
            fixed.append(rx)
    job["forbid"] = list(dict.fromkeys(fixed))
    if scoped:
        entry = dict(name="no mesh pool exhaustion in measured windows (load excluded)",
                     select=scoped[0], match=r"(?!)", event=True, min=0,
                     windows=[p["label"] for p in plan if p.get("label") in judged_labels(job, plan)])
        if entry not in job.setdefault("expect_every", []):
            job["expect_every"].append(entry)
    return job


def lint(job, plan, evidence, memo=None):
    regex, coverage = regex_problems(job, evidence, memo)
    windows, budgets = window_problems(job, plan)
    return dict(problems=regex + windows + warmup_problems(job, plan), regex_coverage=coverage, windows=budgets)


def markdown_report(report):
    jobs = report["jobs"]
    failed = sum(bool(j["problems"]) for j in jobs.values())
    lines = ["# Live-flow design audit", "", f"{len(jobs)} jobs checked; {failed} fail, {len(jobs) - failed} pass.",
             f"Corpus: {len(report['corpus']['logs'])} recent VALID logs across {len(report['corpus']['saves'])} saves (newest 3 readable logs per save).",
             "Structural checks only; no game launch and no rendering qualification.",
             "Missing historical expect matches are coverage information, since new features have no old evidence.",
             "Window bounds are exclusive and include the first skipped stats line, settings latency and declared trace stalls.",
             "Foreground jobs without an expected FPS fail rather than borrowing a faster run's cadence.", "",
             "Regenerate: `python tools/live_batches/live_flow.py --report tools/live_batches/live_flow_audit.md`.", "",
             "| Job | Result | Problems |", "| --- | --- | --- |"]
    for path, result in jobs.items():
        try:
            path = Path(path).relative_to(ROOT / "tools/live_batches").as_posix()
        except ValueError:
            pass
        reasons = []
        for p in result["problems"]:
            if p.startswith("forbid /"):
                p = p.split(" matches VALID log", 1)[0] + " matches a VALID log"
            elif "worst-case" in p:
                label = p.split(": ", 2)[1]
                p = f"window {label}: " + p.split("minimum > ", 1)[-1]
                p = p.split(" (settings/trace", 1)[0]
                p = p.replace(": ", ": > ", 1)
            if p not in reasons:
                reasons.append(p)
        cell = "<br>".join(reasons).replace("|", "&#124;") or "ok"
        lines.append(f"| {path} | {'FAIL' if reasons else 'PASS'} | {cell} |")
    lines += ["", "Corpus notes: " + "; ".join(report["corpus"].get("warnings", [])), "",
              "Known repairs applied: broad crash/nonfinite patterns narrowed; mesh-pool rejection scoped to measured windows;",
              "post_temporal warms both values and uses 12-second timing windows (minimum > 11 seconds).",
              "Other failures remain explicit; do not queue these jobs until their owners resolve the reported assumptions.", ""]
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("target", nargs="?", default=str(ROOT / "tools/live_batches"))
    ap.add_argument("--results", default=str(default_results()))
    ap.add_argument("--cache", default=str(ROOT / "captures/.cache/live_flow_corpus.json"))
    ap.add_argument("--newest", type=int, default=3)
    ap.add_argument("--report")
    a = ap.parse_args(argv)
    if a.newest < 1:
        ap.error("--newest must be positive")
    index = corpus(a.results, a.cache, a.newest)
    print(f"VALID corpus: {len(index['entries'])} logs, {len(index['saves'])} saves; {a.results}", flush=True)
    report = dict(corpus={k: v for k, v in index.items() if k not in ("entries", "key", "matches")}, jobs={})
    failures = 0
    memo = index.get("matches", {})
    for path in job_files(a.target):
        try:
            job, plan, _ = load_job(path)
            result = lint(job, plan, index["entries"], memo)
            if not index["entries"]:
                result["problems"].append("corpus: no readable VALID logs; regex validation has no evidence")
        except (OSError, ValueError, KeyError, TypeError) as e:
            result = dict(problems=[f"job/plan: {e}"])
        report["jobs"][str(path)] = result
        if result["problems"]:
            failures += 1
            for problem in result["problems"]:
                print(f"{path}: {problem}", flush=True)
    if not report["jobs"]:
        print("no jobs found", flush=True)
        return 1
    if a.report:
        if Path(a.report).suffix == ".md":
            Path(a.report).write_text(markdown_report(report), encoding="utf-8")
        else:
            write(a.report, report)
    index["matches"] = memo
    write(a.cache, index)
    print(f"live-flow: {len(report['jobs'])} jobs, {failures} failing", flush=True)
    return int(bool(failures))


if __name__ == "__main__":
    sys.exit(main())
