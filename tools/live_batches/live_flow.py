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
from scripts.queue_gates import phase_states, _apply, scene_wait_eligible
from tools.live_batches import fps_windows, readiness_budget, pipeline_readiness

# Recent VALID logs, scene-ready -> both a zero-timeout scene interval and
# a watch interval with zero pipeline builds. See LIVE_FLOW.md for timestamps.
# ceil(max observed seconds) + the one-second settings/observation alignment.
SCENE_SETTLE_SECONDS = {"Water Queens House": 17, "Test Dialogue3": 19,
                        "Test Dialogue1": 16, "Campsite - 27h": 29}
TRACE_EXPORT = {}
TRACE_STALL = {}


def configure_trace_exports(entries):
    """VALID nearest-rank p95 export latency and capture-frame stalls.

    Match each request only before the next request; never borrow a later export
    to hide missing evidence. Clocks use the same midnight handling as gates.
    """
    from scripts.queue_gates import stamped_lines
    samples = []
    stalls = []
    stall_logs = set()
    for entry in entries:
        requests = [t for t, line in stamped_lines(entry.get("runner", "")) if "[motion-trace]" in line]
        lines = stamped_lines("\n".join(entry["lines"]))
        writes = [t for t, line in lines
                  if re.search(r"\[trace\] frame \d+ written:", line)]
        dumps = [(t, int(m[1])/1000) for t, line in lines
                 if (m := re.search(r"\[dump\].*decoded in (\d+) ms", line))]
        pace = [(t, float(m[1])/1000) for t, line in lines
                if (m := re.search(r"\[pace\].*max ([\d.]+) ms", line))]
        for i, start in enumerate(requests):
            end = requests[i+1] if i+1 < len(requests) else math.inf
            finish = next((t for t in writes if start <= t < end), None)
            if finish is not None:
                samples.append(finish-start)
            # The next pace report contains the capture frame. Charge its full
            # maximum, rather than adding decode again (already in that frame).
            decoded = [(t, seconds) for t, seconds in dumps if start <= t < end]
            if decoded:
                t, seconds = decoded[0]
                frame = next((maximum for stamp, maximum in pace if stamp >= t), 0)
                stalls.append((max(seconds, frame), Path(entry['path']).name))
                stall_logs.add(Path(entry['path']).name)
    TRACE_EXPORT.clear()
    if samples:
        TRACE_EXPORT.update(seconds=sorted(samples)[math.ceil(.95*len(samples))-1],
                            quantile=.95, samples=len(samples), maximum=max(samples))
    TRACE_STALL.clear()
    if stalls:
        seconds, source = sorted(stalls)[math.ceil(.95*len(stalls))-1]
        TRACE_STALL.update(seconds=seconds, source=source,
                           quantile=.95, samples=len(stalls), maximum=max(stalls)[0],
                           logs=len(stall_logs))


def capture_stall(meta):
    """Declared floor plus the VALID capture-frame p95, also in fresh processes."""
    declared = float(meta.get('trace_stall_s', 1))
    if not math.isfinite(declared) or declared < 0:
        return declared  # retain validation errors even with measured evidence
    return max(declared,
               float(meta.get('trace_capture_stall', {}).get('seconds', 0)),
               float(TRACE_STALL.get('seconds', 0)))


def trace_schedules(job, plan):
    """Carry the previous export across phase boundaries, including ungated traces."""
    meta = job.get("live_flow", {})
    segments = {i: seg.get("live_flow", {}) for seg in meta.get("segments", [])
                for i in range(seg["offset"], seg["offset"]+seg["count"])}
    origin = finish = 0
    out = {}
    for i, (phase, state) in enumerate(zip(plan, phase_states(job, plan))):
        local = segments.get(i, meta)
        export = max(float(local.get("trace_export", {}).get("seconds", 0)), TRACE_EXPORT.get("seconds", 0))
        dispatch = float(local.get("trace_dispatch_s", 0))
        fps = fps_windows.expected(job, state)["fps"]
        latency = float(local.get("settings_latency_s", 1))
        service = max(2/fps + capture_stall(local), export)
        times = []
        for at in phase.get("trace_at", []):
            finish = max(origin+max(float(at), latency), finish) + dispatch + service
            times.append(finish-origin)
        out[phase.get("label")] = times
        origin += float(phase.get("seconds", 0))
    return out


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


def active_job_files():
    """Explicit inventory; historical artifacts remain available for audit."""
    inventory = read(ROOT / "tools/live_batches/duration_lint.json")
    return {ROOT / Path(path) for path, result in inventory.items() if result.get("active", False)}


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
    key = [4, newest, signature(results) if results.exists() else None,
           [signature(p) for p in sources]]
    try:
        old = read(cache)
        if old["key"] == key and all(signature(p) == sig for p, sig in old["logs"]):
            return old
    except (OSError, ValueError, KeyError, TypeError):
        pass
    saves, jobs = {}, {}
    for p in sources:
        try:
            j = read(p)
            if j.get("save"):
                saves[j.get("name", p.stem)] = j["save"]
                jobs[j.get("name", p.stem)] = j
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
        runner = Path(row.get("runner_log") or p.with_suffix(".runner.txt"))
        runner_text = runner.read_text(encoding="utf-8-sig", errors="replace") if runner.is_file() else ""
        if runner.is_file():
            logs.append([str(runner), signature(runner)])
        entries.append(dict(save=save, path=str(p), job=jobs.get(row.get("job")), runner=runner_text,
                            lines=p.read_text(encoding="utf-8-sig", errors="replace").splitlines()))
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
    # Preserve the existing recent-log forbid audit. Older VALID runs are useful
    # for FPS states but may predate fixes which introduced today's forbids.
    counts, recent = {}, []
    for entry in evidence:
        save = entry.get("save", entry["path"])
        counts[save] = counts.get(save, 0) + 1
        if counts[save] <= 3:
            recent.append(entry)
    evidence = recent
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
    native = native_period(gate, state, fps)
    override = int(state.get("rt_stats_period_frames", 0))
    # Unknown signatures must still fail closed. The native timer producers
    # also use the frame override while qualification is enabled.
    if native is not None and override > 0:
        return override / fps
    return native


def native_period(gate, state, fps):
    if producer_requirements(gate["select"]):
        return 120 / fps
    if "period_frames" in gate:
        return float(gate["period_frames"]) / fps
    sel = gate["select"].replace("\\", "")
    if "[hit material paks] loaded" in sel:
        return None  # unconditional startup event, not the periodic pool producer
    if producer_requirements(gate["select"]):
        return 120 / fps  # layer/hit_material.cpp: pool_counter_frame / pool_log
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
    if "period_s" in gate:
        return float(gate["period_s"])
    return None


# Literal producer signatures in required regexes -> settings needed to emit them.
# pool_log is called only with rt_stats enabled (layer/hit_material.cpp).
# The paks startup "loaded" line is deliberately absent: it is unconditional.
PRODUCER_SETTINGS = {
    "[hit material paks join]": {"rt_stats": "1"},
    "[hit material paks] frame": {"rt_stats": "1"},
    "[hit material paks COLOR]": {"rt_stats": "1"},
    "[hit material pool]": {"rt_stats": "1"},
    "[hit material pool basis]": {"rt_stats": "1"},
}


def producer_requirements(pattern):
    plain = pattern.replace("\\", "")
    return {k: v for signature, requirements in PRODUCER_SETTINGS.items()
            if signature in plain for k, v in requirements.items()}


def producer_problems(job, plan):
    states = phase_states(job, plan)
    phases = {p.get("label"): s for p, s in zip(plan, states)}
    errors = []
    for gate in job.get("expect_every", []):
        if int(gate.get("min", 1)) == 0:
            continue
        for key, value in producer_requirements(gate["select"]).items():
            labels = gate.get("windows", [])
            if not labels and not any(s.get(key) == value for s in states):
                errors.append(f"producer: /{gate['select']}/ requires a window with {key}={value}")
            for label in labels:
                if label in phases and phases[label].get(key) != value:
                    errors.append(f"producer: {label}: /{gate['select']}/ requires {key}={value}")
    for pattern in job.get("expect", []):
        for key, value in producer_requirements(pattern).items():
            if not any(s.get(key) == value for s in states):
                errors.append(f"producer: expect /{pattern}/ requires a window with {key}={value}")
    return errors


def developer_setting_problems(job, plan):
    """Reject explicit overrides which release policy silently neutralizes.

    Use knob metadata, its zero choice and qualified preset values, including
    compound/inverted keys. A qualified preset value is not a dev-only override.
    Missing dev inherits the development INI; explicit dev=0 selects release.
    """
    schema = read(ROOT / "data/settings_schema.json")
    disabled = {key: (knob, str(value))
                for knob in schema["knobs"] if knob.get("developer")
                for key, value in knob["choices"][0]["keys"].items()}
    state = {}
    errors = []
    for label, values in [("job", job.get("set", ""))] + [
            (p.get("label", f"phase {i}"), p.get("set", "")) for i, p in enumerate(plan)]:
        # Queue accepts a list of set strings as well as one comma-separated string.
        for value in values if isinstance(values, list) else [values]:
            _apply(state, value)
        if state.get("dev", "1") == "1" or state.get("experimental_options") == "1":
            continue
        for key in sorted(state.keys() & disabled.keys()):
            knob, off = disabled[key]
            preset_choice = knob.get("preset_values", {}).get(state.get("preset"), 0)
            qualified = knob["choices"][preset_choice]["keys"].get(key, off)
            if state[key] not in (off, str(qualified)):
                errors.append(f"release policy: {label}: {key}={state[key]} belongs to "
                              f"developer-only knob {knob['id']}; dev=0 and experimental_options is not 1 "
                              f"reset it to {off}; enable dev=1 or remove the override")
    return errors


def environment_problems(job):
    """Only paths/scalars cross the environment; the full job uses a context file.

    Mirror auto-run's job-derived values, counting UTF-16 code units on Windows.
    The terminator takes one of the 32767 units available for a value.
    """
    if "keep_reshade" in job and type(job["keep_reshade"]) is not bool:
        return ["job: keep_reshade must be a boolean"]
    args = job.get("args", [])
    options = {str(k).lower(): str(v) for k, v in zip(args, args[1:])
               if str(k).startswith("-")}
    tree = Path(job.get("tree") or ROOT)
    if not tree.is_absolute():
        tree = ROOT / tree
    tag = options.get("-tag", "")
    suffix = "." + tag if tag else ""
    values = {
        "BG3RTX_QUEUE_ROOT": str(ROOT),
        "BG3RTX_QUEUE_CONTEXT_FILE": str(ROOT / "captures/queue" / ("child-context." + "0" * 32 + ".json")),
        "BG3RTX_QUEUE_QUIT_FILE": str(ROOT / "captures/queue" / ("single." + "0" * 32 + ".quit")),
        "BG3RTX_RUN_TAG": tag,
        "BG3RTX_INI": str(tree / "captures" / f"auto{suffix}.ini"),
        "BG3RTX_SAVE_LOG": str(tree / "captures" / f"saves{suffix}.txt"),
        "BG3RTX_SAVE_ONLY": options.get("-save", str(job.get("save", ""))),
    }
    if "-userini" in options:
        path = Path(options["-userini"])
        values["BG3RTX_USER_INI"] = str(path if path.is_absolute() else tree / path)
    if "-validation" in {str(a).lower() for a in args}:
        values["VK_LAYER_SETTINGS_PATH"] = str(tree / "captures" / f"vk_layer_settings{suffix}.txt")
    return [f"environment: {name} value has {size} UTF-16 units; maximum 32766"
            for name, value in values.items()
            if (size := len(value.encode("utf-16-le")) // 2) > 32766]


def judged_labels(job, plan):
    labels = {w for g in job.get("expect_every", []) for w in g.get("windows", [])}
    labels.update(p["label"] for p in plan if re.match(r"^(cost|shot|timing|energy|rendered|presented|diag|turn)-", p.get("label", "")))
    return labels


def trace_event_budget(gate, phase, fps, latency, stall, completions=None):
    """Trace requests serialize through tracer.cpp's two-present recording and write.

    auto-run.ps1 queues one request file at a time; a dense trace_at list cannot
    promise simultaneous completions. Include the declared disk/write stall for
    each request, not just the last request's nominal timestamp.
    """
    if not gate.get("event") or "[trace] frame" not in gate["select"].replace("\\", ""):
        return None
    if completions is None:
        finish, completions = latency, []
        for at in phase.get("trace_at", []):
            finish = max(float(at), finish) + 2 / fps + stall
            completions.append(finish)
    end = float(phase.get("seconds", 0))
    settled = latency + float(gate.get("settle_s", 0))
    complete = sum(settled <= t < end for t in completions)
    need = int(gate.get("min", 1))
    eligible = [t for t in completions if t >= settled]
    minimum = eligible[need - 1] if len(eligible) >= need else None
    return dict(complete=complete, required=need, minimum_seconds_exclusive=minimum)


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
    trace_stall = capture_stall(meta)
    if not all(math.isfinite(v) and v >= 0 for v in (latency, trace_stall)):
        return ["windows: settings latency and trace stall budgets must be finite and nonnegative"], []
    errors, budgets = [], []
    # scripts/camera-plan.ps1 Assert-CameraPlanFields refuses every frame window without these (hl1 160108: two event-gate
    # frame windows, never seen by the per-gate frame check below, stopped the run before the launch).
    errors += [f"windows: {p.get('label')}: frame windows need stats_reset and 0 <= settle_frames < frames" for p in plan
               if p.get("frames") is not None and not (int(p["frames"]) > 0 and p.get("stats_reset")
                                                       and 0 <= int(p.get("settle_frames", 0)) < int(p["frames"]))]
    schedules = trace_schedules(job, plan)
    phases = {p.get("label"): (p, s) for p, s in zip(plan, phase_states(job, plan))}
    segment_meta = {p.get("label"): seg.get("live_flow", {})
                    for seg in meta.get("segments", [])
                    for p in plan[seg["offset"]:seg["offset"] + seg["count"]]}
    for g in job.get("expect_every", []):
        if not g.get("windows"):
            continue  # whole-run gates have no every-window promise
        for label in g["windows"]:
            if label not in phases:
                errors.append(f"windows: unknown gate window {label!r}")
                continue
            p, state = phases[label]
            fps = fps_windows.expected(job, state)["fps"]
            window_meta = segment_meta.get(label, meta)
            trace_stall = capture_stall(window_meta)
            if not math.isfinite(trace_stall) or trace_stall < 0:
                errors.append(f"windows: {label}: trace stall budget must be finite and nonnegative")
                continue
            need = int(g.get("min", 1))
            if need == 0:
                continue
            name = g.get("name", g["select"])
            trace = trace_event_budget(g, p, fps, latency, trace_stall, schedules[label])
            if trace is not None:
                if "trace_stall_s" not in window_meta:
                    errors.append(f"windows: {label}: {name}: declare live_flow.trace_stall_s, the upper stall budget per trace")
                budgets.append(dict(window=label, gate=name, **trace))
                if trace["complete"] < need:
                    errors.append(f"windows: {label}: {name}: trace schedule guarantees {trace['complete']} complete events, need {need}; "
                                  f"minimum > {trace['minimum_seconds_exclusive']} s including serialized trace stalls")
                continue
            dt = period(g, state, fps)
            if dt is None:
                if not g.get("event"):
                    errors.append(f"windows: {label}: {name}: unknown cadence; declare period_frames or period_s")
                continue
            if dt <= 0 or not math.isfinite(dt):
                errors.append(f"windows: {label}: {name}: invalid cadence {dt}")
                continue
            if p.get("trace_at") and "trace_stall_s" not in window_meta:
                errors.append(f"windows: {label}: {name}: declare live_flow.trace_stall_s, the upper stall budget per trace")
            if p.get("frames") is not None:
                frames = int(p["frames"])
                settle = int(p.get("settle_frames", 0))
                cadence = round(dt * fps)
                if not p.get("stats_reset"):
                    errors.append(f"windows: {label}: frame window requires stats_reset acknowledgement")
                complete = max(0, (frames - settle) // cadence)
                required = settle + need * cadence
                budgets.append(dict(window=label, gate=name, complete=complete, required=need,
                                    period_frames=cadence, minimum_frames=required,
                                    minimum_seconds_exclusive=required/fps + len(p.get("trace_at", []))*trace_stall,
                                    period_s=dt, fps=fps, fps_evidence=fps_windows.expected(job, state)))
                if complete < need:
                    errors.append(f"windows: {label}: {name}: {frames} frames provide {complete} complete periods, need {need} ({required} frames)")
                continue
            # Endpoint is exclusive. Worst alignment puts the first line one full
            # period after apply; one further line is consumed by the straddle rule.
            skip = 0 if g.get("event") else 1
            # auto-run restarts the phase clock at motion-ready after wait_log.
            # Settings/readiness time is outside this scheduled duration, not
            # another delay to subtract from the measured window. Still skip
            # the first stats line: readiness does not reset producer counters.
            apply_delay = 0 if p.get("wait_log") else latency
            overhead = apply_delay + float(g.get("settle_s", 0)) + len(p.get("trace_at", [])) * trace_stall
            available = float(p.get("seconds", 0)) - overhead
            complete = max(0, math.ceil(available / dt - 1e-9) - 1 - skip)
            minimum = overhead + (need + skip) * dt
            budgets.append(dict(window=label, gate=name, complete=complete, required=need,
                                period_s=dt, fps=fps, fps_evidence=fps_windows.expected(job, state), minimum_seconds_exclusive=minimum))
            if complete < need:
                errors.append(f"windows: {label}: {name}: worst-case {complete} complete lines, need {need}; "
                              f"{dt:g} s/line at {fps:g} fps; minimum > {minimum:g} s (settings/trace stalls and straddle included)")
    return errors, budgets


# These schedule observations/captures and diagnostics; they do not select shading
# pipeline variants. All rendering keys (including fg/rr) still require warming.
CONTROL = {"qa_exposure_hold", "fg_snap", "pan_capture", "pan_prepare", "pan_post", "pan_edge", "pan_water",
           "rt_stats", "rt_primary_check", "rt_primary_identity", "rt_primary_epoch", "ls_stats", "ls_check",
           "rf_stats", "rt_debug", "ls_debug", "gi_debug", "rf_debug", "rt_occ_stats", "rr_show",
           "fg_snap_any", "replay_pack", "rt_mesh_audit_frames", "rt_mesh_audits", "rt_stats_period_frames", "rt_stats_reset"}


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
    initial = {}
    _apply(initial, job.get("set", ""))
    out = []
    for key in sorted(keys):
        values = {s.get(key) for p, s in zip(plan[first:], states[first:])
                  if p.get("label") in labels and s.get(key) is not None}
        if key in startup:
            if values - {initial.get(key)}:
                out.append(f"warmup: startup-only key {key} differs from its startup value")
            continue
        # These host-side gates select no shader variant. Pre-warming order 1
        # would execute the header path before a first-use crash bisect can
        # measure it. Changed-key readiness is still checked independently.
        if key in CONTROL or key in {"rt_hair_groom_primary_order", "rt_hair_primary_passes"}:
            continue
        warmed = {s.get(key) for p, s in zip(plan[:first], states[:first])
                  if float(p.get("seconds", 0)) > 0 and not p.get("trace_at") and not p.get("serial")}
        missing = sorted(values - warmed)
        if missing:
            out.append(f"warmup: {key} values {', '.join(missing)} not exercised before {plan[first]['label']}")
    return out


def duration_budget(job, plan):
    """Derived lower bounds, with exclusive endpoints rounded to the next 0.1 s.

    Readiness waits are conditional and reported separately from scheduled time.
    Unknown event captures retain their existing observation bound: guessing a
    shorter capture completion time would turn savings into INVALID runs.
    """
    _, gates = window_problems(job, plan)
    by_label = {}
    for gate in gates:
        minimum = gate.get("minimum_seconds_exclusive")
        if minimum is not None:
            by_label[gate["window"]] = max(by_label.get(gate["window"], 0), minimum)
    latency = float(job.get("live_flow", {}).get("settings_latency_s", 1))
    fps = 30 if str(job.get("window_mode", "")).startswith("visible_unfocused") else float(job.get("live_flow", {}).get("expected_fps", 30))
    stall = float(job.get("live_flow", {}).get("trace_stall_s", 1))
    windows = []
    schedules = trace_schedules(job, plan)
    segment_meta = {p.get("label"): seg.get("live_flow", {})
                    for seg in job.get("live_flow", {}).get("segments", [])
                    for p in plan[seg["offset"]:seg["offset"] + seg["count"]]}
    previous = {}
    for index, (phase, state) in enumerate(zip(plan, phase_states(job, plan))):
        changed = {k: v for k, v in state.items() if previous.get(k) != v and k not in CONTROL}
        previous = state
        expected_wait = readiness_budget.estimate(job, phase, changed, first=index == 0)
        wait_floor = readiness_budget.minimum_wait(job, phase, state)
        fps = fps_windows.expected(job, state)["fps"]
        current = float(phase.get("seconds", 0))
        label = phase.get("label", "")
        bare_label = label.rsplit("--", 1)[-1]
        minimum = by_label.get(label, latency)
        unresolved = []
        if phase.get("wait_log") and not expected_wait["samples"]:
            unresolved.append("expected readiness unproven: no VALID same-save/key median; not charged as timeout")
        stall = capture_stall(segment_meta.get(label, job.get("live_flow", {})))
        if phase.get("trace_at"):
            minimum = max(minimum, schedules[label][-1])
        # dump.cpp: edge9 / water(raw_frames+1) / ordinary17 samples,
        # then three presents before completion; include settings and write budget.
        selected = [g for g in job.get("expect_every", []) if g.get("event") and int(g.get("min",1)) > 0
                    and label in g.get("windows", [])]
        if phase.get("serial") or any("[pan] burst" in g["select"].replace("\\", "") for g in selected):
            frames = 9 if state.get("pan_edge") == "1" else (min(16,max(2,int(state.get("pan_water_frames",8))))+1 if state.get("pan_water") == "1" else 17)
            minimum = max(minimum, latency + float(phase.get("capture_at",0)) + float(state.get("pan_delay_ms",0))/1000 + (frames+3)/fps + stall)
        # streamline.cpp: requested captures plus the next present's snap_finish.
        if any("present capture done" in g["select"] for g in selected):
            delay = float(state.get("fg_snap_delay_ms",0))/1000 or float(state.get("fg_snap_delay",0))/fps
            ready = 0 if state.get("fg_snap_any") == "1" else float(state.get("fg_snap_after",60))/fps
            minimum = max(minimum, latency + delay + ready + (float(state.get("fg_snap",1))+1)/fps + stall)
        # Held-turn endpoints define the requested camera travel. Wheel steps
        # post one notch per wheel_ms; neither needs a generic five-second sleep.
        if phase.get("kind") not in (None,"static"):
            minimum = max(minimum, float(phase.get("motion_end", phase.get("seconds",0))))
        if phase.get("wheel"):
            minimum = max(minimum, latency + abs(int(phase["wheel"]))*float(phase.get("wheel_ms",150))/1000)
        if bare_label.startswith("settle"):
            # 16 Halton phases plus alignment for ordinary reconstruction. For
            # rr_post_temporal, decay old EMA history below fp16's 1/1024 step.
            frames = 32
            if state.get("rr_post_temporal") == "1":
                frames = max(frames, math.ceil(math.log(1/1024)/math.log(15/16)))
            minimum = max(minimum, latency + frames/fps)
        # Readiness is not scene settling. Prefetch/GI creation can complete
        # before streaming, exposure and the first stable BVH windows. Preserve
        # the declared observation bound even when a wait_log is present.
        if bare_label.startswith(("warm", "prefetch")):
            if not phase.get("wait_log"):
                unresolved.append("pipeline readiness has no completion signal")
            unresolved.append("scene/pipeline settle bound retained; not a producer cadence")
            minimum = max(minimum,current)
        # Survival and reload regressions are measured in wall-clock time, not
        # in the number of periodic log lines. Hair failures were observed at
        # 2 s after enable and 25 s after reload (HAIR_VALIDATION.md).
        if (bare_label.startswith(("survive-", "set-", "toggle-")) or phase.get("reload_selected")
                or "after-reload" in bare_label or bare_label == "validation-strands"):
            unresolved.append("failure-latency observation bound retained")
            minimum = max(minimum,current)
        motion_endpoint = phase.get("kind") not in (None,"static") and minimum == float(phase.get("motion_end",phase.get("seconds",0)))
        minimum = minimum if (unresolved and minimum == current) or motion_endpoint else math.floor(minimum * 10 + 1e-7) / 10 + .1
        windows.append(dict(window=label, seconds=current, minimum_seconds=round(minimum, 3),
                            savings_seconds=round(max(0, current-minimum), 3), unresolved=unresolved,
                            readiness_timeout_seconds=float(phase.get("wait_log_timeout", 0)),
                            expected_readiness_seconds=max(expected_wait["seconds"], wait_floor),
                            readiness_samples=expected_wait["samples"], readiness_source=expected_wait["source"],
                            minimum_readiness_wait_seconds=wait_floor))
    total = sum(w["seconds"] for w in windows)
    minimum = sum(w["minimum_seconds"] for w in windows)
    findings = [f"duration: {w['window']}: {w['seconds']:g} s > 1.5x minimum {w['minimum_seconds']:g} s; save {w['savings_seconds']:g} s"
                for w in windows if w["seconds"] > 1.5*w["minimum_seconds"]]
    if total > 1.3*minimum:
        findings.append(f"duration: plan {total:g} s > 1.3x minimum {minimum:g} s; save {total-minimum:g} s")
    return dict(seconds=total, minimum_seconds=round(minimum, 3),
                unresolved=[dict(window=w["window"], reasons=w["unresolved"]) for w in windows if w["unresolved"]],
                expected_readiness_seconds=round(sum(w["expected_readiness_seconds"] for w in windows), 3),
                readiness_timeout_seconds=sum(float(p.get("wait_log_timeout", 0)) for p in plan if p.get("wait_log")),
                windows=windows, findings=findings)


def tighten(job, plan):
    """Use the shared lower bounds without changing gate counts or camera paths."""
    before = sum(float(p["seconds"]) for p in plan)
    budget = duration_budget(job, plan)
    for phase, window in zip(plan, budget["windows"]):
        old = float(phase["seconds"])
        # A burst window (serial) keeps its declared length when that is longer: the model (17 frames + one trace stall)
        # is a lower bound, and a pan_inputs burst (16 frames, 368 samples) completed 7.5 s after arming (he1 03:03 and
        # 03:13) against a 5.1 s model; a window that ends first restores pan_capture and aborts the burst.
        keep = phase.get("frames") is not None or phase.get("serial")
        new = max(old, window["minimum_seconds"]) if keep else window["minimum_seconds"]
        phase["seconds"] = new
        for key in ("motion_start", "motion_end"):
            if phase.get(key) == old:
                phase[key] = new
    job["estimated_phase_seconds"] = round(sum(float(p["seconds"]) for p in plan), 3)
    return dict(name=job["name"], before_seconds=before, after_seconds=job["estimated_phase_seconds"],
                windows=[dict(window=p["label"], before_seconds=w["seconds"], after_seconds=p["seconds"])
                         for p,w in zip(plan,budget["windows"]) if p["seconds"] != w["seconds"]])


def startup_producers(job, plan):
    """Put exact periodic producer checks after pipeline warmup, before captures."""
    if any(p.get("label", "").startswith("producer-sanity-") for p in plan):
        original = [i for i,p in enumerate(plan) if not p.get("label", "").startswith("producer-sanity-")]
        old_count = len(original)
        if any("complete phases="+str(old_count)+r"\b" in rx for rx in job.get("expect_runner", [])):
            job["expect_runner"] = [re.sub(r"phase=(\d+)", lambda m: "phase="+str(original[int(m[1])]), rx)
                                    .replace("complete phases="+str(old_count), "complete phases="+str(len(plan)))
                                    for rx in job.get("expect_runner", [])]
        return
    states = phase_states(job, plan)
    warm = [i for i,p in enumerate(plan) if p.get("label", "").startswith(("warm", "prefetch"))]
    if not warm:
        return
    fps = 30 if str(job.get("window_mode", "")).startswith("visible_unfocused") else float(job.get("live_flow", {}).get("expected_fps", 30))
    groups = {}
    for pattern in job.get("expect", []):
        related = next((g for g in job.get("expect_every", []) if g.get("match") == pattern and not g.get("event")), None)
        dt = period(related or dict(select=pattern), states[0], fps_windows.expected(job, states[0])["fps"])
        if dt is None or not math.isfinite(dt) or dt <= 0:
            continue
        target = next((states[i] for i,p in enumerate(plan) if related and p.get("label") in related.get("windows", [])), None)
        candidates = warm
        if target:
            varied = set().union(*(set(settings(p.get("set", ""))) for p in plan)) - CONTROL - {"qa_exposure_hold"}
            candidates = [i for i in warm if all(states[i].get(k) == target.get(k) for k in varied)] or warm
        groups.setdefault(candidates[0], []).append((pattern, dt))
    mapping, updated = {}, []
    for index, phase in enumerate(plan):
        mapping[index] = len(updated)
        updated.append(phase)
        if index not in groups:
            continue
        checks = groups[index]
        state = settings(phase.get("set", ""))
        requirements = {k:v for pattern,_ in checks for k,v in producer_requirements(pattern).items()}
        state.update(requirements)
        updated.append(dict(label="producer-sanity-"+phase["label"], kind="static", seconds=1.1,
                            capture_at=0, serial=0, set=",".join(k+"="+v for k,v in state.items()),
                            wait_log=[pattern for pattern,_ in checks],
                            wait_log_timeout=max(dt for _,dt in checks)+5))
        if index+1 < len(plan) and requirements:
            restored = settings(plan[index+1].get("set", ""))
            restored.update({k:states[index+1].get(k,"0") for k in requirements})
            plan[index+1]["set"] = ",".join(k+"="+v for k,v in restored.items())
    old_count = len(plan)
    plan[:] = updated
    job["expect_runner"] = [re.sub(r"phase=(\d+)", lambda m: "phase="+str(mapping.get(int(m[1]),int(m[1]))), rx)
                            .replace("complete phases="+str(old_count), "complete phases="+str(len(plan)))
                            for rx in job.get("expect_runner", [])]
    job.get("live_flow", {}).pop("producer_windows", None)


def settings(text):
    state = {}
    _apply(state, text)
    return state


def scope_scene_wait_gates(job, plan):
    """Keep strict timeout checks outside captures and expensive diagnostics."""
    eligible = {p['label'] for p, state in zip(plan, phase_states(job, plan))
                if scene_wait_eligible(p, state)}
    for phase in plan:
        if phase['label'] not in eligible and 'wait_stats' in phase:
            phase['wait_stats'] = [rx for rx in phase['wait_stats'] if 'scene wait:' not in rx]
    gates = []
    for gate in job.get('expect_every', []):
        if 'scene wait:' in gate.get('select', '') and '0 timeouts' in gate.get('match', ''):
            gate['windows'] = [label for label in gate.get('windows', [p['label'] for p in plan])
                               if label in eligible]
            if not gate['windows']:
                continue  # An empty windows list would gate the entire run.
        gates.append(gate)
    job['expect_every'] = gates


def frame_windows(job, plan):
    """Set sample counts before estimating seconds; FPS never selects a count."""
    scope_scene_wait_gates(job, plan)
    # Log-only GPU timers are latched at launch; preserve gate producers in release mode.
    initial = settings(job.get("set", ""))
    initial["detailed_logging"] = "1"
    job["set"] = ",".join(k+"="+v for k,v in initial.items())
    for target in [job] + job.get("live_flow", {}).get("segments", []):
        values = settings(target.get("set", ""))
        if int(values.get("rt_stats_period_frames", 0)) <= 0:
            values["rt_stats_period_frames"] = "60"
        target["set"] = ",".join(k+"="+v for k,v in values.items())
    # Producer sample counts are structural gates, not independent statistical
    # repeats. One reset-aligned period is sufficient unless a derivation names
    # a larger sample_frames floor. Keep capture/event counts untouched.
    for gate in job.get("expect_every", []):
        if not gate.get("event") and int(gate.get("min", 1)) > 0 and not gate.get("trend") and not gate.get("sample_frames"):
            gate["min"] = 1
    def cadence_regex(value):
        if isinstance(value, str):
            return re.sub(r"(?<![\d])(?:120|300|600)(?= frames)", r"\\d+", value)
        if isinstance(value, list):
            return [cadence_regex(x) for x in value]
        if isinstance(value, dict):
            return {k: cadence_regex(v) if k in {"select", "match", "required_if", "wait_log", "wait_log_fresh", "expect", "patterns", "expect_every"} else v for k,v in value.items()}
        return value
    for key in ("expect", "patterns", "expect_every"):
        if key in job:
            job[key] = cadence_regex(job[key])
    for phase in plan:
        for key in ("wait_log", "wait_log_fresh"):
            if key in phase:
                phase[key] = cadence_regex(phase[key])
    states = phase_states(job, plan)
    for phase, state in zip(plan, states):
        label = phase.get("label", "")
        gates = [g for g in job.get("expect_every", []) if label in g.get("windows", [])
                 and native_period(g, state, 30) is not None and int(g.get("min", 1)) > 0]
        if not gates:
            if label.rsplit("--", 1)[-1].startswith("settle"):
                frames = 32
                if state.get("rr_post_temporal") == "1":
                    frames = max(frames, math.ceil(math.log(1/1024)/math.log(15/16)))
                phase.update(frames=frames, settle_frames=0, stats_reset=True, wait_stats=[])
                phase["wait_log_timeout"] = max(30, float(phase.get("wait_log_timeout", 0)))
                phase["seconds"] = round(frames/fps_windows.expected(job, state)["fps"], 3)
            continue
        period_frames = int(state.get("rt_stats_period_frames", 60))
        # Legacy settle_s was authored against the 30 FPS runner cap. Translate
        # it once at that fixed reference, never with a scene's measured FPS.
        settle = max([int(g.get("settle_frames", math.ceil(float(g.get("settle_s", 0))*30))) for g in gates] or [0])
        samples = max([int(g.get("sample_frames", 0)) for g in gates] or [0])
        if samples > period_frames:
            period_frames = samples
            values = settings(phase.get("set", ""))
            values["rt_stats_period_frames"] = str(period_frames)
            phase["set"] = ",".join(k+"="+v for k,v in values.items())
        frames = settle + period_frames * max(int(g.get("min", 1)) for g in gates)
        phase.update(frames=frames, settle_frames=settle, stats_reset=True,
                     wait_stats=list(dict.fromkeys(g["select"] for g in gates if not g.get("when_present") and not g.get("required_if"))))
        phase["wait_log_timeout"] = max(30, float(phase.get("wait_log_timeout", 0)))
        # This is only a watchdog/cap estimate. The runner ends on frame count.
        fps = fps_windows.expected(job, state)["fps"]
        phase["seconds"] = round(frames/fps + len(phase.get("trace_at", []))*capture_stall(job.get("live_flow", {})), 3)


def retime(job, plan):
    """One regeneration path for generators and the offline CLI."""
    # Runtime reports derived values, e.g. rt_build=auto as 0/1. Emit that
    # resolved value so cfg acknowledgement cannot wait for the literal auto.
    states = phase_states(job, plan)
    for phase, state in zip(plan, states):
        if str(state.get('rt_build', '')).lower() == 'auto':
            phase['set'] = phase.get('set', '') + ',rt_build=' + pipeline_readiness.resolved(state)['rt_build']
    job.setdefault("live_flow", {})["fps_windows"] = [dict(state=list(sig), **v) for (save, sig),v in fps_windows.MODEL.items() if save == job.get("save")]
    if TRACE_EXPORT:
        job["live_flow"]["trace_export"] = dict(TRACE_EXPORT)
    if TRACE_STALL and any(p.get("trace_at") for p in plan):
        job["live_flow"]["trace_capture_stall"] = dict(TRACE_STALL)
        for segment in job["live_flow"].get("segments", []):
            segment.setdefault("live_flow", {})["trace_capture_stall"] = dict(TRACE_STALL)
    frame_windows(job, plan)
    pipeline_readiness.apply(job, plan)
    changes = tighten(job, plan)
    for phase, state in zip(plan, phase_states(job, plan)):
        if phase.get("wait_log"):
            patterns = phase["wait_log"]
            patterns = [patterns] if isinstance(patterns, str) else patterns
            periods = [period(dict(select=rx), state, fps_windows.expected(job,state)["fps"]) for rx in patterns]
            phase["wait_log_timeout"] = max(float(phase.get("wait_log_timeout",0)), max([dt+5 for dt in periods if dt is not None] or [0]))
    for phase, limit in zip(plan, readiness_budget.timeouts(job, plan)):
        if phase.get('wait_log'):
            phase['wait_log_timeout'] = limit
    budget = duration_budget(job, plan)
    job["timeout"] = max(float(job.get("timeout", 0)), math.ceil(budget["seconds"] + budget["readiness_timeout_seconds"] + 120))
    return changes


def tighten_files(directory, cache=None, job_names=None, frozen=False):
    """Refresh checked-in artifacts with the same model used by lint/preflight."""
    if frozen:
        from tools.live_batches import lint_snapshot
        lint_snapshot.load()
    else:
        index = corpus(default_results(), cache or ROOT / "captures/.cache/live_flow_corpus.json", newest=100000)
        fps_windows.configure(index["entries"])
        configure_trace_exports(index["entries"])
        readiness_budget.configure(index["entries"])
    reports = []
    for path in job_files(directory):
        if job_names and path.stem not in job_names:
            continue
        job, plan, plan_path = load_job(path)
        first_warm = next((p for p in plan if p.get("label", "").startswith(("warm", "prefetch"))), None)
        if first_warm is not None:
            old = float(first_warm["seconds"])
            first_warm["seconds"] = max(old, SCENE_SETTLE_SECONDS.get(job.get("save"), old))
            for key in ("motion_start", "motion_end"):
                if first_warm.get(key) == old:
                    first_warm[key] = first_warm["seconds"]
        # Cold-start prefetch is a completion signal, unlike a fixed sleep.
        for phase in plan:
            if phase.get("label", "").startswith(("warm", "prefetch")) and not phase.get("wait_log"):
                phase.update(wait_log=[r"\[shaders\] cache: prefetched ",
                                       r"gi: gi/[^ ]+ generation "], wait_log_timeout=180)
        if first_warm is not None:
            patterns = first_warm.get("wait_log", [])
            if isinstance(patterns, str):
                patterns = [patterns]
            first_warm["wait_log"] = list(dict.fromkeys(
                [p for p in patterns if p != r"\[auto\] scene ready"] + [r"\[auto\] scene ready"]))
        startup_producers(job, plan)
        reports.append(retime(job, plan))
        for target, value in ((plan_path, plan), (path, job)):
            text = target.read_text(encoding="utf-8-sig")
            match = re.search(r"\n( +)\S", text)
            indent = len(match[1]) if match else 2
            target.write_text(json.dumps(value, indent=indent) + "\n", encoding="utf-8")
    return reports


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


def plan_field_problems(plan):
    registry = read(ROOT / "scripts/camera-plan-fields.json")
    allowed = set(registry["runner"]) | set(registry["analysis_only"])
    return [f"Unknown camera plan field: {key} (phase {i})"
            for i, step in enumerate(plan) for key in sorted(set(step) - allowed)]


def launch_problems(job, duration):
    limit = float(job.get("max_minutes", 15))
    if not math.isfinite(limit) or limit <= 0:
        return ["duration: max_minutes must be finite and positive"]
    if duration["seconds"] + duration.get("expected_readiness_seconds", 0) > 60 * limit:
        return ["duration: launch exceeds max_minutes (default 15); split the batch or explicitly set max_minutes"]
    return []


def lint(job, plan, evidence, memo=None):
    regex, coverage = regex_problems(job, evidence, memo)
    windows, budgets = window_problems(job, plan)
    duration = duration_budget(job, plan)
    timeout = []
    if "timeout" in job and float(job["timeout"]) < duration["seconds"] + duration["readiness_timeout_seconds"]:
        timeout.append("duration: timeout does not cover plan + readiness allowance")
    return dict(problems=plan_field_problems(plan) + developer_setting_problems(job, plan) + pipeline_readiness.problems(job, plan) + regex + environment_problems(job) + windows + producer_problems(job, plan) + warmup_problems(job, plan) + timeout, regex_coverage=coverage, windows=budgets, duration=duration, replay=fps_windows.replay(job, plan, evidence))


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
    ap.add_argument("--newest", type=int, default=100000)
    ap.add_argument("--retime", action="store_true", help="regenerate durations with the corpus FPS model")
    ap.add_argument("--batches", help="comma-separated batch folders to regenerate with --retime")
    ap.add_argument("--replay-only", action="store_true", help="report historical gate coverage without regex-forbid audit")
    ap.add_argument("--report")
    ap.add_argument("--job-names", help="comma-separated job stems to check")
    ap.add_argument("--tree", help="actual runner tree for preflight plan resolution")
    ap.add_argument("--preflight", action="store_true", help="refuse invalid or overlong launches before starting the runner")
    ap.add_argument("--frozen", action="store_true", help="lint with the checked-in snapshot; never read or cache full live logs")
    ap.add_argument("--active", action="store_true", help="check only active jobs in duration_lint.json")
    a = ap.parse_args(argv)
    if a.newest < 1:
        ap.error("--newest must be positive")
    frozen = a.frozen
    if not frozen:
        index = corpus(a.results, a.cache, a.newest)
        if not index["entries"] and not a.replay_only:
            print("live corpus empty: using the frozen snapshot", flush=True)
            frozen = True
    if frozen:
        if a.replay_only:
            ap.error('--frozen cannot replay historical logs')
        from tools.live_batches import lint_snapshot
        snapshot = lint_snapshot.load()
        index = dict(entries=[], matches=snapshot['matches'], saves=snapshot['saves'],
                     valid_logs=snapshot['valid_logs'])
        print(f"Frozen VALID snapshot: {snapshot['valid_logs']} logs; no raw corpus cache", flush=True)
    else:
        fps_windows.configure(index["entries"])
        configure_trace_exports(index["entries"])
        readiness_budget.configure(index["entries"])
        print(f"VALID corpus: {len(index['entries'])} logs, {len(index['saves'])} saves; {a.results}", flush=True)
    report = dict(corpus={k: v for k, v in index.items() if k not in ("entries", "key", "matches")}, jobs={})
    failures = 0
    memo = index.get("matches", {})
    active = active_job_files() if a.active else None
    for path in job_files(a.target):
        if (active is not None and path not in active) or (a.job_names and path.stem not in a.job_names.split(",")):
            continue
        try:
            if a.preflight and a.tree:
                job = read(path)
                args = job["args"]
                plan_path = Path(a.tree) / args[args.index("-MovePlan") + 1]
                plan = read(plan_path)
            else:
                job, plan, plan_path = load_job(path)
            changes = None
            if a.retime and (not a.batches or path.parent.parent.name in a.batches.split(",")):
                changes = retime(job, plan)
                write(plan_path if a.preflight and a.tree else load_job(path)[2], plan)
                write(path, job)
                manifest_path = path.parent.parent / (path.stem + ".manifest.json")
                if job.get("live_flow", {}).get("segments") and manifest_path.is_file():
                    manifest = read(manifest_path)
                    manifest["plan"] = plan
                    for segment in manifest["slices"]:
                        for original, updated in zip(segment["plan"], plan[segment["offset"]:segment["offset"]+segment["count"]]):
                            for field in ("seconds", "motion_start", "motion_end", "wait_log_timeout"):
                                if field in updated:
                                    original[field] = updated[field]
                        segment["job"].setdefault("live_flow", {})["fps_windows"] = job["live_flow"]["fps_windows"]
                        if "trace_capture_stall" in job["live_flow"]:
                            segment["job"]["live_flow"]["trace_capture_stall"] = job["live_flow"]["trace_capture_stall"]
                        segment["job"]["estimated_phase_seconds"] = round(sum(p["seconds"] for p in segment["plan"]), 3)
                    write(manifest_path, manifest)
            if a.replay_only:
                budget = duration_budget(job, plan)
                replay_plan = [dict(p, seconds=w["minimum_seconds"]) for p,w in zip(plan,budget["windows"])]
                result = dict(problems=[], duration=budget, replay=fps_windows.replay(job, replay_plan, index["entries"]))
            else:
                result = lint(job, plan, index["entries"], memo)
            if changes is not None:
                result["changes"] = changes
            duration = result["duration"]
            print(f"{job.get('name', path.stem)}: expected duration {duration['seconds']:g} s after load; minimum {duration['minimum_seconds']:g} s; expected readiness {duration['expected_readiness_seconds']:g} s; readiness timeout allowance {duration['readiness_timeout_seconds']:g} s", flush=True)
            unproven = [w["window"] for w in duration["windows"] if w["readiness_timeout_seconds"] and not w["readiness_samples"]]
            if unproven:
                print(f"{job.get('name', path.stem)}: expected readiness unproven for {', '.join(unproven)}; no VALID same-save/key median (not charged as timeout)", flush=True)
            for finding in duration["findings"]:
                print(f"{job.get('name', path.stem)}: {finding}", flush=True)
            if a.preflight:
                result["problems"] += launch_problems(job, duration)
            if not index["entries"] and not frozen:
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
    if not frozen:
        write(a.cache, index)
    unproven = sum(bool(r.get("replay", {}).get("problems")) for r in report["jobs"].values())
    print(f"replay gates: {len(report['jobs'])} jobs, {unproven} with unsatisfied historical evidence (see report)", flush=True)
    print(f"live-flow: {len(report['jobs'])} jobs, {failures} failing", flush=True)
    return int(bool(failures or a.replay_only and unproven))


if __name__ == "__main__":
    sys.exit(main())
