"""Writes tools/live_batches/rtx_hair_live/{plans,jobs}/*.json and predicted.json. Generate only: never stages or launches.

RTX Hair live switch (docs/RTX_HAIR_PRIMARY.md 5m, common/hair_live.h): one launch of Test Dialogue1 that starts with
cards only (RTX Hair off, physical hair ray origins reserved at startup), turns the RTX Hair bundle on live (the
session's first strand activation, which used to need a restart) and then toggles the live key rt_hair 1/0/1/0 by
setting, ending with an explicit cards-only window. Static camera, exposure held, one scene-colour dump per window.
Observables: strand draws present in every on window and absent in every off window (the [hair primary] period lines),
the switch edges, no device loss, and the scene-colour energy of the off windows equal to the cards-only windows.
"""
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
import subprocess
from tools.live_batches import pipeline_readiness  # noqa: E402

B = ROOT / "tools" / "live_batches" / "rtx_hair_live"
(B / "plans").mkdir(parents=True, exist_ok=True)
(B / "jobs").mkdir(parents=True, exist_ok=True)

# The RTX Hair On bundle (common/settings.h exp_hair_groom_primary choice "on"), and its cards-only values.
BUNDLE = ["rt_hair_groom_primary", "rt_hair_groom_primary_order", "rt_hair_groom_primary_current", "rt_hair_primary_frame_tag",
          "rt_hair_groom_lod_keep", "rt_hair_primary_hold", "rt_hair_tangent_world", "rt_hair_groom", "rt_hair_groom_shadow_t",
          "rt_hair_groom_ao_t", "rt_hair_groom_cascade_drop", "rt_hair_fibre", "rt_hair_fibre_gi", "rt_hair_fibre_spec",
          "rt_hair_fibre_mapped", "rr_hair_guides", "rt_hair_physical_origin", "rt_hair_capture"]
ON = ",".join(f"{k}=1" for k in BUNDLE)
OFF = ",".join(f"{k}=0" for k in BUNDLE)
PERIOD = 60    # rt_stats_period_frames: the [hair primary] and scene-wait period lines (layer/hair_primary.cpp stats_due)
FRAMES = 3 * PERIOD   # two complete period lines after the settings acknowledgement and GPU readback latency
QUIET = "\\[watch\\].*\\| pipelines 0 \\(0 ms in driver\\)"


def phase(label, seconds, keys, trace=True, wait=None, fresh=None, timeout=20, frames=None):
    p = {"label": label, "kind": "static", "speed": "static", "seconds": seconds, "repeat_ms": 0, "scan": 16,
         "capture_at": 0, "serial": 0, "motion_start": seconds, "motion_end": seconds, "pixels_per_second": 0,
         "direction": 0, "set": keys + ",rt_stats=0,qa_exposure_hold=1"}
    if trace:
        p["trace_at"] = [seconds - 3]
    if wait:
        p["wait_log"] = wait
        p["wait_log_timeout"] = timeout
    if fresh:
        p["wait_log_fresh"] = fresh
    if frames:
        p.update(frames=frames, settle_frames=0, stats_reset=True)
    return p


ON_EDGE = "\\[hair primary\\] live: strands ON at frame \\d+ \\(switch {n}\\)"
OFF_EDGE = "\\[hair primary\\] live: strands OFF at frame \\d+ \\(switch {n}\\)"
# Readiness waits on pipeline quiescence only (pipeline_readiness adds it for the changed keys). The switch edges are
# job expectations, never readiness patterns: rhl1 2026-11-09 went INVALID because a second settings read after the
# edge moved camera-plan's fresh-observation start past it (scripts/camera-plan.ps1 takes the last acknowledging
# [cfg] keys record), although the edge was logged 3 ms after the first one.
W = 12   # seconds per measured window (FRAMES at the lint's unfocused FPS model plus the trace)
plan = [
    phase("warmup", 20, f"rt_hair=1,{OFF}", trace=False, wait=["\\[auto\\] scene ready", "\\[cfg\\] settings read", QUIET],
          fresh=[QUIET], timeout=180),
    # Every value once before the measured windows (strand variants, fibre and guide pipelines resident). This is the
    # session's first strand activation: the game started cards only.
    phase("warmup-on", 10, f"rt_hair=1,{ON}", trace=False, wait=[QUIET], fresh=[QUIET],
          timeout=90),
    phase("warmup-off", 6, f"rt_hair=0,{ON}", trace=False),
    phase("base-a", W, f"rt_hair=1,{OFF}", frames=FRAMES),                                   # cards only
    phase("on-a", W, f"rt_hair=1,{ON}", frames=FRAMES),
    phase("off-a", W, f"rt_hair=0,{ON}", frames=FRAMES),
    phase("on-b", W, f"rt_hair=1,{ON}", frames=FRAMES),
    phase("off-b", W, f"rt_hair=0,{ON}", frames=FRAMES),
    phase("base-b", W, f"rt_hair=1,{OFF}", frames=FRAMES),                                   # cards only, explicit keys
    phase("restore", 2, f"rt_hair=1,{OFF}", trace=False),
]
ON_W, OFF_W, BASE_W = ["on-a", "on-b"], ["off-a", "off-b"], ["base-a", "base-b"]
SET = ("preset=ultra,fg=2,headless=0,crash_dump=1,detailed_logging=1,rt_stats=0,rt_debug=0,gi_debug=0,ls_debug=0,rf_debug=0,"
       "ls_stats=0,rf_stats=0,ls_check=0,dump_images=1,dump_png_width=1280,dump_raw=0,dump_shaders=0,pan_capture=0,pan_inputs=0,"
       "pan_prepare=0,rt_primary_check=0,replay_pack=0,qa_exposure_hold=0,"
       f"rt_stats_period_frames={PERIOD},rt_timing_frames={PERIOD},rt_scene_wait_log_frames={PERIOD},"
       # Startup: Hair shadows on (rt_native_hair, a restart key) and the physical-origin reservation (the one startup
       # part of RTX Hair, 5m); every other RTX Hair key off: the session starts cards only.
       "rt_native_hair=1,rt_native_scalp=1,rt_bvh_split=1,sharc_device=1,rt_hair=1," + OFF.replace("rt_hair_physical_origin=0", "rt_hair_physical_origin=1"))
name = "rhl1-dialogue"
plan_rel = "tools/live_batches/rtx_hair_live/plans/dialogue.json"
job = {
    "name": name, "save": "Test Dialogue1", "session": False, "traces": 0, "repeat": 1, "hold": 120, "timeout": 600,
    "max_minutes": 10, "window_mode": "visible_unfocused_fg", "live_batch": "rtx-hair-live",
    "tree": ".claude/worktrees/test-main", "fail_fast": True, "stop_on_dump": False, "set": SET,
    "args": ["-RealPresent", "-Move", "-MovePlan", plan_rel],
    "patterns": ["\\[auto\\]", "\\[cfg\\]", "\\[hot\\]", "\\[watch\\]", "\\[trace\\]", "\\[crash\\]", "\\[fault\\]", "FAILED",
                 "\\[hair primary\\]", "\\[hair groom\\]", "\\[hair\\]", "\\[hair origin\\]", "\\[hair capture\\]",
                 "\\[rt\\] scene wait", "\\[rrpost\\] exposure (held|hold|probe)", "rt passes: GPU cost", "gi: GPU cost",
                 "VK_ERROR", "validation"],
    "expect": [
        "\\[auto\\] scene ready",
        "\\[hot\\] core generation \\d+ loaded",
        "\\[hair groom\\] device with linear swept spheres",
        "\\[rt\\] \\+ occlusionQueryPrecise \\(strand hair coverage gate\\)",
        "\\[hair primary\\] header at 0x[0-9a-f]+: static hair pipelines get strand variants",
        "\\[hair primary\\] strand variants: \\d+ pipelines in [0-9.]+ ms of pipeline creation so far",
        "\\[core\\] \\[hair origin\\] startup auxiliary capability 1",
        ON_EDGE.format(n=1), OFF_EDGE.format(n=1), ON_EDGE.format(n=2), OFF_EDGE.format(n=2),
        ON_EDGE.format(n=3), OFF_EDGE.format(n=3),
        "\\[hair\\] rt_hair=0 from the settings: RTX Hair OFF",
        "\\[hair primary\\] strand G-buffer draws GPU [0-9.]+ ms/frame",
        "\\[trace\\] frame \\d+ written",
        "\\[rrpost\\] exposure held \\(qa_exposure_hold=1\\) at EV [-0-9.]+",
    ],
    "forbid": [
        "\\[crash\\] exception 0x", "VK_ERROR_DEVICE_LOST", "\\[hot\\] core (rejected|faulted)",
        "FAILED, keeping previous", "compile call failed",
        "\\[hair primary\\] UNAVAILABLE", "\\[hair primary\\] strand variants not prepared",
        "\\[hair primary\\] live: RTX Hair requested, but", "\\[hair primary\\] .* [1-9]\\d* pipelines refused",
        "\\[hair groom\\] device WITHOUT", "\\[hair origin\\] enable rejected",
        # A G-buffer draw that lost its card depth with no fallback (the off switch's in-flight frame, review item 19).
        "\\[hair primary\\] depth orphans \\(last \\d+ frames, first 4; [^)]*\\): [0-9a-f]{4}:",
    ],
    "expect_every": [
        {"name": "strands present: every on window's period line has strand G-buffer draws",
         "select": "\\[hair primary\\] frame \\d+: \\d+ grooms published;", "match": ", [1-9]\\d* G-buffer and \\d+ velocity strand draws",
         "windows": ON_W, "min": 1, "period_frames": PERIOD},
        {"name": "strands absent: every off and cards-only window's period line has none",
         "select": "\\[hair primary\\] frame \\d+: RTX Hair off \\(live\\):", "match": ": 0 grooms published; last \\d+ frames: 0 prepass, 0 G-buffer",
         "windows": OFF_W + BASE_W, "min": 1, "period_frames": PERIOD},
        {"name": "scene wait: every measured window traces its own frame's scene (0 timeouts)",
         "select": "\\[rt\\] scene wait: \\d+ frames", "match": "got this frame's scene, 0 timeouts",
         "windows": BASE_W + ON_W + OFF_W, "min": 1, "period_frames": PERIOD},
    ],
    "expect_runner": [
        "\\[window-mode\\].*visible_unfocused",
        f"\\[motion-plan\\] complete phases={len(plan)}\\b",
        "^Run ended: done",
    ],
    "live_flow": {"expected_fps": 30, "settings_latency_s": 1, "trace_stall_s": 3},
    "purpose": ("RTX Hair live switch (5m): Test Dialogue1 starts cards only, RTX Hair turned on live (first activation), then "
                "rt_hair 1/0/1/0 by setting and an explicit cards-only window. Observables: strand G-buffer draws > 0 in on "
                "windows and 0 in off windows ([hair primary] period lines), six switch edges (warmup on/off, then 1/0/1/0), no device loss or depth "
                "orphan, and the pre-post scene-colour energy of the off windows equal to the cards-only windows "
                "(dumps.json, analysed after the run)."),
}
pipeline_readiness.apply(job, plan)
job["estimated_phase_seconds"] = sum(p["seconds"] for p in plan)
(ROOT / plan_rel).write_text(json.dumps(plan, indent=1) + "\n", encoding="utf-8", newline="\n")
(B / "jobs" / f"{name}.json").write_text(json.dumps(job, indent=1) + "\n", encoding="utf-8", newline="\n")
pred = {
    "_rule": "Written and committed BEFORE any run. energy: float mean of the pre-post scene colour (dumps.json) per window's shot.",
    "status": "READY_FOR_LIVE",
    name: {
        "strands": {"predicted": "on-a/on-b: G-buffer strand draws > 0 in every period line; off-a/off-b/base-b: 0",
                    "source": "layer/hair_primary.cpp live_tick + tests/hair_live_test.cpp", "written": "2026-11-09"},
        "edges": {"predicted": "ON/OFF switch 1 (warmup: the session's first activation), 2 and 3, each within one frame of its setting",
                  "source": "common/hair_live.h; settings change at a present (layer/util.cpp cfg_poll)", "written": "2026-11-09"},
        "energy_off_vs_cards": {"predicted": 0.0, "tolerance": "within the base-a vs base-b difference (static camera, exposure held)",
                                "source": "rt_hair=0 makes every RTX Hair key read 0: the same settings as the cards-only windows",
                                "written": "2026-11-09"},
        "first_swap": {"predicted": "warmup-on: strand G-buffer draws within about 2 s of switch 1, as switch 2 in rhl1 01:07 "
                                    "(was 12 s: 3.6 s render-thread groom indexing plus a 300-frame first-image load pause)",
                       "source": "rhl1-dialogue.1.010732.log; hair_groom startup index thread, LoadGuard tracked while off",
                       "written": "2026-11-09"},
        "device_loss": {"predicted": "none (no VK_ERROR_DEVICE_LOST, no [fault] line, no depth orphan)", "written": "2026-11-09"},
        "startup_cost": {"predicted": "'strand variants' line: a few hundred pipelines in about 100 ms (hcap3 2026-11-08: the first "
                                      "batch of 6+ G-buffer variants took 96 ms after the header)", "written": "2026-11-09"},
    },
}
(B / "predicted.json").write_text(json.dumps(pred, indent=1) + "\n", encoding="utf-8", newline="\n")
print("written", job["estimated_phase_seconds"], "s of phases")
# The lint's own regeneration path (frame windows, readiness waits, timeout) against the VALID corpus, then the lint.
lf = str(ROOT / "tools" / "live_batches" / "live_flow.py")
subprocess.run([sys.executable, lf, str(B), "--retime", "--batches", "rtx_hair_live"], check=True, cwd=ROOT)
subprocess.run([sys.executable, lf, str(B)], check=True, cwd=ROOT)
