"""Writes tools/live_batches/hair_warmup/{plans,jobs}/*.json and predicted.json. Generate only: never stages or launches.

RTX Hair warm-up (docs/RTX_HAIR_PRIMARY.md 5o, key rt_hair_warmup): one launch of Test Dialogue1 with the RTX Hair bundle
on. The first load runs with rt_hair_warmup=1 (latched in the startup ini), then an F8 reload of the same save with it 0
and a second F8 reload with it 1 again: the A/B is between the two reloads of one run (same save, warm file cache),
the first load is a third sample. Observables (one-shot log lines per load, written by the layer):
  [hair primary] warm-up: level load at frame B: load quiet ended at frame Q (+n, groom state settled|frame count),
      first strand G-buffer swap at frame F, m frames after the load (rt_hair_warmup k)   -> frames from load to strands
  [hair primary] warm-up: last N frames: K draws swapped to strands for the first time ...; frames from their first
      publish: mean, max                                                                 -> frames a published draw waited
  [hair groom] warm-up: last N frames: K groom slots got their first primary key; frames from the first match: mean,
      max                                                                                -> join, upload and build latency
plus strand draws in every window after a load and 0 dropouts there (strands complete, not only present).
Water Queens House is not included: a second launch for the same observable (rule: one launch per save, lean lives).
"""
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.live_batches import pipeline_readiness  # noqa: E402

B = ROOT / "tools" / "live_batches" / "hair_warmup"
(B / "plans").mkdir(parents=True, exist_ok=True)
(B / "jobs").mkdir(parents=True, exist_ok=True)

# The RTX Hair On bundle (common/settings.h exp_hair_groom_primary choice "on").
BUNDLE = ["rt_hair_groom_primary", "rt_hair_groom_primary_order", "rt_hair_groom_primary_current", "rt_hair_primary_frame_tag",
          "rt_hair_groom_lod_keep", "rt_hair_primary_hold", "rt_hair_tangent_world", "rt_hair_groom", "rt_hair_groom_shadow_t",
          "rt_hair_groom_ao_t", "rt_hair_groom_cascade_drop", "rt_hair_fibre", "rt_hair_fibre_gi", "rt_hair_fibre_spec",
          "rt_hair_fibre_mapped", "rr_hair_guides", "rt_hair_physical_origin", "rt_hair_capture"]
ON = ",".join(f"{k}=1" for k in BUNDLE)
PERIOD = 60    # rt_stats_period_frames: the [hair primary]/[hair groom] period lines, warm-up lines included
FRAMES = 3 * PERIOD
QUIET = "\\[watch\\].*\\| pipelines 0 \\(0 ms in driver\\)"


def phase(label, seconds, warm, wait=None, fresh=None, timeout=20, frames=None):
    p = {"label": label, "kind": "static", "speed": "static", "seconds": seconds, "repeat_ms": 0, "scan": 16,
         "capture_at": 0, "serial": 0, "motion_start": seconds, "motion_end": seconds, "pixels_per_second": 0,
         "direction": 0, "set": f"rt_hair=1,{ON},rt_hair_warmup={warm},rt_stats=0,qa_exposure_hold=1"}
    if wait:
        p["wait_log"] = wait
        p["wait_log_timeout"] = timeout
    if fresh:
        p["wait_log_fresh"] = fresh
    if frames:
        p.update(frames=frames, settle_frames=0, stats_reset=True)
    return p


def reload(label, warm):
    # F8 of the sole visible save (scripts/auto-run.ps1 reload_selected); the phase clock pauses during the load and the
    # step completes after two stable 300-present samples (scripts/queue-reload.ps1), so the load's warm-up lines are
    # written before the next window starts.
    return dict(phase(label, 5, warm), reload_selected=True)


W = 12
plan = [
    phase("warmup", 20, 1, wait=["\\[auto\\] scene ready", "\\[cfg\\] settings read", QUIET], fresh=[QUIET], timeout=180),
    # Both key values before the judged windows (live_flow warmup rule). The key only acts inside a load's quiet window,
    # which the first load has closed by now: this toggle changes nothing on screen.
    phase("warm-off", 3, 0),
    phase("load-on", W, 1, frames=FRAMES),        # the first load (key on from the startup ini): strands present
    phase("set-off", 5, 0),                       # the key before the reload (the LoadGuard reads it at every build)
    reload("reload-off", 0),
    phase("after-off", W, 0, frames=FRAMES),
    phase("set-on", 5, 1),
    reload("reload-on", 1),
    phase("after-on", W, 1, frames=FRAMES),
    phase("restore", 2, 0),
]
AFTER = ["load-on", "after-off", "after-on"]
SET = ("preset=ultra,fg=2,headless=0,crash_dump=1,detailed_logging=1,rt_stats=0,rt_debug=0,gi_debug=0,ls_debug=0,rf_debug=0,"
       "ls_stats=0,rf_stats=0,ls_check=0,dump_images=0,dump_raw=0,dump_shaders=0,pan_capture=0,pan_inputs=0,"
       "pan_prepare=0,rt_primary_check=0,replay_pack=0,qa_exposure_hold=0,"
       f"rt_stats_period_frames={PERIOD},rt_timing_frames={PERIOD},rt_scene_wait_log_frames={PERIOD},"
       f"rt_native_hair=1,rt_native_scalp=1,rt_bvh_split=1,sharc_device=1,rt_hair=1,{ON},rt_hair_warmup=1")
LOAD = ("\\[hair primary\\] warm-up: level load at frame \\d+: load quiet ended at frame \\d+ \\(\\+{quiet}, {why}\\), "
        "first strand G-buffer swap at frame \\d+, \\d+ frames after the load \\(rt_hair_warmup {key}\\)")
name = "hw1-dialogue"
plan_rel = "tools/live_batches/hair_warmup/plans/dialogue.json"
job = {
    "name": name, "save": "Test Dialogue1", "session": False, "traces": 0, "repeat": 1, "hold": 120, "timeout": 600,
    "max_minutes": 10, "window_mode": "visible_unfocused_fg", "live_batch": "hair-warmup",
    "tree": ".claude/worktrees/test-main", "fail_fast": True, "stop_on_dump": False, "set": SET,
    "args": ["-RealPresent", "-Move", "-MovePlan", plan_rel],
    "patterns": ["\\[auto\\]", "\\[cfg\\]", "\\[hot\\]", "\\[watch\\]", "\\[crash\\]", "\\[fault\\]", "FAILED",
                 "\\[hair primary\\]", "\\[hair groom\\]", "\\[hair\\]", "\\[hair capture\\]", "\\[rt\\] scene wait",
                 "VK_ERROR", "validation", "DEVICE_LOST"],
    "expect": [
        "\\[auto\\] scene ready",
        "\\[hot\\] core generation \\d+ loaded",
        "\\[hair groom\\] device with linear swept spheres",
        "\\[hair primary\\] header at 0x[0-9a-f]+: static hair pipelines get strand variants",
        # Key on (first load and reload-on): the quiet window ends on the settled signal, then the first swap.
        "\\[hair primary\\] warm-up: load quiet ended at frame \\d+, \\d+ frames after the level load \\(groom state settled "
        "for 30 frames, rt_hair_warmup\\)",
        LOAD.format(quiet="\\d+", why="groom state settled", key=1),
        # Key off (reload-off): the fixed 300-frame window, as before.
        LOAD.format(quiet="300", why="frame count", key=0),
        "\\[hair primary\\] warm-up: last \\d+ frames: [1-9]\\d* draws swapped to strands for the first time since the level "
        "load; frames from their first publish: mean [0-9.]+, max \\d+",
        "\\[hair groom\\] warm-up: last \\d+ frames: [1-9]\\d* groom slots got their first primary key; frames from the first "
        "match: mean [0-9.]+, max \\d+ \\(rt_hair_warmup [01]\\)",
    ],
    "forbid": [
        "\\[crash\\] exception 0x", "VK_ERROR_DEVICE_LOST", "(?i)device (?:was )?lost", "\\[hot\\] core (rejected|faulted)",
        "FAILED, keeping previous", "compile call failed",
        "\\[hair primary\\] UNAVAILABLE", "\\[hair primary\\] strand variants not prepared",
        "\\[hair primary\\] .* [1-9]\\d* pipelines refused", "\\[hair groom\\] device WITHOUT",
        "\\[hair primary\\] depth orphans \\(last \\d+ frames, first 4; [^)]*\\): [0-9a-f]{4}:",
    ],
    "expect_every": [
        {"name": "strands present after every load: every period line has strand G-buffer draws",
         "select": "\\[hair primary\\] frame \\d+: \\d+ grooms published;", "match": ", [1-9]\\d* G-buffer and \\d+ velocity strand draws",
         "windows": AFTER, "min": 1, "period_frames": PERIOD},
        {"name": "strands complete after every load: no dropout (a swapped draw under a tenth of its card pixels)",
         "select": "\\[hair primary\\] dropouts \\(last \\d+ frames\\):", "match": ": 0 swapped G-buffer draws wrote under a tenth",
         "windows": AFTER, "min": 1, "period_frames": PERIOD},
    ],
    "expect_runner": [
        "\\[window-mode\\].*visible_unfocused",
        "\\[hair-reload\\] selected save reload complete",
        f"\\[motion-plan\\] complete phases={len(plan)}\\b",
        "^Run ended: done",
    ],
    "live_flow": {"expected_fps": 30, "settings_latency_s": 1, "trace_stall_s": 3},
    "purpose": ("RTX Hair warm-up (5o, rt_hair_warmup): Test Dialogue1 with RTX Hair on; first load with the key on, an F8 "
                "reload with it off, an F8 reload with it on. Observables: the per-load warm-up line (frames from the level "
                "load to the quiet end and to the first strand swap; settled versus frame count), the per-draw wait from "
                "first publish to first swap, the groom first-key wait, strand draws and 0 dropouts in every window after a "
                "load, no device loss."),
}
pipeline_readiness.apply(job, plan)
job["estimated_phase_seconds"] = sum(p["seconds"] for p in plan)
(ROOT / plan_rel).write_text(json.dumps(plan, indent=1) + "\n", encoding="utf-8", newline="\n")
(B / "jobs" / f"{name}.json").write_text(json.dumps(job, indent=1) + "\n", encoding="utf-8", newline="\n")
pred = {
    "_rule": "Written and committed BEFORE any run. Frames are frame_index values of the warm-up lines (one per load).",
    "status": "READY_FOR_LIVE",
    name: {
        "load_to_first_swap_off": {
            "predicted": "reload-off: quiet ended '+300, frame count'; first strand swap 305-330 frames after the load",
            "source": "he1-dialogue1-edge.1.125642.log: load at frame ~1100, grooms uploaded ~+10 frames, first strand "
                      "G-buffer draws in the 1381-1440 window (~+300..+330); sh2-s8-scene-handout.1.184303.log the same",
            "written": "2026-11-09"},
        "load_to_first_swap_on": {
            "predicted": "load-on and reload-on: quiet ended 'groom state settled' at +45..+150 frames; first strand swap "
                         "+55..+170 frames after the load (at least 3x sooner than the off reload of the same run)",
            "source": "first hair draws ~+5..+45 frames after the boundary (slow streaming frames), UV join 1-3 frames, LOD 0 "
                      "upload 2-3, key at the second points update, bounds readback 2-3, measured LOD at once (upload 2-3, "
                      "one full build), then 30 settled frames; the swap gates add ~12 (kFreshFrames) + 2 publishes",
            "written": "2026-11-09"},
        "first_publish_to_swap_per_draw": {
            "predicted": "off: mean 250-300 frames; on: mean <= 120 frames (the warm-up period line of the window after "
                         "each load)",
            "written": "2026-11-09"},
        "groom_first_key_wait": {
            "predicted": "mean <= 10 frames with the key on and off (join, upload, build, second update are unchanged; the "
                         "measured-LOD switch keeps the slot's key, H2)",
            "written": "2026-11-09"},
        "lod_switch": {
            "predicted": "key on: each groom's screen-size LOD upload ('LOD 1/2 uploaded') within ~10 frames of its LOD 0 "
                         "upload; key off: ~30-50 frames later (sh2: 0.9 s)",
            "written": "2026-11-09"},
        "strands_complete": {
            "predicted": "every period line in load-on, after-off and after-on has strand G-buffer draws and 0 dropouts",
            "written": "2026-11-09"},
        "device_loss": {"predicted": "none (no VK_ERROR_DEVICE_LOST, no [fault] line, no depth orphan)", "written": "2026-11-09"},
    },
}
(B / "predicted.json").write_text(json.dumps(pred, indent=1) + "\n", encoding="utf-8", newline="\n")
print("written", job["estimated_phase_seconds"], "s of phases")
lf = str(ROOT / "tools" / "live_batches" / "live_flow.py")
subprocess.run([sys.executable, lf, str(B), "--retime", "--batches", "hair_warmup"], check=True, cwd=ROOT)
subprocess.run([sys.executable, lf, str(B)], check=True, cwd=ROOT)
