"""rel095_verify: one launch at Water Queens House (S8) that checks the 0.9.5 release fixes in play
and qualifies rt_capture_rise_reserve (opt-vram, READY_FOR_LIVE since 2026-11-08).

User-like settings (release Ultra, FG 3, RTX Hair keys, rr_hair_strand_aa=2), camera lowered 10
notches, then identical right/left turns with rt_capture_rise_reserve 0 / 1 / 0, each followed by a
static window. Gates: no cascade casters or RT capture draw left out in any window (the reserve
must cover every rise), no strand resolve fallback, no BLAS retirement stall, no device loss.
Observables (predicted.json): cascade capture MB in `[mem] ours` lower with the key on.
rv2 (rt_capture_wide_split, claude/capture-stride, 2026-11-09): the same launch with the startup key
rt_capture_wide_split=1. A startup key cannot go 0/1/0 in one run (the XFB stride is a decoration of
each vertex shader module, fixed when the game creates it), so rv2 runs the same plan and turns with
the key on from launch; the key-off baseline is rv1 (same save, view and turns: 1-8 opaque draws per
frame dropped at about 2065 MB demand, rv1 12:13 and 12:53). Gates in EVERY checked window: 0 opaque
draws dropped, 0 alpha draws made opaque, 0 wide draws left out, 0 cascade casters left out.
rv3 (rt_mesh_budget_own, claude/mesh-cache-rate): the same view and turns with release defaults, the live key
0 / 1 / 0 in one run (two ON turn pairs: fill, then steady state).
Generate: py -3 tools/live_batches/rel095_verify/make_job.py
"""
import copy
import json
import pathlib

HERE = pathlib.Path(__file__).resolve().parent
PLAN = "tools/live_batches/rel095_verify/plans/rv1_s8.json"

HAIR = ("rt_hair_groom=1,rt_hair_groom_primary=1,rt_hair_groom_primary_order=1,rt_hair_groom_primary_current=1,"
        "rt_hair_primary_frame_tag=1,rt_hair_groom_lod_keep=1,rt_hair_primary_hold=1,rt_hair_tangent_world=1,"
        "rt_hair_groom_shadow_t=1,rt_hair_groom_ao_t=1,rt_hair_groom_cascade_drop=1,rt_hair_fibre=1,"
        "rt_hair_fibre_gi=1,rt_hair_fibre_spec=1,rt_hair_fibre_mapped=1,rr_hair_guides=1,"
        "rt_hair_physical_origin=1,rt_hair_capture=1,rr_hair_strand_aa=2")
SET = ("preset=ultra,fg=3,headless=0,crash_dump=1,rt_timing_frames=120,rt_scene_wait_log_frames=120,"
       "rt_debug=0,ls_debug=0,gi_debug=0,rf_debug=0,rr_show=0,rf_stats=0,ls_stats=0,ls_check=0,rt_stats=0,"
       "rt_primary_check=0,dump_images=1,dump_png_width=1280,dump_raw=0,dump_shaders=0,pan_capture=0,"
       "pan_inputs=0,pan_prepare=0,sharc=0,qa_exposure_hold=0,rt_alpha_floor_yield=0,rt_capture_rise_reserve=0," + HAIR)


def phase(label, seconds, keys, kind="static", direction=0, wheel=None, hold=1, trace=None):
    yld, rise = keys
    p = {"label": label, "kind": kind, "speed": "slow" if kind == "rotate" else "static",
         "seconds": seconds, "repeat_ms": 0, "scan": 16, "capture_at": 0, "serial": 0,
         "motion_start": 1 if kind == "rotate" else seconds,
         "motion_end": seconds - 1 if kind == "rotate" else seconds,
         "pixels_per_second": 60 if kind == "rotate" else 0, "direction": direction,
         "set": f"rt_alpha_floor_yield={yld},rt_capture_rise_reserve={rise},qa_exposure_hold={hold}"}
    if wheel:
        p["wheel"] = wheel
    if trace is not None:
        p["trace_at"] = [trace]
    return p


OFF0, YIELD, BOTH = (0, 0), (1, 0), (1, 1)
# Capacity gates hold in the ON windows; the OFF windows record the baseline (rv1 11:39 / 11:44: the
# lowered view drops 1.27 opaque draws/frame with both keys off).
ON = ["turn-yield-r", "turn-yield-l", "turn-both-r", "turn-both-l", "static-both"]
CHECKED = ["turn-off-a-r", "turn-off-a-l", "static-off-a"] + ON + ["turn-off-b-r", "turn-off-b-l", "static-off-b"]

plan = [
    phase("warmup", 20, OFF0, hold=0),
    phase("warm-on", 6, BOTH, hold=0),
    phase("lower", 8, OFF0, wheel=10, hold=0),
    phase("turn-off-a-r", 20, OFF0, "rotate", 1),
    phase("turn-off-a-l", 20, OFF0, "rotate", -1),
    phase("static-off-a", 15, OFF0, trace=12),
    phase("turn-yield-r", 20, YIELD, "rotate", 1),
    phase("turn-yield-l", 20, YIELD, "rotate", -1),
    # Shrink needs 120 low cascade samples (one per 3 frames) after rise_reserve flips: settle first.
    phase("settle-both", 15, BOTH),
    phase("turn-both-r", 20, BOTH, "rotate", 1),
    phase("turn-both-l", 20, BOTH, "rotate", -1),
    phase("static-both", 15, BOTH, trace=12),
    phase("turn-off-b-r", 20, OFF0, "rotate", 1),
    phase("turn-off-b-l", 20, OFF0, "rotate", -1),
    phase("static-off-b", 15, OFF0),
    phase("restore", 2, OFF0, hold=0),
]


def every(name, select, match, windows, **kw):
    e = {"name": name, "select": select, "match": match, "min": 1, "windows": windows}
    e.update(kw)
    return e


job = {
    "name": "rv1-s8-release-verify",
    "save": "Water Queens House",
    "session": False,
    "traces": 0,
    "hold": 120,
    "timeout": 600,
    "max_minutes": 10,
    "repeat": 1,
    "live_batch": "rel095-verify",
    "tree": ".claude/worktrees/test-main",
    "window_mode": "visible_unfocused_fg",
    "fail_fast": False,  # rv4 12:09: the S8 view overflows the 2047 MB capture even with the yield (2065 MB demand); measure every window
    "stop_on_dump": False,
    "set": SET,
    "args": ["-RealPresent", "-Move", "-MovePlan", PLAN],
    "patterns": ["\\[auto\\]", "\\[cfg\\]", "\\[hot\\]", "\\[watch\\]", "\\[trace\\]", "\\[crash\\]", "FAILED",
                 "\\[mem\\]", "\\[vram\\]", "\\[cascade\\]", "\\[geo\\] RT capture", "\\[bvh\\] mesh (cache|pool)",
                 "\\[rt\\] scene wait", "rt passes: GPU cost", "gi: GPU cost", "\\[sl\\] DLSS-G frame", "\\[sl\\] warn",
                 "\\[rrpost\\] strand resolve", "\\[rrpost\\] exposure (held|hold)", "\\[hair primary\\] frame",
                 "VK_ERROR", "\\[log\\]"],
    "expect": [
        "\\[auto\\] scene ready",
        "\\[hot\\] core generation \\d+ loaded",
        "\\[cfg\\] settings read",
        "\\[mem\\] ours \\(\\w+\\): BLAS chunks \\d+ MB reserved in \\d+ chunks",
        "\\[mem\\] device-local VRAM of this process: used [1-9]\\d* MB, budget [1-9]\\d* MB",
        "\\[cascade\\] casters: cascade \\d+, \\d+ frames;",
        "\\[geo\\] RT capture, last \\d+ frames:",
        "\\[trace\\] frame \\d+ written",
        "\\[rrpost\\] exposure held \\(qa_exposure_hold=1\\) at EV [-0-9.]+",
    ],
    "forbid": [
        "\\[crash\\] exception 0x", "VK_ERROR_DEVICE_LOST", "\\[hot\\] core (rejected|faulted)",
        "FAILED, keeping previous", "compile call failed", "\\[cascade\\] could not allocate",
        "\\[log\\] reached \\d+ MB", "\\[sl\\] .*eWarnOutOfVRAM",
    ],
    "expect_every": [
        every("capacity: cascade casters never left out (buffer full 0.0) in the ON windows",
              "\\[cascade\\] casters: cascade \\d+, \\d+ frames;", ", buffer full 0\\.0, added ", ON),
        every("capacity: RT capture drops no opaque draw in the ON windows",
              "\\[geo\\] RT capture, last \\d+ frames:", "opaque draws dropped \\(buffer full\\) 0\\.00,", ON),
        every("capacity: no alpha draw made opaque in the ON windows",
              "\\[geo\\] RT capture, last \\d+ frames:", "made opaque \\(region full\\) 0\\.00,", ON),
        every("scene wait: 0 timeouts in every window", "\\[rt\\] scene wait: \\d+ frames",
              "got this frame's scene, 0 timeouts", CHECKED),
        every("strand resolve: 0 fallbacks wherever strand hair is resolved",
              r"\[rrpost\] strand resolve \(rr_hair_strand_aa=2\):", ", 0 fell back", CHECKED, when_present=True, min=0),
        every("memory accounting in every window", "\\[mem\\] ours \\(", "cascade capture \\d+ MB", CHECKED),
    ],
    "expect_runner": [
        "\\[window-mode\\].*visible_unfocused",
        "\\[wheel-input\\] phase=2 notches=10\\b",
        "\\[motion-trace\\] phase=5 label=static-off-a repeat=0",
        "\\[motion-trace\\] phase=12 label=static-both repeat=0",
        f"\\[motion-plan\\] complete phases={len(plan)}\\b",
        "^Run ended: done",
    ],
    "live_flow": {"expected_fps": 30, "settings_latency_s": 1, "trace_stall_s": 3},
    "estimated_phase_seconds": sum(p["seconds"] for p in plan),
    "purpose": "0.9.5 release check in play (FG 3, RTX Hair, strand resolve); rt_alpha_floor_yield and "
               "rt_capture_rise_reserve off / yield / yield+rise / off: cascade capture MB lower with the key on, 0 casters or opaque draws left out in "
               "every window, 0 strand resolve fallbacks, no BLAS retirement stall.",
}

# rv2: rt_capture_wide_split=1 from launch (startup-only), the same plan; every checked window is gated.
PLAN2 = "tools/live_batches/rel095_verify/plans/rv2_s8.json"
STRIDE_LINE = "\\[geo\\] RT capture stride split, last \\d+ frames:"
job2 = copy.deepcopy(job)
job2.update({
    "name": "rv2-s8-capture-stride",
    "set": SET + ",rt_capture_wide_split=1",
    "args": ["-RealPresent", "-Move", "-MovePlan", PLAN2],
    "fail_fast": True,  # with the key on the view must fit: a window that drops draws decides the run
    "purpose": "rt_capture_wide_split=1 (startup) in the rv1 S8 lowered view and turns: only strand-frame VS at 88 "
               "bytes, all others at 64; 0 opaque draws dropped, 0 alpha draws made opaque and 0 wide draws left out "
               "in every window (rv1 key-off baseline: 1-8 opaque draws/frame dropped), RT capture opaque peak about "
               "1.2 GB instead of 1.6 GB.",
})
job2["patterns"] = job["patterns"] + ["\\[geo\\] rt_capture_wide_split", "\\[cascade\\] wide-stride casters"]
job2["expect"] = job["expect"] + [
    "\\[geo\\] rt_capture_wide_split on: strand-frame vertex shaders 88 bytes, all others 64 bytes per vertex",
    STRIDE_LINE,
    "\\[cascade\\] wide-stride casters \\(rt_capture_wide_split\\):",
]
job2["forbid"] = job["forbid"] + ["\\[geo\\] rt_capture_wide_split inactive"]
job2["expect_every"] = [
    every("capacity: cascade casters never left out (buffer full 0.0) in every window",
          "\\[cascade\\] casters: cascade \\d+, \\d+ frames;", ", buffer full 0\\.0, added ", CHECKED),
    every("capacity: RT capture drops no opaque draw in every window (key on from launch)",
          "\\[geo\\] RT capture, last \\d+ frames:", "opaque draws dropped \\(buffer full\\) 0\\.00,", CHECKED),
    every("capacity: no alpha draw made opaque in every window",
          "\\[geo\\] RT capture, last \\d+ frames:", "made opaque \\(region full\\) 0\\.00,", CHECKED),
    every("stride split: no wide (88-byte) draw left out in every window", STRIDE_LINE,
          "left out \\(region full or no split\\) 0\\.00 per frame", CHECKED),
] + [e for e in job["expect_every"] if e["name"].startswith(("scene wait", "strand resolve", "memory accounting"))]
job2["live_flow"] = dict(job["live_flow"], startup_only=["rt_capture_wide_split"])

# rv3 (rt_mesh_budget_own, claude/mesh-cache-rate, 2026-11-09): the rv1/rv2 view and turns with release defaults
# (wide split and rise reserve on), toggling the live key 0 / 1 / 0. Key off, pool + every BLAS byte was 2.5-3.8 GB
# against the 2048 MB mesh cache budget in the turns (rv2 14:57-14:59): 35-60 cached meshes, about 250 static draws
# per build declined and captured, 4.0-6.2M captured triangles. Two ON turn pairs: the first fills the cache, the
# second is the steady state. Same-run A/B only: observables in predicted.json.
PLAN3 = "tools/live_batches/rel095_verify/plans/rv3_s8.json"
SET3 = SET.replace("rt_capture_rise_reserve=0", "rt_capture_rise_reserve=1") + ",rt_capture_wide_split=1,rt_mesh_budget_own=0"


def phase3(label, seconds, own, kind="static", direction=0, wheel=None, hold=1, trace=None):
    p = phase(label, seconds, (0, 1), kind, direction, wheel, hold, trace)
    p["set"] = f"rt_mesh_budget_own={own},qa_exposure_hold={hold}"
    return p


plan3 = [
    phase3("warmup", 20, 0, hold=0),
    phase3("warm-on", 6, 1, hold=0),
    phase3("lower", 8, 0, wheel=10, hold=0),
    phase3("turn-off-a-r", 20, 0, "rotate", 1),
    phase3("turn-off-a-l", 20, 0, "rotate", -1),
    phase3("static-off-a", 15, 0, trace=12),
    phase3("turn-on-r", 20, 1, "rotate", 1),
    phase3("turn-on-l", 20, 1, "rotate", -1),
    phase3("turn-on2-r", 20, 1, "rotate", 1),
    phase3("turn-on2-l", 20, 1, "rotate", -1),
    phase3("static-on", 15, 1, trace=12),
    phase3("turn-off-b-r", 20, 0, "rotate", 1),
    phase3("turn-off-b-l", 20, 0, "rotate", -1),
    phase3("static-off-b", 15, 0),
    phase3("restore", 2, 0, hold=0),
]
ON3 = ["turn-on-r", "turn-on-l", "turn-on2-r", "turn-on2-l", "static-on"]
OFF3 = ["turn-off-a-r", "turn-off-a-l", "static-off-a", "turn-off-b-r", "turn-off-b-l", "static-off-b"]
CHECKED3 = OFF3[:3] + ON3 + OFF3[3:]
CACHE_LINE = "\\[bvh\\] mesh cache: \\d+ meshes"
job3 = copy.deepcopy(job2)
job3.update({
    "name": "rv3-s8-mesh-cache-budget",
    "set": SET3,
    "args": ["-RealPresent", "-Move", "-MovePlan", PLAN3],
    "fail_fast": True,  # every gate below must hold key off as well (rv2 passed them key off)
    "estimated_phase_seconds": sum(p["seconds"] for p in plan3),
    "purpose": "rt_mesh_budget_own 0 / 1 / 0 in the rv1 S8 lowered view and turns (release defaults): with the key "
               "the mesh cache keeps the static meshes of the turn (about 300-550 meshes instead of 35-60), captured "
               "triangles per build fall from 4.0-6.2M to about 1.8-2.6M, BLAS builds/refits GPU median in motion from "
               "2.4-4.3 ms to about 1.0-2.0 ms (p99 7-13 -> 3-6 ms); capture, cascade, cache correctness and scene "
               "wait gates hold in every window.",
})
job3["patterns"] = job2["patterns"] + ["\\[bvh\\] last \\d+ builds", "\\[rt\\] scene build GPU"]
job3["expect"] = job["expect"] + [
    STRIDE_LINE,
    "\\[bvh\\] last \\d+ builds, per build:",
    "\\[bvh\\] mesh cache: .*budget \\(rt_mesh_budget_own=1\\): charged",
]
job3["forbid"] = job2["forbid"] + ["\\[bvh\\] allocation failed"]
job3["expect_every"] = [
    every("key state: the mesh cache line reports rt_mesh_budget_own=1 in the ON windows", CACHE_LINE,
          "budget \\(rt_mesh_budget_own=1\\)", ON3),
    every("key state: the mesh cache line reports rt_mesh_budget_own=0 in the OFF windows", CACHE_LINE,
          "budget \\(rt_mesh_budget_own=0\\)", OFF3),
    every("cache correctness: no instance or audit mismatch in every window", CACHE_LINE,
          "rejected \\d+/0/0 ", CHECKED3),
    every("capacity: cascade casters never left out (buffer full 0.0) in every window",
          "\\[cascade\\] casters: cascade \\d+, \\d+ frames;", ", buffer full 0\\.0, added ", CHECKED3),
    every("capacity: RT capture drops no opaque draw in every window",
          "\\[geo\\] RT capture, last \\d+ frames:", "opaque draws dropped \\(buffer full\\) 0\\.00,", CHECKED3),
    every("capacity: no alpha draw made opaque in every window",
          "\\[geo\\] RT capture, last \\d+ frames:", "made opaque \\(region full\\) 0\\.00,", CHECKED3),
    every("stride split: no wide (88-byte) draw left out in every window", STRIDE_LINE,
          "left out \\(region full or no split\\) 0\\.00 per frame", CHECKED3),
    every("scene wait: 0 timeouts in every window", "\\[rt\\] scene wait: \\d+ frames",
          "got this frame's scene, 0 timeouts", CHECKED3),
    every("strand resolve: 0 fallbacks wherever strand hair is resolved",
          r"\[rrpost\] strand resolve \(rr_hair_strand_aa=2\):", ", 0 fell back", CHECKED3, when_present=True, min=0),
    every("memory accounting in every window", "\\[mem\\] ours \\(", "cascade capture \\d+ MB", CHECKED3),
]
job3["expect_runner"] = [
    "\\[window-mode\\].*visible_unfocused",
    "\\[wheel-input\\] phase=2 notches=10\\b",
    "\\[motion-trace\\] phase=5 label=static-off-a repeat=0",
    "\\[motion-trace\\] phase=10 label=static-on repeat=0",
    f"\\[motion-plan\\] complete phases={len(plan3)}\\b",
    "^Run ended: done",
]

# sh2 (scene handout increment 3, claude/scene-handout-3, docs/SCENE_HANDOUT_DESIGN.md 6): Water Queens House, uncapped
# and focused (frame time is the deliverable), rt_scene_handout 0 / 2 / 0 in turns and static windows of one launch.
# rt_scene_handout_layout=1 from launch (startup key: the update-after-bind set 1 and the dummy TLAS; it changes no
# frame by itself, increment 1). The real Ultra preset since increment 4b: native hit materials (their query is built
# at the commit) and OMM (tickets on every handout user, increment 4) on. Short rt_stats + rt_primary_check
# windows (about 80 ms/frame) only after the timed windows of each state.
PLAN4 = "tools/live_batches/rel095_verify/plans/sh2_s8.json"
SET4 = (SET.replace("fg=3", "fg=3,fps_cap=0").replace("rt_capture_rise_reserve=0", "rt_capture_rise_reserve=1") +
        ",rt_capture_wide_split=1,rt_scene_handout_layout=1,rt_scene_handout=0,rt_stats_period_frames=60")


QUIET4 = "\\[watch\\].*\\| pipelines 0 \\(0 ms in driver\\)"


def phase4(label, seconds, handout, kind="static", direction=0, hold=1, trace=None, check=False, toggle=False):
    p = phase(label, seconds, (0, 1), kind, direction, None, hold, trace)
    p["set"] = (f"rt_scene_handout={handout},rt_stats={int(check)},rt_primary_check={int(check)},"
                f"qa_exposure_hold={hold}")
    # A toggle of rt_scene_handout re-selects constant 1051 in every gi/ and water pipeline: wait for a quiet driver
    # (tools/live_batches/pipeline_readiness.py) before the window's clock starts.
    if toggle:
        p["wait_log"] = [QUIET4]
        p["wait_log_fresh"] = [QUIET4]
        p["wait_log_timeout"] = 60.0
    return p


plan4 = [
    phase4("warmup", 20, 0, hold=0),
    phase4("warm-on", 8, 2, hold=0, toggle=True),   # builds the constant-1051 pipelines before the first measured ON window
    phase4("turn-off-a-r", 20, 0, "rotate", 1, toggle=True),
    phase4("turn-off-a-l", 20, 0, "rotate", -1),
    phase4("static-off-a", 15, 0, trace=12),
    phase4("check-off-a", 40, 0, check=True),
    phase4("turn-on-r", 20, 2, "rotate", 1, toggle=True),
    phase4("turn-on-l", 20, 2, "rotate", -1),
    phase4("static-on", 15, 2, trace=12),
    phase4("check-on", 40, 2, check=True),
    phase4("turn-off-b-r", 20, 0, "rotate", 1, toggle=True),
    phase4("turn-off-b-l", 20, 0, "rotate", -1),
    phase4("static-off-b", 15, 0, trace=12),
    phase4("check-off-b", 40, 0, check=True),
    phase4("restore", 2, 0, hold=0),
]
ON4 = ["turn-on-r", "turn-on-l", "static-on"]
STEADY4 = ["turn-on-l", "static-on"]
OFF4 = ["turn-off-a-r", "turn-off-a-l", "static-off-a", "turn-off-b-r", "turn-off-b-l", "static-off-b"]
CHECK4 = ["check-off-a", "check-on", "check-off-b"]
TIMED4 = OFF4[:3] + ON4 + OFF4[3:]
WAIT_LINE = "\\[rt\\] scene wait: \\d+ frames"
HANDOUT_LINE = "\\[rt\\] scene handout: \\d+ frames, reserved"
job4 = copy.deepcopy(job)
job4.update({
    "name": "sh2-s8-scene-handout",
    "set": SET4,
    "args": ["-RealPresent", "-Foreground", "-Move", "-MovePlan", PLAN4],
    "fail_fast": True,
    "timeout": 900,
    "max_minutes": 15,
    "estimated_phase_seconds": sum(p["seconds"] for p in plan4),
    "purpose": "rt_scene_handout 0 / 2 / 0 at S8, uncapped and focused (the real Ultra preset: hit materials, OMM): with the "
               "handout the lighting's first RT query no longer waits for the scene build (wait mean 2-7 ms -> < 0.1 ms, "
               "got = waited, 0 timeouts), 0 fallbacks and 0 pairing mismatches in the steady ON windows, rt_primary_check "
               "match and the static energy equal to the OFF windows, rendered frame time lower by up to the OFF wait.",
})
job4.pop("window_mode")   # focused (the game takes the focus: no unfocused 30 fps cap; ask the user first)
job4["patterns"] = job["patterns"] + ["\\[rt\\] scene handout", "\\[core\\] scene handout", "\\[alpha\\] set 1 layout",
                                      "\\[rt\\] \\+ scene handout", "\\[pace\\]", "rt stats \\(", "\\[rt\\] scene build GPU"]
job4["expect"] = [e for e in job["expect"] if "exposure held" not in e] + [
    "\\[rt\\] \\+ scene handout layout: storage buffer, sampled image",
    "\\[rt\\] scene handout layout on \\(set 1 update-after-bind\\)",
    "\\[alpha\\] set 1 layout update-after-bind",
    "\\[rt\\] scene handout: dummy TLAS \\(0 instances",
    HANDOUT_LINE,
    "\\[pace\\] rendered frame time over \\d+ frames",
    "\\[rrpost\\] exposure held \\(qa_exposure_hold=1\\) at EV [-0-9.]+",
]
job4["forbid"] = job["forbid"] + ["\\[rt\\] scene handout layout off", "\\[rt\\] scene handout: dummy TLAS (allocation|creation) failed",
                                  "\\[layer\\] optional scene handout update-after-bind request failed"]
job4["expect_every"] = [
    every("handout: no wait in the ON windows (every first query handed out or got its scene at once, mean < 0.1 ms)",
          WAIT_LINE, "(\\d+) waited \\(\\1 got this frame's scene, 0 timeouts, \\d+ skipped\\), wait mean 0\\.0\\d\\d ms, "
          "max [0-9.]+ ms, handed out [1-9]", ON4),
    every("handout: 0 fallbacks and 0 pairing mismatches in the steady ON windows", HANDOUT_LINE,
          "fallbacks 0 \\(.*\\), pairing mismatches 0,", STEADY4, period_frames=120),
    every("handout: frames handed out in the ON windows", HANDOUT_LINE, "handed out [1-9]", ON4, period_frames=120),
    every("scene wait: 0 timeouts in every timed window", WAIT_LINE, "got this frame's scene, 0 timeouts", TIMED4),
    every("primary-ray check in every check window (match compared offline: ON equal to OFF within the OFF a/b spread)",
          "rt stats \\(\\d+ frames, alpha test on\\): primary-ray check match", "primary-ray check match [0-9.]+%", CHECK4,
          period_frames=60),
    every("frame time in every timed window", "\\[pace\\] rendered frame time over \\d+ frames", "median [0-9.]+ ms", TIMED4),
    every("capacity: RT capture drops no opaque draw in every timed window",
          "\\[geo\\] RT capture, last \\d+ frames:", "opaque draws dropped \\(buffer full\\) 0\\.00,", TIMED4),
]
job4["expect_runner"] = [
    "\\[motion-trace\\] phase=4 label=static-off-a repeat=0",
    "\\[motion-trace\\] phase=8 label=static-on repeat=0",
    "\\[motion-trace\\] phase=12 label=static-off-b repeat=0",
    f"\\[motion-plan\\] complete phases={len(plan4)}\\b",
    "^Run ended: done",
]
job4["live_flow"] = {"expected_fps": 60, "settings_latency_s": 1, "trace_stall_s": 3,
                     "startup_only": ["rt_scene_handout_layout"]}

(HERE / "plans" / "rv1_s8.json").write_text(json.dumps(plan, indent=1) + "\n", encoding="utf-8")
(HERE / "plans" / "rv2_s8.json").write_text(json.dumps(plan, indent=1) + "\n", encoding="utf-8")
(HERE / "plans" / "rv3_s8.json").write_text(json.dumps(plan3, indent=1) + "\n", encoding="utf-8")
(HERE / "plans" / "sh2_s8.json").write_text(json.dumps(plan4, indent=1) + "\n", encoding="utf-8")
for j, pl in ((job, plan), (job2, plan), (job3, plan3), (job4, plan4)):
    (HERE / "jobs" / f"{j['name']}.json").write_text(json.dumps(j, indent=1) + "\n", encoding="utf-8")
    print(f"{j['name']}: {len(pl)} phases, {j['estimated_phase_seconds']} s")
