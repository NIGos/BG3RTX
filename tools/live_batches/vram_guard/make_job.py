"""Generate vg3 only. Predictions are recorded first in predicted.json."""
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
NAME = "vg3-s8-low-long-view-fg2"


def step(label, seconds, on=0, direction=0, trace=False, wheel=0, hold=1):
    s = dict(label=label, kind="rotate" if direction else "static",
             speed="slow" if direction else "static", seconds=seconds,
             repeat_ms=0, scan=16, capture_at=0, serial=0,
             motion_start=1 if direction else seconds,
             motion_end=seconds - 1 if direction else seconds,
             pixels_per_second=60 if direction else 0, direction=direction,
             set=f"rt_vram_guard={on},rt_vram_compact={on},rt_capture_elastic={on},rt_debug=0,rt_stats=0,qa_exposure_hold={hold}")
    if trace:
        s["trace_at"] = [4]
    if wheel:
        s["wheel"] = wheel
    return s


plan = [step("warmup-off", 20, hold=0), step("lower-off", 8, wheel=10, hold=0)]
for state, on in [("off-a", 0), ("on", 1), ("off-b", 0)]:
    plan += [step(f"settle-{state}", 10, on),
             step(f"turn-{state}-r", 20, on, 1),
             step(f"turn-{state}-l", 20, on, -1)]
plan += [step("settle-shot-off", 10), step("shot-off-a", 6, trace=True),
         step("shot-on", 6, 1, trace=True), step("shot-off-b", 6, trace=True),
         step("recover-settle-on", 25, 1), step("recover-on", 20, 1),
         step("restore", 5, hold=0)]
on_windows = ["turn-on-r", "turn-on-l", "recover-on"]
turns = [s["label"] for s in plan if s["kind"] == "rotate"]
job = json.loads((HERE / "archive/vg2-s8-low-long-view-fg2.json").read_text())
job.update(name=NAME, estimated_phase_seconds=sum(s["seconds"] for s in plan))
job["timeout"] = job["estimated_phase_seconds"] + 330
# Zero explicitly disables any inherited synthetic override: use the real driver budget.
job["set"] = job["set"].replace("rt_vram_guard_budget_mb=15640", "rt_vram_guard_budget_mb=0")
job["args"] = ["-Move", "-MovePlan", "captures/live_batches/vram_guard/plans/vg3_s8.json"]
job["expect"] = [x for x in job["expect"] if not any(k in x for k in ["capacity", "repacked"])]
job["expect_every"] = []
for gate in json.loads((HERE / "archive/vg2-s8-low-long-view-fg2.json").read_text())["expect_every"]:
    if gate["name"].startswith("recovery has"):
        continue
    if gate["name"].startswith("VRAM:"):
        gate["select"] = r"\[mem\] ours \("
    gate["windows"] = turns + ["recover-on"] if gate["name"].startswith("scene wait") else on_windows
    job["expect_every"].append(gate)
job["expect_every"].append(dict(name="capture summaries in every matched turn", select=r"\[geo\] RT capture, last 120 frames:",
    match=r"alpha draws made opaque .*opaque draws dropped", min=2, windows=turns))
job["expect_runner"] = [r"\[window-mode\] visible_unfocused: hwnd=", r"\[motion-plan\] posted pointer centre to owned unfocused HWND",
    r"\[wheel-input\] phase=1 notches=10\b"] + [rf"\[motion-trace\] phase={i} label={s['label']} repeat=0" for i,s in enumerate(plan) if "trace_at" in s]
job["expect_runner"] += [rf"\[motion-plan\] complete phases={len(plan)}\b", "^Run ended: done"]
job["post"] = 'py -3 tools/live_batches/vram_guard/gate.py --log "{log}" --runner "{runner}"'
job["post_gate"] = True
job["purpose"] = "Real-budget vg3 motion quality: identical right/left OFF-a / ON / OFF-b cycles in one launch. OFF-OFF derives per-direction capture-drop tolerance. Static energy OFF/ON/OFF within 0.92%. Every ON snapshot within real budget with consumer accounting. Post-analysis is mandatory; no live qualification from a dryrun."
for path, data in [(HERE / "plans/vg3_s8.json", plan), (HERE / f"jobs/{NAME}.json", job)]:
    path.write_text(json.dumps(data, indent=1) + "\n")
print(f"{len(plan)} phases, {job['estimated_phase_seconds']} s, 3 traces; real budget; mandatory post gate")
