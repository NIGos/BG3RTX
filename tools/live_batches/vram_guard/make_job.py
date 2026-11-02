"""Writes the vram_guard job and plan (README.md). Rerun after editing the plan or the gates:

    py -3 tools/live_batches/vram_guard/make_job.py
"""
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
NAME = "vg2-s8-low-long-view-fg2"
PLAN = "vg2_s8.json"
# vg1 (2026-11-02 20:48) peaked at 18.6 of 31.4 GB: a 4-minute launch does not reach the user's 31.7 GB. The guard
# gets the budget of a 16 GB card (95.5% of the heap, as 31419 of 32187 MB) instead: the game's own share was
# 10.3-11.3 GB in vg1, ours 3.0-7.6 GB, so ours must stay below about 4 GB: the caps and refusals decide.
TEST_BUDGET_MB = 15640
WHEEL = 10   # wheel-up notches: the game zooms in and lowers the camera to the long view (the user's 20:04 move)
BASE = "rt_debug=0,rt_stats=0"


def step(label, kind, seconds, guard, hold, direction=0, pps=0, trace=None, wheel=0):
    moving = kind != "static"
    s = {"label": label, "kind": kind, "speed": "slow" if moving else "static", "seconds": seconds, "repeat_ms": 0,
         "scan": 16, "capture_at": 0, "serial": 0, "motion_start": 1 if moving else seconds,
         "motion_end": seconds - 1 if moving else seconds, "pixels_per_second": pps, "direction": direction,
         "set": f"rt_vram_guard={guard},rt_vram_compact={guard},rt_capture_elastic={guard},{BASE},qa_exposure_hold={hold}"}
    if trace is not None:
        s["trace_at"] = [trace]
    if wheel:
        s["wheel"] = wheel
    return s


plan = [
    step("warmup-on", "static", 20, 1, 0),                      # load settles, caches fill, guard samples
    step("lower-on", "static", 8, 1, 0, wheel=WHEEL),           # low camera: the long view of 20:04
    step("view-on-a", "static", 20, 1, 0),                      # the long view, guard on
    step("turn-on-r", "rotate", 20, 1, 0, 1, 60),               # the turn that triggered it, guard on
    step("turn-on-l", "rotate", 20, 1, 0, -1, 60),
    step("settle-off", "static", 10, 0, 1),                     # static, guard off; the exposure latches here
    step("shot-off-a", "static", 6, 0, 1, trace=4),             # energy: static OFF / ON / OFF, exposure held
    step("shot-on", "static", 6, 1, 1, trace=4),
    step("shot-off-b", "static", 6, 0, 1, trace=4),
    step("turn-off-r", "rotate", 20, 0, 1, 1, 60),              # the same turn, guard off (report: the old behaviour)
    step("turn-off-l", "rotate", 20, 0, 1, -1, 60),
    step("recover-settle-on", "static", 25, 1, 1),             # 254-frame pool watchdog + GPU retirement
    step("recover-on", "static", 20, 1, 1),                     # guard back on after the off turn: back within budget
    step("restore", "static", 5, 0, 0),
]
ON = ["view-on-a", "turn-on-r", "turn-on-l", "recover-on"]
# vg1: at the unfocused 30 fps a 120-frame stats line comes every 4 s; a complete line (its previous one after the
# toggle) needs a window of 8 s or more: the 6 s shots and 8 s lower-on have none. Gates use the 20 s windows only.
OFF = ["turn-off-r", "turn-off-l"]
ALL = [s["label"] for s in plan if s["seconds"] >= 20 and s["label"] != "warmup-on"]

expect_every = [
    # The decisive observable: the window breakdown line (layer.cpp log_vram_ours, every rt_timing window) says
    # within budget after reserves shrink. Recovery has a separate 25 s settling phase; no over-budget
    # line is excused inside the measured recover-on window.
    {"name": "VRAM: process usage within the driver's budget in every guard-on window",
     "select": r"\[mem\] ours \(window\):", "match": r"\(within budget\); guard (normal|LOW)", "min": 1, "windows": ON},
    {"name": "pacing: no Streamline 'Frame rate over 100 ms' in a guard-on window",
     "select": r"\[sl\] warn .*Frame rate over 100\.00ms", "match": r"(?!)", "min": 0, "event": True, "windows": ON},
    {"name": "pacing: no Streamline fence wait timeout in a guard-on window",
     "select": r"\[sl\] warn .*waitCPUFence.*timed out", "match": r"(?!)", "min": 0, "event": True, "windows": ON},
    {"name": "scene wait: every window traces its own frame's scene (0 timeouts)",
     "select": r"\[rt\] scene wait: \d+ frames", "match": r"got this frame's scene, 0 timeouts", "min": 1, "windows": ALL},
]
# Require the new accounting in every measured ON window. A pending copy is counted,
# and recovery must have retired it; a target announcement alone is not a release.
expect_every += [
    {"name": "mesh reserve accounting in every ON window",
     "select": r"\[mem\] ours \(window\):",
     "match": r"mesh pool \d+ MB \(\d+ used, \d+ copy/retirement, \d+ repacks; \d+ cached BLAS relocated\)",
     "min": 1, "windows": ON},
    {"name": "BLAS capacity accounting in every ON window",
     "select": r"\[bvh\] mesh cache:",
     "match": r"chunks reserved/live/pending [0-9.]+/[0-9.]+/[0-9.]+ MB, freed \d+ \([0-9.]+ MB\), relocated \d+ \([0-9.]+ MB\); pool repacks \d+ \(-[0-9.]+ MB\)",
     "min": 1, "windows": ON},
    {"name": "recovery has a published repack and no pool copy/retirement overlap",
     "select": r"\[mem\] ours \(window\):",
     "match": r"0 copy/retirement, [1-9]\d* repacks;",
     "min": 3, "windows": ["recover-on"]},
]
forbid = [
    r"\[crash\] exception 0x", "VK_ERROR_DEVICE_LOST", r"\[hot\] core (rejected|faulted)", "FAILED, keeping previous",
    "compile call failed", "RR creation failed", "RR evaluate failed", r"\[cascade\] could not allocate",
    r"set 1 [1-9]\d* out-of-range reads skipped", r"\[log\] reached \d+ MB", r"\[sl\] .*eWarnOutOfVRAM",
]
plan_s = sum(s["seconds"] for s in plan)
shots = [(i, s["label"]) for i, s in enumerate(plan) if "trace_at" in s]
job = {
    "name": NAME,
    "save": "Water Queens House",
    "session": False, "traces": 0, "hold": 120, "timeout": int(plan_s + 330), "repeat": 1,
    "live_batch": "vram-guard",
    # The user's custom Ultra of 2026-11-02 20:04: Ultra (gi_far on) + hidden-surface shading "Reflections",
    # cutout spill on, box lights All. rt_vram_guard toggles per phase (the plan), off at start.
    "set": ("preset=ultra,fg=2,headless=0,fps_cap=0,rt=1,rr=1,reflex=1,crash_dump=1,rt_timing_frames=120,"
            "rt_scene_wait_log_frames=120,rt_scene_wait=1,rt_debug=0,ls_debug=0,gi_debug=0,rf_debug=0,rr_show=0,rf_stats=0,"
            "ls_stats=0,ls_check=0,dump_images=1,dump_png_width=1280,dump_raw=0,dump_shaders=0,pan_capture=0,pan_inputs=0,"
            "pan_prepare=0,rt_order_trace=0,rt_stability_log=0,rt_stats=0,rt_primary_check=0,rt_primary_epoch=0,"
            "fg_state_every=60,rt_hit_material=1,rt_hit_material_instanced=1,gi_hit_direct=1,gi_hit_sun=1,rf_hit_shade=1,"
            "rf_hit_gi=1,rt_alpha_spill=1,ls_box=2,gi_far=1,ls_k=0,rt_hit_reference=0,sharc=0,rt_vram_guard=0,rt_vram_compact=0,rt_capture_elastic=0,"
            f"qa_exposure_hold=0,rt_vram_guard_budget_mb={TEST_BUDGET_MB}"),
    "args": ["-Move", "-MovePlan", f"captures/live_batches/vram_guard/plans/{PLAN}"],
    "patterns": [r"\[hot\]", r"\[ngx\] RR create", r"\[sl\] DLSS-G frame", r"\[sl\] warn", r"\[pace\]", r"\[mem\]",
                 r"\[vram\]", r"\[watch\]", r"rt passes: GPU cost", "gi: GPU cost", r"\[rt\] scene wait", r"\[geo\] RT capture",
                 r"\[cascade\]", r"\[bvh\] mesh (cache|pool)", r"\[trace\]", r"\[crash\]", "FAILED", r"\[log\]",
                 r"\[rrpost\] exposure (held|hold|probe)"],
    "expect": [
        r"\[auto\] scene ready", r"\[hot\] core generation \d+ loaded", r"\[ngx\] RR create .*: Success",
        r"\[sl\] DLSS-G frame \d+: status 0x0, presented per app frame 2", r"\[cfg\] settings read",
        r"\[vram\] guard on \(rt_vram_guard=1\)", r"\[vram\] guard off \(rt_vram_guard=0\)",
        r"\[mem\] ours \(window\): BLAS chunks \d+ MB reserved in \d+ chunks",
        r"\[mem\] device-local VRAM of this process: used [1-9]\d* MB, budget [1-9]\d* MB",
        r"\[bvh\] mesh cache: \d+ meshes", r"\[trace\] frame \d+ written",
        r"\[bvh\] mesh pool capacity: repacked [0-9.]+ / [0-9.]+ -> [0-9.]+ MB, epoch \d+; old pool waits for scene retirement",
        r"\[cascade\] capture capacity demand [0-9.]+ MB, target \d+ MB of \d+ MB \(elastic, \d+/24 low samples; waiting for retired slot\)",
        r"\[cascade\] capture capacity \d+ -> \d+ MB \(elastic, retired slot\)",
        r"\[geo\] RT capture capacity demand opaque [0-9.]+ MB alpha [0-9.]+ MB, target \d+ MB of \d+ MB \(elastic, \d+/24 low samples; waiting for retired slot\)",
        r"\[geo\] RT capture capacity \d+ -> \d+ MB \(elastic, retired slot\)",
        r"\[rrpost\] exposure held \(qa_exposure_hold=1\) at EV [-0-9.]+",
    ],
    "expect_every": expect_every,
    "forbid": forbid,
    "expect_runner": [r"\[window-mode\] visible_unfocused: hwnd=", r"\[motion-plan\] posted pointer centre to owned unfocused HWND", rf"\[wheel-input\] phase=1 notches={WHEEL}\b"]
                     + [rf"\[motion-trace\] phase={i} label={l} repeat=0" for i, l in shots]
                     + [rf"\[motion-plan\] complete phases={len(plan)}\b", "^Run ended: done"],
    "window_mode": "visible_unfocused_fg",
    "stop_on_dump": True,
    "purpose": ("rt_vram_guard at the user's trigger (S8 port, custom Ultra, low camera long view, the turn of "
                "2026-11-02 20:04: 21992 -> 31668 MB of a 30630 MB budget). One launch: guard on through the long view and "
                "the turn, OFF / ON / OFF static shots for energy (exposure held), the same turn guard off (the old "
                "behaviour, report), guard/compact/elastic on again (25 s retirement settling, then measured recovery). Gates: [mem] ours within budget in every guard-on window, no "
                "Streamline >100 ms or fence timeout in them, scene wait 0 timeouts; RT capture drops reported separately (growth may be retirement-blocked). "
                "Energy: shot-on within the OFF shots' noise (tools/m4_promotion_gate.py energy triples)."),
    "estimated_phase_seconds": plan_s,
}
(HERE / "plans").mkdir(exist_ok=True)
(HERE / "jobs").mkdir(exist_ok=True)
(HERE / "plans" / PLAN).write_text(json.dumps(plan, indent=1) + "\n", encoding="utf-8")
(HERE / "jobs" / f"{NAME}.json").write_text(json.dumps(job, indent=1) + "\n", encoding="utf-8")
print(f"{len(plan)} phases, {plan_s} s ({plan_s / 60:.1f} min), {len(expect_every)} expect_every entries")
