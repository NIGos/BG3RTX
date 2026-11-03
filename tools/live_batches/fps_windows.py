"""Rendered-frame throughput and historical gate replay. No game/queue writes."""
from collections import defaultdict
import math
from functools import lru_cache
import re

from scripts import queue_gates as gates

# Additional milliseconds/frame; conservative ceilings of observed diagnostic
# slowdowns. Sources and limitations are recorded in LIVE_FLOW.md.
COST_MS = {"rt_stats": 100, "rt_primary_check": 80,
           "omm_check": 80, "rt_omm_reference": 80, "gpu_av": 200}
MODEL = {}
PREPARED = {}
STAMPED = {}


def stamped(entry):
    if entry["path"] not in STAMPED:
        STAMPED[entry["path"]] = gates.stamped_lines("\n".join(entry["lines"]))
    return STAMPED[entry["path"]]


@lru_cache(None)
def preset_defaults(preset):
    # The shipped central preset table also resolves implicit diagnostic keys.
    from tools.live_batches.combine import defaults
    return defaults(preset)


def signature(job, state):
    state = dict(preset_defaults(state.get("preset", "")), **state)
    enabled = lambda key: state.get(key, "0") not in ("0", "", None)
    return tuple(int(value) for value in (
        enabled("rt_stats"), enabled("rt_primary_check"),
        enabled("rt_stats") and enabled("rt_omm"),
        enabled("rt_omm_reference"),
        any("gpuav_enable=true" in str(arg).lower() for arg in job.get("args", []))))


def quantile(values):
    """Lower nearest-rank tenth percentile; never interpolate toward faster FPS."""
    values = sorted(values)
    return values[max(0, math.ceil(len(values) * .1) - 1)]


def window_samples(entry):
    """Only counter pairs wholly within one applied settings window qualify."""
    if entry["path"] in PREPARED:
        return PREPARED[entry["path"]]
    job = entry.get("job")
    runner = entry.get("runner", "")
    if not job or not runner:
        return []
    lines = stamped(entry)
    wins = gates.windows(runner, [], lines)
    state = {}
    gates._apply(state, job.get("set", ""))
    out = []
    for w in wins:
        # Runner settings are the historical truth; current plans may have changed.
        if w["set"] is None:
            continue
        gates._apply(state, w["set"])
        pairs = []
        # input copy and DLSS-G both count app frames, never displayed FG frames.
        for pattern in (r"\[sl\] input copy frame (\d+):", r"\[sl\] DLSS-G frame (\d+):"):
            pairs = [(t, int(m[1])) for t, line in lines
                     if w["applied"] <= t < w["end"] and (m := re.search(pattern, line))]
            if len(pairs) >= 2:
                break
        rates = [(b[1]-a[1])/(b[0]-a[0]) for a, b in zip(pairs, pairs[1:])
                 if b[1] > a[1] and b[0] > a[0]]
        if rates:
            effective = dict(state)
            # Presets evolve. A historical producer is direct evidence that the
            # implicit OMM check ran; do not infer it from today's Ultra preset.
            if "rt_omm" not in state:
                effective["rt_omm"] = str(int(any("omm check (" in line for t,line in lines if w["applied"] <= t < w["end"])))
            out.append((signature(job, effective), quantile(rates), w))
    PREPARED[entry["path"]] = out
    return out


def configure(entries):
    MODEL.clear()
    PREPARED.clear()
    STAMPED.clear()
    samples = defaultdict(list)
    for entry in entries:
        for sig, fps, _ in window_samples(entry):
            samples[entry.get("save"), sig].append((fps, entry["path"]))
    for key, values in samples.items():
        MODEL[key] = dict(fps=quantile([v[0] for v in values]), windows=len(values),
                          logs=sorted({v[1] for v in values}))


def expected(job, state):
    """Same-save, same diagnostic state; an explicit FPS is an upper bound."""
    ceiling = float(job.get("live_flow", {}).get("expected_fps", 30))
    if str(job.get("window_mode", "")).startswith("visible_unfocused"):
        ceiling = min(30, ceiling)
    if not math.isfinite(ceiling) or ceiling <= 0:
        raise ValueError("expected_fps must be finite and positive")
    sig = signature(job, state)
    observed = MODEL.get((job.get("save"), sig))
    if observed is None:
        observed = next((v for v in job.get("live_flow", {}).get("fps_windows", []) if tuple(v["state"]) == sig), None)
    if observed:
        return dict(fps=min(ceiling, observed["fps"]), source="VALID p10", **{k:v for k,v in observed.items() if k != "fps"})
    base = MODEL.get((job.get("save"), (0, 0, 0, 0, 0)), {}).get("fps")
    if base is None:
        base = next((v["fps"] for v in job.get("live_flow", {}).get("fps_windows", []) if not any(v["state"])), ceiling)
    cost = sum(ms * on for ms, on in zip(COST_MS.values(), sig))
    return dict(fps=(min(base, ceiling) if cost == 0 else 1000/(1000/min(base, ceiling)+cost)), source="per-key cost fallback", cost_ms=cost)


def replay(job, plan, entries):
    """Replay unchanged historical lines under derived window endpoints.

    No synthesized producer lines or stretched timestamps: absent signatures and
    states stay unproven. Each target window uses a historical window in the same
    diagnostic state, from the newest VALID log of its save.
    """
    entry = next((e for e in entries if e.get("save") == job.get("save")), None)
    if entry is None:
        return dict(log=None, problems=["no VALID log for save"])
    text = "\n".join(entry["lines"])
    problems = ["expect missing: " + rx for rx in job.get("expect", [])
                if not re.search(rx, text, re.I | re.M)]
    historical = window_samples(entry)
    states = gates.phase_states(job, plan)
    snippets = {}
    for gate in job.get("expect_every", []):
        if not gate.get("windows") and not gate.get("trend"):
            problems.extend(gates.check([gate], text))
            continue
        labels = gate.get("windows", []) or list(dict.fromkeys(gate.get("baseline", []) + gate.get("tail", [])))
        selected = []
        for label in labels:
            index = next((i for i,p in enumerate(plan) if p.get("label") == label), None)
            if index is None:
                problems.append(f"{label}: unknown replay window")
                continue
            candidates = [w for sig, _, w in historical if sig == signature(job, states[index])]
            if not candidates:
                problems.append(f"{label}: {gate.get('name', gate['select'])}: no historical window in same diagnostic state")
                continue
            # Newest matching state window; retain real settings-read delay and
            # never borrow lines past its original settings boundary.
            w = candidates[-1]
            end = min(w["end"], w["start"] + float(plan[index]["seconds"]))
            stamp = lambda t: f"{int(t//3600)%24:02}:{int(t//60)%60:02}:{t%60:06.3f}"
            if gate.get("trend"):
                problems.append(f"{label}: trend replay requires distinct historical baseline/tail attribution")
                continue
            runner = f"{stamp(w['start'])} [pan-plan] window 0\n{stamp(end)} [motion-plan] complete"
            key = (w["index"], end)
            if key not in snippets:
                snippets[key] = "\n".join(line for t,line in stamped(entry) if w["start"] <= t < end)
            snippet = snippets[key]
            selected.extend(gates.check([dict(gate, windows=[label])], snippet, runner, [dict(label=label)]))
        problems.extend(selected)
    return dict(log=entry["path"], status="UNPROVEN" if problems else "PASS", problems=problems,
                note="Historical coverage only; missing evidence is unproven, never a rendering pass.")
