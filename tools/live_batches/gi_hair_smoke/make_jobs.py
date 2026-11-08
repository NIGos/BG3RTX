"""Writes tools/live_batches/gi_hair_smoke/{plans,jobs}/*.json (one launch per scene, gi_rt_pipeline=1, gi_rt_pipeline_reorder 0/1/0/1)."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
B = ROOT / "tools" / "live_batches" / "gi_hair_smoke"
(B / "plans").mkdir(parents=True, exist_ok=True)
(B / "jobs").mkdir(parents=True, exist_ok=True)


def phase(label, seconds, key, hold, trace=False, wait=None, fresh=None, timeout=None):
    p = {"label": label, "kind": "static", "speed": "static", "seconds": seconds, "repeat_ms": 0, "scan": 16,
         "capture_at": 0, "serial": 0, "motion_start": seconds, "motion_end": seconds, "pixels_per_second": 0,
         "direction": 0, "set": f"gi_rt_pipeline={key},gi_rt_pipeline_reorder={key},rt_stats=0,qa_exposure_hold={hold}"}
    if trace:
        p["trace_at"] = [4]
    if wait:
        p["wait_log"] = wait
        p["wait_log_timeout"] = timeout or 120
    if fresh:
        p["wait_log_fresh"] = fresh
    return p


PIPELINES_IDLE = "\\[watch\\].*\\| pipelines 0 \\(0 ms in driver\\)"
plan = [
    phase("warmup", 20, 0, 0, wait=["\\[auto\\] scene ready", "\\[cfg\\] settings read", PIPELINES_IDLE],
          fresh=[PIPELINES_IDLE], timeout=180),
    # The reorder pipeline (spec 1050=1) builds in the background the first time the key is on; reorder 0 runs meanwhile.
    phase("warmup-on", 10, 1, 0, wait=["gi: gi_rt_pipeline: the hit-GI trace runs as a ray tracing pipeline, .* \\(reorder 1\\)", PIPELINES_IDLE],
          fresh=[PIPELINES_IDLE], timeout=60),
    phase("cost-off-a", 22, 0, 1), phase("shot-off-a", 6, 0, 1, trace=True),
    phase("cost-on-a", 22, 1, 1), phase("shot-on-a", 6, 1, 1, trace=True),
    phase("cost-off-b", 22, 0, 1), phase("shot-off-b", 6, 0, 1, trace=True),
    phase("cost-on-b", 22, 1, 1), phase("shot-on-b", 6, 1, 1, trace=True),
    phase("restore", 2, 0, 0),
]
COST = ["cost-off-a", "cost-on-a", "cost-off-b", "cost-on-b"]
import sys
sys.path.insert(0, str(ROOT))
from tools.live_batches import pipeline_readiness  # noqa: E402
SET = ("preset=ultra,fg=2,headless=0,crash_dump=1,detailed_logging=1,rt_timing_frames=120,rt_stats=0,rt_debug=0,gi_debug=0,"
       "ls_debug=0,rf_debug=0,ls_stats=0,rf_stats=0,ls_check=0,dump_images=1,dump_png_width=1280,dump_raw=0,dump_shaders=0,"
       "pan_capture=0,pan_inputs=0,pan_prepare=0,rt_primary_check=0,replay_pack=0,sharc=0,qa_exposure_hold=0,"
       "rt_hair_groom_primary=1,rt_hair_groom_primary_order=1,rt_hair_groom_primary_current=1,rt_hair_primary_frame_tag=1,rt_hair_groom_lod_keep=1,rt_hair_primary_hold=1,rt_hair_tangent_world=1,rt_hair_groom=1,rt_hair_groom_shadow_t=1,rt_hair_groom_ao_t=1,rt_hair_groom_cascade_drop=1,rt_hair_fibre=1,rt_hair_fibre_gi=1,rt_hair_fibre_spec=1,rt_hair_fibre_mapped=1,rr_hair_guides=1,rt_hair_physical_origin=1,sharc_device=1,gi_rt_pipeline=0,gi_rt_pipeline_reorder=0,rt_scene_wait_log_frames=120")
SCENES = [("hair1-dialogue", "Test Dialogue1", "dialogue", -3.0)]
for name, save, short, ms in SCENES:
    plan_rel = f"tools/live_batches/gi_hair_smoke/plans/{short}.json"
    pipeline_readiness.apply({"set": SET}, plan)
    for p in plan:   # both pipelines are resident after warmup-on: a toggle only needs a quiet driver
        if p["label"].startswith("cost-") or p["label"] == "restore":
            p["wait_log_timeout"] = 30
    (ROOT / plan_rel).write_text(json.dumps(plan, indent=1) + "\n", encoding="utf-8", newline="\n")
    job = {
        "name": name, "save": save, "session": False, "traces": 0, "repeat": 1, "hold": 120, "timeout": 720,
        "max_minutes": 12, "window_mode": "visible_unfocused_fg", "live_batch": "gi-hair-smoke",
        "tree": ".claude/worktrees/test-main", "fail_fast": True, "stop_on_dump": False, "set": SET,
        "args": ["-RealPresent", "-Move", "-MovePlan", plan_rel],
        "patterns": ["\\[auto\\]", "\\[cfg\\]", "\\[hot\\]", "\\[watch\\]", "\\[trace\\]", "\\[crash\\]", "FAILED",
                     "\\[rt pipeline\\]", "gi: gi_rt_pipeline", "gi: gi/gi_trace_rt_hit_sun", "gi: trace pass",
                     "gi: GPU cost", "rt passes: GPU cost", "\\[rt\\] scene wait", "\\[rrpost\\] exposure (held|hold|probe)",
                     "\\[sl\\] DLSS-G frame", "\\[hair primary\\]", "\\[hair groom\\]", "VK_ERROR", "validation", "\\[geo\\] RT capture", "\\[cascade\\] casters"],
        "expect": [
            "\\[auto\\] scene ready",
            "\\[hot\\] core generation \\d+ loaded",
            "\\[rt pipeline\\] on the device: handle \\d+ B, alignment \\d+, base alignment \\d+, reorder REORDER",
            "gi: gi/gi_trace_rt_hit_sun(_em)?\\.hlsl\\?[^ ]*GI_TRACE_RAYGEN=1 generation \\d+",
            "gi: gi_rt_pipeline: the hit-GI trace runs as a ray tracing pipeline, (emission|sun) variant \\(reorder 1\\)",
            "gi: gi_rt_pipeline: the hit-GI trace runs as compute: key off",
            "gi: trace pass \\(ray-traced, gi_rt=1, \\d+ rays/sample, .*\\) GPU median [0-9.]+ ms",
            "\\[trace\\] frame \\d+ written",
            "\\[hair groom\\] device with linear swept spheres",
            "\\[hair primary\\] strand G-buffer draws GPU",
            "\\[rrpost\\] exposure held \\(qa_exposure_hold=1\\) at EV [-0-9.]+",
            "\\[rrpost\\] exposure hold released after [1-9]\\d* writes",
        ],
        "forbid": [
            "\\[crash\\] exception 0x", "VK_ERROR_DEVICE_LOST", "\\[hot\\] core (rejected|faulted)",
            "FAILED, keeping previous", "compile call failed",
            "\\[rt pipeline\\] .*(unavailable|entry points missing|unusable properties|left alone)",
            "\\[layer\\] optional ray tracing pipeline device request failed",
            "gi: gi_rt_pipeline: the hit-GI trace runs as compute: (no ray tracing pipeline|ray tracing pipeline entry points|this variant)",
            "gi: gi_rt_pipeline: the shader binding table could not be allocated",
            "GI_TRACE_RAYGEN=1 pipeline creation failed",
            "\\[hair primary\\] UNAVAILABLE", "\\[hair primary\\] .* [1-9]\\d* pipelines refused", "\\[hair groom\\] device WITHOUT",
        ],
        "expect_every": [
            {"name": "gpu: the hit-GI trace pass line in every cost window (compute vs RT pipeline + SER, 0/1/0/1)",
             "select": "gi: trace pass \\(ray-traced", "match": "GPU median [0-9.]+ ms", "windows": COST, "min": 1},
            # The 18:19 rerun allowed 1 timeout per 120 frames; the project lint (tests/queue_gates_test.py) requires
            # 0 for every lean job, so the generator is back on 0 (the runs of 2026-11-08 are recorded in progress).
            {"name": "scene wait: every cost window traces its own frame's scene (0 timeouts)",
             "select": "\\[rt\\] scene wait: \\d+ frames", "match": "got this frame's scene, 0 timeouts",
             "windows": COST, "min": 1},
            {"name": "capacity: the RT capture drops no opaque draw (buffer full 0.00) in every cost window",
             "select": "\\[geo\\] RT capture, last \\d+ frames:", "match": "opaque draws dropped \\(buffer full\\) 0\\.00,",
             "windows": COST, "min": 1},
            {"name": "capacity: the cascade casters buffer is never full in every cost window with the casters on",
             "select": "\\[cascade\\] casters: cascade \\d+, \\d+ frames;", "match": ", buffer full 0\\.0, added ",
             "windows": COST, "min": 1, "when_present": True},
        ],
        "expect_runner": [
            "\\[window-mode\\].*visible_unfocused",
            "\\[motion-trace\\] phase=3 label=shot-off-a repeat=0",
            "\\[motion-trace\\] phase=5 label=shot-on-a repeat=0",
            "\\[motion-trace\\] phase=7 label=shot-off-b repeat=0",
            "\\[motion-trace\\] phase=9 label=shot-on-b repeat=0",
            "\\[motion-plan\\] complete phases=11\\b",
            "^Run ended: done",
        ],
        "live_flow": {"expected_fps": 30, "settings_latency_s": 1, "trace_stall_s": 3},
        "estimated_phase_seconds": sum(p["seconds"] for p in plan),
        "purpose": (f"Smoke with RTX Hair strands on (Strand hair on screen bundle): compute vs the shipped RT pipeline + SER (reorder by hit kind) on the "
                    f"tracing pipeline GI trace (far + sun + emission), toggled 0/1/0/1 in one run, static camera, exposure held. "
                    f"Observables: 'gi: trace pass' GPU median per cost window (predicted {ms:+.1f} ms ON vs OFF, offline replay "
                    f"without emission), scene-colour energy unchanged, no device, validation or nvlddmkm error."),
    }
    (B / "jobs" / f"{name}.json").write_text(json.dumps(job, indent=1) + "\n", encoding="utf-8", newline="\n")
pred = {
    "_rule": "Written and committed BEFORE any run. gpu_ms: 'gi: trace pass' GPU median ON (gi_rt_pipeline=1, reorder 1) minus OFF (compute). energy: pre-post scene colour of each ON shot against its OFF neighbours.",
    "hair1-dialogue": {"gpu_ms": {"predicted": -3.0, "source": "camp live: RT pipeline -2.1 ms (grp1) plus reorder -1.5 ms (ser1); dialogue with strands not measured", "written": "2026-11-08"},
                       "energy": {"predicted": 0.0, "source": "dispatch change only; seed gate PASS", "written": "2026-11-08"}},
}
(B / "predicted.json").write_text(json.dumps(pred, indent=1) + "\n", encoding="utf-8", newline="\n")
print("written", sum(p["seconds"] for p in plan), "s per job")
