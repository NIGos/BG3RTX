"""hair_cam (claude/hair-header-cam, docs/RTX_HAIR_PRIMARY.md 5i item 11): STAGED, not queued (no live permission yet).

One launch at Water Queens House (S8), the rv3 view (release defaults + RTX Hair keys, camera lowered 10 notches),
identical right/left turns with an in-run A/B of the two live keys:
  off-a (0,0) -> cam (rt_hair_header_cam_gpu=1) -> draw (rt_hair_watchdog_draw=1) -> both (1,1) -> off-b (0,0).
Evidence before the fix (rv3-s8-mesh-cache-budget 220145): sampled strand origin 0 -> 94-98.5% strand share (8/8 frames),
origin 2.6-15.5 cm -> 18-80% (7/7); 0-23 frame-watchdog trips per 300 frames, each holding every hair draw on cards
for 28 frames (refusals "watchdog" 195-2045 per line).
Gates in the cam and both windows: frame watchdog trips 0, no watchdog swap refusal, header cameras copied on the GPU,
sampled origin 0, evidence strand share >= kCoverMin 0.891 (common/hair_primary.h, the strict coverage bound). The
item 11 proof counter (re-read at submit vs record-time read) must be reported in every window. OFF windows: baseline.
Generate: py -3 tools/live_batches_staged/hair_cam/make_job.py
"""
import json
import pathlib

HERE = pathlib.Path(__file__).resolve().parent
PLAN = "tools/live_batches_staged/hair_cam/plans/hc1_s8.json"

HAIR = ("rt_hair_groom=1,rt_hair_groom_primary=1,rt_hair_groom_primary_order=1,rt_hair_groom_primary_current=1,"
        "rt_hair_primary_frame_tag=1,rt_hair_groom_lod_keep=1,rt_hair_primary_hold=1,rt_hair_tangent_world=1,"
        "rt_hair_groom_shadow_t=1,rt_hair_groom_ao_t=1,rt_hair_groom_cascade_drop=1,rt_hair_fibre=1,"
        "rt_hair_fibre_gi=1,rt_hair_fibre_spec=1,rt_hair_fibre_mapped=1,rr_hair_guides=1,"
        "rt_hair_physical_origin=1,rt_hair_capture=1,rr_hair_strand_aa=2,rt_hair_warmup=1")
SET = ("preset=ultra,fg=3,headless=0,crash_dump=1,rt_timing_frames=120,rt_scene_wait_log_frames=120,"
       "rt_stats_period_frames=120,"
       "rt_debug=0,ls_debug=0,gi_debug=0,rf_debug=0,rr_show=0,rf_stats=0,ls_stats=0,ls_check=0,rt_stats=0,"
       "rt_primary_check=0,dump_images=0,dump_raw=0,dump_shaders=0,pan_capture=0,"
       "pan_inputs=0,pan_prepare=0,sharc=0,qa_exposure_hold=0,rt_alpha_floor_yield=0,rt_capture_rise_reserve=1,"
       "rt_capture_wide_split=1,rt_mesh_budget_own=0,rt_hair_header_cam_gpu=0,rt_hair_watchdog_draw=0," + HAIR)


def phase(label, seconds, keys, kind="static", direction=0, wheel=None):
    cam, draw = keys
    p = {"label": label, "kind": kind, "speed": "slow" if kind == "rotate" else "static",
         "seconds": seconds, "repeat_ms": 0, "scan": 16, "capture_at": 0, "serial": 0,
         "motion_start": 1 if kind == "rotate" else seconds,
         "motion_end": seconds - 1 if kind == "rotate" else seconds,
         "pixels_per_second": 60 if kind == "rotate" else 0, "direction": direction,
         "set": f"rt_hair_header_cam_gpu={cam},rt_hair_watchdog_draw={draw}"}
    if wheel:
        p["wheel"] = wheel
    return p


OFF, CAM, DRAW, BOTH = (0, 0), (1, 0), (0, 1), (1, 1)
plan = [
    phase("warmup", 20, OFF),
    phase("warm-on", 6, BOTH),   # builds the layer's camera UBO copy pipeline before the first measured ON window
    phase("lower", 3, OFF, wheel=10),
    phase("turn-off-a-r", 20, OFF, "rotate", 1),
    phase("turn-off-a-l", 20, OFF, "rotate", -1),
    phase("turn-cam-r", 20, CAM, "rotate", 1),
    phase("turn-cam-l", 20, CAM, "rotate", -1),
    phase("turn-draw-r", 20, DRAW, "rotate", 1),
    phase("turn-draw-l", 20, DRAW, "rotate", -1),
    phase("turn-both-r", 20, BOTH, "rotate", 1),
    phase("turn-both-l", 20, BOTH, "rotate", -1),
    phase("turn-off-b-r", 20, OFF, "rotate", 1),
    phase("turn-off-b-l", 20, OFF, "rotate", -1),
    phase("restore", 1, OFF),
]
CAM_ON = ["turn-cam-r", "turn-cam-l", "turn-both-r", "turn-both-l"]
DRAW_ON = ["turn-draw-r", "turn-draw-l", "turn-both-r", "turn-both-l"]
BOTH_ON = ["turn-both-r", "turn-both-l"]
CHECKED = [p["label"] for p in plan if p["label"].startswith("turn-")]

SAFETY = "\\[hair primary\\] header safety \\(last \\d+ frames\\)"
TRACES = "\\[hair primary\\] strand traces in counted frame \\d+:"
FRAME = "\\[hair primary\\] frame \\d+: \\d+ grooms published"


def every(name, select, match, windows, **kw):
    e = {"name": name, "select": select, "match": match, "min": 1, "windows": windows, "period_frames": 120}
    e.update(kw)
    return e


job = {
    "name": "hc1-s8-header-cam",
    "save": "Water Queens House",
    "session": False,
    "traces": 0,
    "hold": 120,
    "timeout": 600,
    "max_minutes": 10,
    "repeat": 1,
    "live_batch": "hair-cam",
    "tree": ".claude/worktrees/test-main",
    "window_mode": "visible_unfocused_fg",
    "fail_fast": False,   # the OFF windows are the baseline (watchdog trips expected there): measure every window
    "stop_on_dump": False,
    "set": SET,
    "args": ["-RealPresent", "-Move", "-MovePlan", PLAN],
    "patterns": ["\\[auto\\]", "\\[cfg\\]", "\\[hot\\]", "\\[watch\\]", "\\[crash\\]", "FAILED", "\\[hair primary\\]",
                 "\\[rt\\] scene wait", "rt passes: GPU cost", "\\[sl\\] DLSS-G frame", "\\[sl\\] warn", "VK_ERROR",
                 "\\[log\\]"],
    "expect": [
        "\\[auto\\] scene ready",
        "\\[hot\\] core generation \\d+ loaded",
        "\\[cfg\\] settings read",
        "\\[hair primary\\] header at 0x",
        "\\[hair primary\\] live: strands ON at frame \\d+",
        SAFETY + ".*build camera re-read at submit \\(item 11\\): [1-9]\\d* publishes checked",
        TRACES + ".*header camera \\(",
    ],
    "forbid": [
        "\\[crash\\] exception 0x", "VK_ERROR_DEVICE_LOST", "\\[hot\\] core (rejected|faulted)",
        "FAILED, keeping previous", "compile call failed", "\\[log\\] reached \\d+ MB",
        "\\[hair primary\\] strand feedback unavailable", "\\[hair primary\\] UNAVAILABLE",
    ],
    "expect_every": [
        every("proof: the item 11 re-read is reported in every window (mismatches counted, any value)", SAFETY,
              "build camera re-read at submit \\(item 11\\): [1-9]\\d* publishes checked, \\d+ differ from the "
              "record-time read \\(max [0-9.]+ m\\)", CHECKED),
        every("key state: header cameras copied on the GPU in the cam windows", SAFETY,
              ", [1-9]\\d* header cameras copied on the GPU", CAM_ON),
        every("key state: the per-draw watchdog decides in the draw windows", SAFETY, "\\(deciding: per draw\\)", DRAW_ON),
        every("cam fix: 0 frame watchdog trips in the cam windows", SAFETY, ", watchdog trips 0, ", CAM_ON),
        every("cam fix: 0 per-draw watchdog trips in the both windows", SAFETY, "per-draw watchdog trips 0 ", BOTH_ON),
        every("cam fix: no main-view hair draw kept as cards by the watchdog in the cam windows", FRAME,
              "^(?!.*swap refusals[^;]*watchdog [1-9])", CAM_ON),
        every("cam fix: sampled strand ray origin 0 (header camera = G-buffer camera) in the cam windows", TRACES,
              "origin \\(-?0\\.0000 -?0\\.0000 -?0\\.0000\\)", CAM_ON),
        every("cam fix: evidence strand share >= kCoverMin 0.891 in the cam windows", TRACES,
              "evidence strand share (0\\.89[1-9]|0\\.9\\d\\d|1\\.\\d+)", CAM_ON),
        every("scene wait: 0 timeouts in every window", "\\[rt\\] scene wait: \\d+ frames",
              "got this frame's scene, 0 timeouts", CHECKED),
    ],
    "expect_runner": [
        "\\[window-mode\\].*visible_unfocused",
        "\\[wheel-input\\] phase=2 notches=10\\b",
        f"\\[motion-plan\\] complete phases={len(plan)}\\b",
        "^Run ended: done",
    ],
    "live_flow": {"expected_fps": 30, "settings_latency_s": 1, "trace_stall_s": 3},
    "estimated_phase_seconds": sum(p["seconds"] for p in plan),
    "purpose": "RTX Hair item 11 at S8 in the rv3 view and turns: rt_hair_header_cam_gpu and rt_hair_watchdog_draw "
               "off / cam / draw / both / off in one run. With the GPU header camera: sampled origin 0, frame watchdog "
               "trips 0, no watchdog card fallback, evidence strand share >= 0.891; the re-read proof counter reports "
               "record-time vs submit camera mismatches in every window (OFF: watchdog trips and nonzero origins).",
}

(HERE / "plans").mkdir(exist_ok=True)
(HERE / "jobs").mkdir(exist_ok=True)
(HERE / "plans" / "hc1_s8.json").write_text(json.dumps(plan, indent=1) + "\n", encoding="utf-8")
(HERE / "jobs" / f"{job['name']}.json").write_text(json.dumps(job, indent=1) + "\n", encoding="utf-8")
print(f"{job['name']}: {len(plan)} phases, {job['estimated_phase_seconds']} s")


# hk1 (claude/hair-shared-key-live, docs/RTX_HAIR_PRIMARY.md 5i shared draw key): the same S8 view and turns as hc1,
# in-run A/B of rt_hair_shared_key / rt_hair_watchdog_one_key with rt_hair_header_cam_gpu and rt_hair_watchdog_draw
# at 0 (disproved / worse in hc1 2026-11-09). Windows: static warm-up (off, then both on: the sibling check), turns
# off (must reproduce the hc1 trips, else the run proves nothing), turns shared, turns shared + one key.
HK_PLAN = "tools/live_batches_staged/hair_cam/plans/hk1_s8.json"


def hk_phase(label, seconds, keys, kind="static", direction=0, wheel=None):
    shared, one = keys
    p = phase(label, seconds, (0, 0), kind, direction, wheel)
    p["set"] += f",rt_hair_shared_key={shared},rt_hair_watchdog_one_key={one}"
    return p


HK_OFF, HK_SHARED, HK_BOTH = (0, 0), (1, 0), (1, 1)
hk_plan = [
    hk_phase("warmup", 20, HK_OFF),
    hk_phase("warm-on", 4, HK_BOTH),     # every measured key value once before the measured windows (warm-up lint)
    hk_phase("static-on", 12, HK_BOTH),  # (a) static with both keys: the sibling check (shared-key rows withheld 0)
    hk_phase("lower", 3, HK_OFF, wheel=10),
    hk_phase("turn-off-r", 20, HK_OFF, "rotate", 1),
    hk_phase("turn-off-l", 20, HK_OFF, "rotate", -1),
    hk_phase("turn-shared-r", 20, HK_SHARED, "rotate", 1),
    hk_phase("turn-shared-l", 20, HK_SHARED, "rotate", -1),
    hk_phase("turn-both-r", 20, HK_BOTH, "rotate", 1),
    hk_phase("turn-both-l", 20, HK_BOTH, "rotate", -1),
    hk_phase("restore", 1, HK_OFF),
]
HK_TURN_OFF = ["turn-off-r", "turn-off-l"]
HK_SHARED_ON = ["turn-shared-r", "turn-shared-l", "turn-both-r", "turn-both-l"]
HK_ONE_ON = ["turn-both-r", "turn-both-l"]
HK_ONE_OFF = HK_TURN_OFF + ["turn-shared-r", "turn-shared-l"]
HK_MEASURED = ["static-on"] + HK_TURN_OFF + HK_SHARED_ON
DROPS = "\\[hair primary\\] dropouts \\(last \\d+ frames\\)"
GROOM = "\\[hair groom\\] frame \\d+: \\d+ groom instances; last \\d+ frames"

hk_job = dict(job)
hk_job.update({
    "name": "hk1-s8-shared-key",
    "timeout": 420,
    "max_minutes": 5,
    "live_batch": "hair-shared-key",
    "fail_fast": True,
    "set": SET + ",rt_hair_shared_key=0,rt_hair_watchdog_one_key=0",
    "args": ["-RealPresent", "-Move", "-MovePlan", HK_PLAN],
    "patterns": job["patterns"] + ["\\[hair groom\\] frame \\d+: \\d+ groom instances"],
    "expect": [
        "\\[auto\\] scene ready",
        "\\[hot\\] core generation \\d+ loaded",
        "\\[cfg\\] settings read",
        "\\[hair primary\\] header at 0x",
        "\\[hair primary\\] live: strands ON at frame \\d+",
        SAFETY + ".*frame watchdog spared to one key \\d+ \\(rt_hair_watchdog_one_key [01]\\)",
        GROOM + ".*shared-key rows withheld \\d+ \\(rt_hair_shared_key\\)",
    ],
    "expect_every": [
        every("baseline: the hc1 frame watchdog trips reproduce in each turn-off window (else the run proves nothing)",
              SAFETY + ".*, watchdog trips [1-9]\\d*, ", "", HK_TURN_OFF, event=True),
        every("fix: 0 frame watchdog trips in every shared-key window", SAFETY, ", watchdog trips 0, ", HK_SHARED_ON),
        every("fix: 0 dropouts in every shared-key window", DROPS, "\\): 0 swapped G-buffer draws wrote", HK_SHARED_ON),
        every("report: dropouts count in every measured window", DROPS, "\\): \\d+ swapped G-buffer draws wrote",
              HK_MEASURED),
        every("report: shared-key rows withheld in every measured window", GROOM,
              "shared-key rows withheld \\d+ \\(rt_hair_shared_key\\)", HK_MEASURED),
        every("key state: one-key watchdog on in the both windows", SAFETY,
              "frame watchdog spared to one key \\d+ \\(rt_hair_watchdog_one_key 1\\)", HK_ONE_ON + ["static-on"]),
        every("key state: one-key watchdog off in the off and shared windows", SAFETY,
              "frame watchdog spared to one key \\d+ \\(rt_hair_watchdog_one_key 0\\)", HK_ONE_OFF),
        every("key state: the frame watchdog decides (rt_hair_watchdog_draw 0)", SAFETY, "\\(deciding: frame\\)",
              HK_MEASURED),
        every("report: hair G-buffer draws strand/total in every measured window", FRAME,
              "hair draws strand/total: main G-buffer \\d+/\\d+", HK_MEASURED),
        every("scene wait: 0 timeouts in every window", "\\[rt\\] scene wait: \\d+ frames",
              "got this frame's scene, 0 timeouts", HK_MEASURED),
    ],
    "expect_runner": [
        "\\[window-mode\\].*visible_unfocused",
        f"\\[wheel-input\\] phase={[p['label'] for p in hk_plan].index('lower')} notches=10\\b",
        f"\\[motion-plan\\] complete phases={len(hk_plan)}\\b",
        "^Run ended: done",
    ],
    "estimated_phase_seconds": sum(p["seconds"] for p in hk_plan),
    "purpose": "RTX Hair shared draw key at S8 in the hc1 view and turns: rt_hair_shared_key and "
               "rt_hair_watchdog_one_key off / shared / both in one run (header_cam_gpu and watchdog_draw 0). Turn-off "
               "must reproduce the hc1 frame watchdog trips; shared-key windows: 0 trips and 0 dropouts per window.",
})
(HERE / "plans" / "hk1_s8.json").write_text(json.dumps(hk_plan, indent=1) + "\n", encoding="utf-8")
(HERE / "jobs" / f"{hk_job['name']}.json").write_text(json.dumps(hk_job, indent=1) + "\n", encoding="utf-8")
print(f"{hk_job['name']}: {len(hk_plan)} phases, {hk_job['estimated_phase_seconds']} s")
