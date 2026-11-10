"""hair_late (branch claude/hair-late-arrival, docs/RTX_HAIR_PRIMARY.md 5u): STAGED, not queued, not authorized.

    py -3 -m tools.live_batches_staged.hair_late.make_job

hs1 125217's holes came from a groom that arrived (first join and upload) after the warm-up of a live switch-on had
ended, while the other keys swapped; 103633 and 133243 had the same arrivals inside the warm-up window and showed
nothing. rt_hair_debug_late_arrival=45252 makes the arrival late on demand: after every switch-on the groom of the
45252-index draws is held for 301 frames (one past the longest warm-up window). One Water Queens House (S8) launch with
the RTX Hair bundle and rt_hair_groom_nearest=1 at startup (specialization 1047 compiled in), static camera, two
cycles: A with rt_hair_groom_nearest=0 live (header flag off: the restart loop) must show the holes; B with it on must
show none. predicted.json precedes the job. The job lives outside every watched captures/queue/pending inbox.
"""
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))

NAME = "hl1-s8-late-arrival"
PLAN = f"tools/live_batches_staged/hair_late/plans/{NAME}.json"
# The RTX Hair On bundle (common/settings.h exp_hair_groom_primary "on"), as hs1's, plus the test aid.
HAIR = ("rt_hair_groom=1,rt_hair_groom_primary=1,rt_hair_groom_primary_order=1,rt_hair_groom_primary_current=1,"
        "rt_hair_primary_frame_tag=1,rt_hair_groom_lod_keep=1,rt_hair_primary_hold=1,rt_hair_tangent_world=1,"
        "rt_hair_groom_shadow_t=1,rt_hair_groom_ao_t=1,rt_hair_groom_cascade_drop=1,rt_hair_fibre=1,"
        "rt_hair_fibre_gi=1,rt_hair_fibre_spec=1,rt_hair_fibre_mapped=1,rr_hair_guides=1,"
        "rt_hair_physical_origin=1,rt_hair_capture=1,rt_hair_coverage_union=1,rt_hair_coverage_confirm=1,"
        "rt_hair_slot_stable=1,rt_hair_live_warmup=1,rt_hair_groom_nearest=1,rt_hair_debug_late_arrival=45252")
SET = ("preset=ultra,fg=3,headless=0,crash_dump=1,rt_timing_frames=120,rt_scene_wait_log_frames=120,"
       "rt_debug=0,ls_debug=0,gi_debug=0,rf_debug=0,rr_show=0,rf_stats=0,ls_stats=0,ls_check=0,rt_stats=0,"
       "rt_primary_check=0,dump_images=0,dump_raw=0,dump_shaders=0,pan_capture=0,pan_inputs=0,pan_prepare=0,sharc=0,"
       "qa_exposure_hold=0,rt_alpha_floor_yield=0,rt_capture_rise_reserve=1,rt_capture_wide_split=1,"
       "rt_mesh_budget_own=0,detailed_logging=1,rt_stats_period_frames=60," + HAIR)
# An arrival window, in frames (a frame window keeps its length through live_flow's tightening): the release comes
# kLateArrivalFrames = 301 frames after the switch-on, then at least two complete 60-frame periods after the one the
# release falls in (the first line after a toggle straddles): 301 + 3 x 60 = 481 frames (16 s at 30 fps).
ARRIVAL_FRAMES = 301 + 3 * 60


def phase(label, seconds, strands=1, nearest=1, wheel=None, frames=None):
    p = {"label": label, "kind": "static", "speed": "static", "seconds": seconds, "repeat_ms": 0, "scan": 16,
         "capture_at": 0, "serial": 0, "motion_start": seconds, "motion_end": seconds, "pixels_per_second": 0,
         "direction": 0, "set": f"rt_hair_groom_primary={strands},rt_hair_groom_nearest={nearest}"}
    if wheel:
        p["wheel"] = wheel
    if frames:
        # A frame window (camera-plan.ps1 Assert-CameraPlanFields): the runner resets the stats periods when the window
        # starts (stats_reset, acknowledged by the game) and counts `frames` from there. The switch-on is applied at the
        # settings read just before, so the release (301 frames after it) falls inside the 481 frames, and every period
        # line the event gates read covers only this window's frames (no line straddles the toggle).
        p.update(frames=frames, settle_frames=0, stats_reset=True, wait_stats=[], wait_log_timeout=30)
    return p


def make_plan():
    return [
        phase("warmup", 20),
        phase("lower", 8, wheel=10),
        # every value the measured windows use, before them (live_flow warm-up lint)
        phase("warm-loop", 3, nearest=0),
        phase("off-a", 4, strands=0, nearest=0),
        phase("arrival-a", ARRIVAL_FRAMES / 30, nearest=0, frames=ARRIVAL_FRAMES),
        phase("off-b", 4, strands=0),
        phase("arrival-b", ARRIVAL_FRAMES / 30, frames=ARRIVAL_FRAMES),
        phase("restore", 2),
    ]


def make_job(plan):
    release = "\\[hair primary\\] late arrival: groom of the 45252-index hair draws released"
    return {
        "name": NAME,
        "save": "Water Queens House",
        "session": False,
        "traces": 0,
        "hold": 120,
        "timeout": 420,
        "max_minutes": 5,
        "repeat": 1,
        "live_batch": "hair-late",
        "tree": ".claude/worktrees/test-main",
        "window_mode": "visible_unfocused_fg",
        "fail_fast": True,
        "stop_on_dump": False,
        "set": SET,
        "args": ["-RealPresent", "-Move", "-MovePlan", PLAN],
        "patterns": ["\\[auto\\]", "\\[cfg\\]", "\\[hot\\]", "\\[watch\\]", "\\[crash\\]", "FAILED",
                     "\\[hair primary\\] frame", "\\[hair primary\\] switches", "\\[hair primary\\] coverage by footprint",
                     "\\[hair primary\\] late arrival", "\\[hair primary\\] live", "\\[hair primary\\] watchdog",
                     "\\[hair primary\\] header safety", "\\[hair primary\\] strand G-buffer draws GPU",
                     "\\[rt\\] scene wait", "VK_ERROR", "\\[log\\]"],
        "expect": [
            "\\[auto\\] scene ready",
            "\\[hot\\] core generation \\d+ loaded",
            "\\[cfg\\] settings read",
            "\\[hair primary\\] late arrival: groom of the 45252-index hair draws held",
            release,
        ],
        "forbid": [
            "\\[crash\\] exception 0x", "VK_ERROR_DEVICE_LOST", "\\[hot\\] core (rejected|faulted)",
            "FAILED, keeping previous", "compile call failed", "\\[log\\] reached \\d+ MB", "\\[sl\\] .*eWarnOutOfVRAM",
        ],
        # Every period line of an arrival window is judged as an event (no straddle skip, the window keeps its 481
        # frames): 481 frames hold 7-8 lines of 60 frames, min 6. A line straddling the switch-on covers card frames
        # before it (no trips there either), so judging it too is conservative.
        "expect_every": [
            {"name": "late arrival released inside each arrival window", "select": release, "match": r"released at frame \d+, (30[1-9]|3[1-9]\d) frames after the switch-on",
             "windows": ["arrival-a", "arrival-b"], "event": True, "min": 1},
            {"name": "A (restart loop): the late arrival leaves holes (frame watchdog trips or dropouts)",
             "select": "\\[hair primary\\] switches \\(last .*frame watchdog trips [1-9]|"
                       "\\[hair primary\\] coverage by footprint .*dropouts [1-9]",
             "match": "trips [1-9]|dropouts [1-9]", "windows": ["arrival-a"], "event": True, "min": 1},
            {"name": "B (nearest): 0 frame and key watchdog trips in every period",
             "select": "\\[hair primary\\] switches \\(last", "match": "frame watchdog trips 0 .*key watchdog trips 0;",
             "windows": ["arrival-b"], "event": True, "min": 6},
            {"name": "B (nearest): at most 2 dropouts in every period",
             "select": "\\[hair primary\\] coverage by footprint", "match": "dropouts [0-2] \\(",
             "windows": ["arrival-b"], "event": True, "min": 6},
        ],
        "expect_runner": [
            "\\[window-mode\\].*visible_unfocused",
            "\\[wheel-input\\] phase=1 notches=10\\b",
            f"\\[motion-plan\\] complete phases={len(plan)}\\b",
            "^Run ended: done",
        ],
        "live_flow": {"expected_fps": 30, "settings_latency_s": 1, "trace_stall_s": 3},
        "estimated_phase_seconds": sum(p["seconds"] for p in plan),
        "purpose": "Late groom arrival at S8 (rt_hair_debug_late_arrival=45252, 301 frames after each live switch-on): "
                   "A with rt_hair_groom_nearest=0 live must show 125217's holes, B with it on none. STAGED, not "
                   "authorized.",
    }


def main():
    assert (HERE / "predicted.json").exists(), "predicted.json precedes the job"
    plan = make_plan()
    for folder, value in (("plans", plan), ("jobs", make_job(plan))):
        (HERE / folder).mkdir(exist_ok=True)
        (HERE / folder / f"{NAME}.json").write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8", newline="\n")
    from tools.live_batches.live_flow import tighten_files
    tighten_files(HERE, job_names=[NAME])
    print(f"Generated {NAME}; no queue staging")


if __name__ == "__main__":
    main()
