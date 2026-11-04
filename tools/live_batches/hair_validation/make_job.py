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
POST_RELOAD_SECONDS = 26.1  # Observed 25 s failure + 1 s apply + exclusive endpoint.


def phase(label, seconds=7, on=False, trace=False, turn=0, wheel=0):
    p = hp.step(label, seconds, trace, **(hp.ON if on else hp.OFF))
    if turn:
        p.update(kind="rotate", turn_mode="keys", direction=turn, motion_start=0, motion_end=seconds)
    if wheel:
        p.update(wheel=wheel, wheel_ms=150)
    return p


# Reload bisect (hv5; hv1 2026-11-03 18:40 lost the device after the F8 reload): one launch with every hair capability
# latched at startup, then one F8 per stage, the keys of each stage set before its reload and held through a 60 s
# survival window (3 x 20 s: phases stay within the lean 5-25 s rule). The first stage whose survival windows have no
# frames names the culprit. Stage keys: on top of the previous stage.
# rt_hair_groom_primary is latched on at startup (main 8df7978d), so the groom stage is also the strands-on-screen
# stage; the last stage adds the fibre pass (rt_hair_fibre, a live core key).
STAGES = [("a-native", dict(rt_native_hair="1", rt_native_scalp="1", rt_hair_physical_origin="0", rt_hair_groom="0",
                            rt_hair_groom_primary="1", rt_hair_fibre="0", rt_hair_groom_primary_order="1")),   # cards stay in order 0
          ("b-origin", dict(rt_hair_physical_origin="1")),
          ("c-strands", dict(rt_hair_groom="1")),
          ("d-fibre", dict(rt_hair_fibre="1"))]
STRAND_STAGES = ("c-strands", "d-fibre")   # strands on screen: their survival windows need strand draws
# Header-order A/B is now isolated in hv6, alternating 0/1/0/1.
ORDER = [("order-prepass", "0"), ("order-gbuffer", "1")]
SURVIVE = [1, 2, 3]   # 3 adjacent same-key 20 s windows: uninterrupted 60 s > 25 s failure.


def stage_phase(label, seconds, keys, trace=False):
    p = hp.hg.step(label, seconds, trace, **keys)
    p["set"] += ",pan_prepare=0,pan_edge=0,pan_capture=0,fg_snap=0"
    return p


def make_bisect():
    keys = {}
    every = {}
    for _, more in STAGES:
        every.update(more)
    # Warm every value the stages use before the first judged window (pipelines, groom uploads), then stage A.
    plan = [stage_phase("warmup", PRIMARY_LOG_FRAMES / UNFOCUSED_FPS, every),
            stage_phase("warm-settle", 10, dict(STAGES[0][1]))]
    for name, more in STAGES:
        keys.update(more)
        plan.append(stage_phase(f"set-{name}", 5, keys))   # the stage's keys before its reload
        plan.append(dict(stage_phase(f"reload-{name}", 5, keys), reload_selected=True))
        # The strand stages' windows hold a whole 300-frame strand line at the unfocused 30 fps (> 21 s, live_flow).
        plan += [stage_phase(f"survive-{name}-{i}", 25 if name in STRAND_STAGES else 20, keys) for i in SURVIVE]
    keys["rt_hair_groom_primary_order"] = "0"
    plan.append(stage_phase("restore", 5, keys))
    for p in plan:
        if p['label'].startswith('warm'):
            p.update(wait_log=[r"\[shaders\] cache: prefetched ", r"gi: gi/[^ ]+ generation ", r"\[auto\] scene ready"], wait_log_timeout=180)
    return plan


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
    reload = [dict(phase("reload-selected", 5, True), reload_selected=True), phase("cost-after-reload", POST_RELOAD_SECONDS, True)]
    restore = [phase("restore", 5)]
    for p in cost + reload + restore:
        p["set"] += ",pan_prepare=0,pan_edge=0,pan_capture=0,fg_snap=0"
    # hv4: the same reload under GPU-AV, so validation names a bad access across the load boundary.
    vreload = [dict(phase("validation-reload", 5, True), reload_selected=True), phase("validation-after-reload", POST_RELOAD_SECONDS, True)]
    for p in vreload:
        p["set"] += ",pan_prepare=0,pan_edge=0,pan_capture=0,fg_snap=0"
    plan = combine([warm, shots, motion, toggles, temporal, cost, reload, restore] if not validation else
                   [warm, [phase("validation-strands", cost_seconds, True)], vreload, restore])
    return plan


def job(name, save, plan, validation=False):
    j = hp.job(name, save)
    labels = [p["label"] for p in plan]
    j.update(live_batch="hair-validation", timeout=sum(p["seconds"] for p in plan)+660,   # + readiness waits (<= 375 s, live_flow) + 240 s load + margin
             estimated_phase_seconds=sum(p["seconds"] for p in plan), window_mode="visible_unfocused_fg")
    j["args"] = ["-RealPresent", "-Move", "-MovePlan", f"captures/live_batches/hair_validation/plans/{name}.json"]
    if validation:
        j["args"] += ["-Validation", "-VSet", "validate_sync=true,gpuav_enable=true"]
    j["set"] = j["set"].replace("dump_png_width=0", "dump_png_width=960")
    # Keep actual Ultra qualified keys: the inherited diagnostic jobs disabled OMM.
    j["set"] = j["set"].replace("rt_omm=0", "rt_omm=1")
    j["expect"] = [r"\[auto\] scene ready", r"\[cfg\] settings read"]   # strand draws: per-window, once swapped
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
                      match=hp.STRANDS, min=1, period_frames=PRIMARY_LOG_FRAMES, required_if=hp.WHEN_SWAPPED,
                      windows=["validation-strands", "validation-after-reload"] if validation else ["cost-strands", "cost-after-reload"]))
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
    j["expect_runner"].append(r"\[hair-reload\] selected save reload complete")
    j["purpose"] = "Hair validation: same-run cards/strands, held Q/E enter/leave view, zoom LOD, repeated toggles, diagnostic-free Ultra costs. No quality pass without analyzer evidence."
    return j


def bisect_job(name, save, plan):
    j = job(name, save, plan, validation=False)
    j["timeout"] = 850
    labels = [p["label"] for p in plan]
    survive = [l for l in labels if l.startswith("survive-")]
    # Frames after every reload: the per-60-frame scene-wait line in every survival window (none: the device or the
    # game is gone in that stage). The level-load line of hair_primary once a strand stage reloads.
    j["expect"] = [r"\[auto\] scene ready", r"\[cfg\] settings read",
                   r"\[hair primary\] level load \(first or new G-buffer images\): strand swaps paused for \d+ frames"]
    j["expect_every"] = [dict(name="frames rendered in every survival window after a reload", select=r"\[rt\] scene wait: 60 frames",
                              match=r"\[rt\] scene wait: 60 frames", event=True, min=1, windows=survive)]
    j["expect_every"].append(dict(name="hair guards zero after every reload", select=r"\[alpha hair\] native coverage records",
                                  match=r"invalid/missing native state 0;.*retired bounds-invalid primitives 0;", min=1,
                                  windows=[l for l in survive if l.endswith("-3")]))
    # A strand stage is only proven with the strand shaders running (hv5 2026-11-03: every draw struck, 0 strand draws, yet
    # "stable"): the coverage line with probed draws in its survival windows after the 300-frame swap pause (the strand
    # G-buffer variant counts strand samples there, writes off); G-buffer and velocity strand draws once draws swapped.
    strand_windows = [l for l in survive if any(l.startswith("survive-" + s) for s in STRAND_STAGES) and not l.endswith("-1")]
    j["expect_every"].append(dict(name="strand shaders run after every strand-stage reload (draws probed)",
                                  select=hp.COVERAGE, match=r"coverage \(last 300 frames\): [1-9]\d* draws probed",
                                  min=1, period_frames=PRIMARY_LOG_FRAMES, windows=strand_windows))
    j["expect_every"].append(dict(name="strands drawn after every strand-stage reload when draws swapped",
                                  select=r"\[hair primary\] frame \d+:", match=hp.STRANDS, min=1,
                                  period_frames=PRIMARY_LOG_FRAMES, required_if=hp.WHEN_SWAPPED, windows=strand_windows))
    j["expect_runner"] = [r"window-mode.*visible_unfocused", rf"\[motion-plan\] complete phases={len(plan)}\b", "^Run ended: done",
                          r"\[hair-reload\] selected save reload complete"]
    j["purpose"] = ("Reload bisect (hv1 device lost after F8): four F8 reloads in one launch with rt_hair_groom_primary "
                    "latched on: native hair + scalp, then + physical origin, + strand groom (RT and on screen), + the fibre "
                    "pass, each with a 60 s survival window; the strand stages need strand draws. The first stage without "
                    "frames after its reload names the culprit.")
    return j


COVERAGE = r"\[hair primary\] coverage \(last 300 frames\):"
COVERAGE_ITEM = r"[0-9a-fA-F]{4}:[0-9]+/[0-9]+=(?P<value>[0-9.]+),(?P<active>[0-9]+)"
SWAPPED = r"[0-9a-fA-F]{4}:\d+/\d+=[0-9.]+,[1-9]\d*\b"
DIAGNOSTIC_WINDOWS = ["order-prepass-1", "order-gbuffer-1", "order-prepass-2", "order-gbuffer-2", "tips-on"]
CAPTURE_WINDOWS = ["cards-a", "cards-b", "cards-c"] + DIAGNOSTIC_WINDOWS


def make_diagnostic():
    # Same stationary saved Dialogue1 close-up as hair_groom.
    def step(label, order="0", groom="1", tips="0", capture=False):
        keys = dict(hp.ON, rt_hair_groom=groom, rt_hair_groom_primary_order=order, rt_hair_groom_tips=tips)
        p = stage_phase(label, 25, keys, trace=capture and label in ("cards-a", "order-prepass-1", "order-gbuffer-1"))
        if capture:
            serial = 98600 + CAPTURE_WINDOWS.index(label)
            p.update(serial=serial, capture_at=3)
            p["set"] += f",pan_prepare=1,pan_edge=1,pan_capture={serial},pan_delay_ms=0"
        return p
    plan = [step("warmup"), step("warm-order", "1", tips="1"), step("warm-cards", groom="0"),
            step("cards-a", groom="0", capture=True),
            step("order-prepass-1", capture=True), step("order-gbuffer-1", "1", capture=True),
            step("cards-b", groom="0", capture=True),
            step("order-prepass-2", capture=True), step("order-gbuffer-2", "1", capture=True),
            step("tips-on", "1", tips="1", capture=True), step("cards-c", groom="0", capture=True),
            stage_phase("restore", 5, dict(hp.OFF, rt_hair_groom_primary_order="0", rt_hair_groom_tips="0"))]

    for p in plan:
        if p['label'].startswith('warm'):
            p['set'] += ',qa_exposure_hold=0,pan_delay_ms=0'
            p.update(wait_log=[r"\[shaders\] cache: prefetched ", r"gi: gi/[^ ]+ generation ", r"\[auto\] scene ready"], wait_log_timeout=180)
    return plan


def diagnostic_job(name, save, plan):
    from scripts.queue_gates import SCENE_WAIT_GATE
    j = job(name, save, plan)
    j["timeout"] = 800
    spec = importlib.util.spec_from_file_location("hair_crowd_jobs", HERE.parent / "hair_crowd/make_job.py")
    crowd = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(crowd)
    j["live_flow"] = dict(crowd.job("hc1-dialogue1-fg2", save)["live_flow"])
    j["set"] += ",rt_hair_groom=1,rt_hair_groom_primary=1,rt_hair_groom_primary_order=0,rt_hair_groom_tips=0"
    j["expect"] = [r"\[auto\] scene ready", r"\[cfg\] settings read",
                   r"\[hair primary\] level load \(first or new G-buffer images\)",
                   r"\[hair primary\] header at 0x[0-9a-f]+", r"\[rrpost\] exposure held"]
    j["fail_fast"] = True
    j["expect_every"] = [dict(name="first load quiet period", select=r"\[hair primary\] frame \d+:",
                              match=r"\b0 G-buffer and 0 velocity strand draws", first_only=True, min=1)]
    j["forbid"] += [r"(?s)\[hair primary\] frame \d+:[^\n]*[1-9]\d* G-buffer[^\n]*\n.*?\[hair primary\] level load \(first or new G-buffer images\)"]
    j["forbid"] += [r"(?s)\[hair primary\] level load \(first or new G-buffer images\)(?:(?!\[hair primary\] frame).)*\[hair primary\] frame \d+:[^\n]*\b[1-9]\d* G-buffer"]
    j["expect_every"] += [dict(name="coverage: no below-bound draw swapped", select=COVERAGE, match=COVERAGE,
                               items=dict(pattern=COVERAGE_ITEM, bound=r", bound ([0-9.]+),", empty=r"\b0 draws probed, 0 swapped;"),
                               min=1, period_frames=300, windows=DIAGNOSTIC_WINDOWS),
                            dict(name="strand GPU cost only when coverage reports swapped frames",
                                 select=r"\[hair primary\] strand G-buffer draws GPU",
                                 match=r"GPU [0-9.]+ ms/frame \(average of [1-9]\d* frames",
                                 required_if=dict(select=COVERAGE, match=SWAPPED),
                                 min=1, period_frames=300, windows=DIAGNOSTIC_WINDOWS),
                            dict(name="order 1 has no adjacent-tag frames", select=r"\[hair primary\] counted frame \d+:",
                                 match=r"counted frames with adjacent-tag pixels 0 \(session \d+\), header order G-buffer",
                                 min=1, period_frames=300, windows=["order-gbuffer-1", "order-gbuffer-2", "tips-on"])]
    for label in CAPTURE_WINDOWS:
        serial = 98600 + CAPTURE_WINDOWS.index(label)
        gate = dict(name="consecutive rendered hair frames " + label,
                    select=rf"\[pan\] burst {serial} complete",
                    match=rf"\[pan\] burst {serial} complete: 8 frames, 20 samples, transport ok",
                    event=True, min=1, windows=[label])
        # Strand windows: the burst samples strand pixels, which exist only once draws swapped (coverage evidence first;
        # hv6 2026-11-04 failed fast in order-prepass-1 on the same cause as its 0 strand samples).
        if not label.startswith("cards"):
            gate["required_if"] = dict(select=COVERAGE, match=SWAPPED)
        j["expect_every"].append(gate)
    j["expect_every"].append(dict(name="strand participation after quiet window when swaps happened",
        select=r"\[hair primary\] frame \d+:", match=hp.STRANDS,
        required_if=dict(select=COVERAGE, match=SWAPPED), min=1, period_frames=300, windows=DIAGNOSTIC_WINDOWS))
    j["expect_every"].append(dict(SCENE_WAIT_GATE, windows=CAPTURE_WINDOWS))
    j["expect_runner"] = [r"window-mode.*visible_unfocused", rf"\[motion-plan\] complete phases={len(plan)}\b", "^Run ended: done"]
    j["expect_runner"] += [rf"\[motion-trace\] phase={i} label={p['label']} repeat=0" for i,p in enumerate(plan) if p.get("trace_at")]
    j["purpose"] = "First-load safety, order 0/1/0/1, per-draw card fallback, tips 0/1 and same-run player-head flicker. No reload."
    return j


def main():
    assert (HERE / "predicted.json").exists()
    for folder in ("jobs", "plans"):
        (HERE/folder).mkdir(exist_ok=True)
    for name, builder, factory in [("hv5-reload-bisect", make_bisect, bisect_job),
                                   ("hv6-strand-diagnostic", make_diagnostic, diagnostic_job)]:
        plan = builder()
        for folder, value in (("plans", plan), ("jobs", factory(name, "Test Dialogue1", plan))):
            (HERE/folder/f"{name}.json").write_text(json.dumps(value, indent=2)+"\n", encoding="utf-8")
    print("hv5 and hv6 generated; historical hv1-hv4 retained; no queue staging")


if __name__ == "__main__":
    main()
    from tools.live_batches.live_flow import tighten_files
    tighten_files(HERE, job_names=["hv5-reload-bisect", "hv6-strand-diagnostic"])
