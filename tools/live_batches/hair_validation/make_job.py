"""Generate only; no inbox staging or game launches. predicted.json precedes jobs."""
import importlib.util
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.live_batches.combine import combine

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("hair_primary_jobs", HERE.parent / "hair_primary/make_job.py")
hp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hp)

# The runner takes seconds; size warmup and cost windows in rendered frames.
UNFOCUSED_FPS = 30
PRIMARY_LOG_FRAMES = 300  # layer/hair_primary.cpp:560
COST_FRAMES = 750  # Two 300-frame lines, settings latency and alignment margin.


def phase(label, seconds=7, on=False, trace=False, turn=0, wheel=0):
    p = hp.step(label, seconds, trace, **(hp.ON if on else hp.OFF))
    if turn:
        p.update(kind="rotate", turn_mode="keys", direction=turn, motion_start=0, motion_end=seconds)
    if wheel:
        p.update(wheel=wheel, wheel_ms=150)
    return p


def make(validation=False):
    warm = [phase("warmup", PRIMARY_LOG_FRAMES / UNFOCUSED_FPS),
            phase("warm-strands", PRIMARY_LOG_FRAMES / UNFOCUSED_FPS, True)]
    shots = [phase("shot-cards-a", trace=True), phase("shot-strands-a", on=True, trace=True),
             phase("shot-cards-b", trace=True), phase("shot-strands-b", on=True, trace=True),
             phase("shot-cards-c", trace=True)]
    motion = [phase("turn-q-a", 18, True, turn=-1), phase("turn-e-a", 18, True, turn=1),
              phase("zoom-out", 5, True, wheel=-8), phase("turn-q-b", 18, True, turn=-1),
              phase("zoom-in", 5, True, wheel=8), phase("turn-e-b", 18, True, turn=1)]
    toggles = [phase(f"toggle-{i}", 5, bool(i % 2)) for i in range(6)]
    temporal = []
    for label, on, serial in (("cards", False, 98410), ("strands", True, 98411)):
        raw = phase("rendered-"+label, 5, on, turn=-1)
        raw.update(capture_at=.1, serial=serial)
        raw["set"] += f",pan_prepare=1,pan_edge=1,pan_capture={serial},fg_snap=0"
        shown = phase("presented-"+label, 5, on, turn=1)
        shown["set"] += ",pan_prepare=0,pan_capture=0,fg_snap=16,fg_snap_any=1"
        temporal += [raw, shown]
    # Captures are separate from judged GPU-cost windows; disable recorder there.
    cost_seconds = COST_FRAMES / UNFOCUSED_FPS
    cost = [phase("cost-cards-a", cost_seconds), phase("cost-strands", cost_seconds, True), phase("cost-cards-b", cost_seconds)]
    reload = [dict(phase("reload-selected", 5, True), reload_selected=True), phase("cost-after-reload", cost_seconds, True)]
    restore = [phase("restore", 5)]
    for p in cost + reload + restore:
        p["set"] += ",pan_prepare=0,pan_edge=0,pan_capture=0,fg_snap=0"
    plan = combine([warm, shots, motion, toggles, temporal, cost, reload, restore] if not validation else
                   [warm, [phase("validation-strands", cost_seconds, True)], restore])
    return plan


def job(name, save, plan, validation=False):
    j = hp.job(name, save)
    labels = [p["label"] for p in plan]
    j.update(live_batch="hair-validation", timeout=sum(p["seconds"] for p in plan)+420,
             estimated_phase_seconds=sum(p["seconds"] for p in plan), window_mode="visible_unfocused_fg")
    j["args"] = ["-RealPresent", "-Move", "-MovePlan", f"captures/live_batches/hair_validation/plans/{name}.json"]
    if validation:
        j["args"] += ["-Validation", "-VSet", "validate_sync=true,gpuav_enable=true"]
    j["set"] = j["set"].replace("dump_png_width=0", "dump_png_width=960")
    # Keep actual Ultra qualified keys: the inherited diagnostic jobs disabled OMM.
    j["set"] = j["set"].replace("rt_omm=0", "rt_omm=1")
    j["expect"] = [r"\[auto\] scene ready", r"\[cfg\] settings read", hp.STRANDS]
    j["forbid"] = [p for p in j["forbid"] if p not in
                   (r"\[hair primary\] .*refused", r"\[bvh\] mesh pool full at")]
    j["forbid"] += [r"\[hair primary\].*\b[1-9]\d* pipelines refused", r"(?i)device (?:was )?lost", r"\[fault\]\s+(read|write|execute) invalid",
                     r"invalid/missing native state [1-9]\d*", r"retired bounds-invalid primitives [1-9]\d*"]
    j["patterns"] += [r"\[fault\]", r"\[mem\]", "pool full", "DEVICE_LOST"]
    # Shared scene/capacity gates in all judged cost windows. Do not import
    # hair_primary's unrelated fibre phase or stale window names.
    keep = ("scene wait", "capacity:", "opaque cutouts", "no background build", "no 'building")
    gates = [dict(g) for g in hp.hg.hq.expect_every("dlg") if g["name"].startswith(keep)]
    judged = [l for l in labels if l.startswith("cost-")] or ["validation-strands"]
    for gate in gates:
        gate["windows"] = judged
    gates.append(dict(name="hair guards zero in every judged window", select=r"\[alpha hair\] native coverage records",
                      match=r"invalid/missing native state 0;.*retired bounds-invalid primitives 0;", min=1, windows=judged))
    gates.append(dict(name="strands participate in each ON cost window", select=r"\[hair primary\] frame \d+:",
                      match=hp.STRANDS, min=1, period_frames=PRIMARY_LOG_FRAMES,
                      windows=["validation-strands"] if validation else ["cost-strands", "cost-after-reload"]))
    if not validation:
        for label, serial in (("cards",98410),("strands",98411)):
            gates += [dict(name="rendered K8 "+label, select=rf"\[pan\] burst {serial} complete",
                           match=rf"\[pan\] burst {serial} complete: 8 frames, 20 samples, transport ok",
                           event=True,min=1,windows=["rendered-"+label]),
                      dict(name="presented P16 "+label,select=r"\[sl\] present capture done",
                           match=r"\[sl\] present capture done",event=True,min=1,windows=["presented-"+label])]
    j["patterns"] += [r"\[pan\]", r"\[sl\] present capture", r"\[sl\] DLSS-G frame"]
    j["expect_every"] = gates
    j["expect_runner"] = [r"window-mode.*visible_unfocused", rf"\[motion-plan\] complete phases={len(plan)}\b", "^Run ended: done"]
    j["expect_runner"] += [rf"\[motion-trace\] phase={i} label={p['label']} repeat=0" for i, p in enumerate(plan) if p.get("trace_at")]
    if not validation:
        j["expect_runner"].append(r"\[hair-reload\] selected save reload complete")
    j["purpose"] = "Hair validation: same-run cards/strands, held Q/E enter/leave view, zoom LOD, repeated toggles, diagnostic-free Ultra costs. No quality pass without analyzer evidence."
    return j


def main():
    assert (HERE / "predicted.json").exists()
    for folder in ("jobs", "plans"):
        (HERE/folder).mkdir(exist_ok=True)
    for name, save, validation in [("hv1-dialogue1", "Test Dialogue1", False),
                                    ("hv2-camp", "Campsite - 27h", False),
                                    ("hv3-s8", "Water Queens House", False),
                                    ("hv4-gpuav", "Test Dialogue1", True)]:
        plan = make(validation)
        for folder, value in (("plans", plan), ("jobs", job(name, save, plan, validation))):
            (HERE/folder/f"{name}.json").write_text(json.dumps(value, indent=2)+"\n", encoding="utf-8")
    print("4 jobs generated, 3 save launches + isolated short GPU-AV; no queue staging")


if __name__ == "__main__":
    main()
