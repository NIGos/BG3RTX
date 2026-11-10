"""hair_late (branch claude/hair-arrival-cause, docs/RTX_HAIR_PRIMARY.md 5u): STAGED, not queued, not authorized.

    py -3 -m tools.live_batches_staged.hair_late.make_job

hl1 161106: the strands of the existing keys failed (frame watchdog at G-buffer frames 1992-1998) at the frame the game
first drew three more hair draws (45252, 100101, 7092: "UV join pending"), right after the runner started the "lower"
phase, before any groom of them existed; the artificial hold of 45252 (rt_hair_debug_late_arrival) reproduced nothing.
This version gates the natural arrival as 161106 had it: one Water Queens House (S8) launch, the RTX Hair bundle with
rt_hair_groom_nearest=1, detailed logging (the observables print with it), warm-up, then the same phase change ("lower",
wheel 10) that triggered the arrivals, then a watch window. The observables decide between the candidate causes
(predicted.json); the job does not require holes (a run without them is still informative). predicted.json precedes
the job. The job lives outside every watched captures/queue/pending inbox.
"""
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))

NAME = "hl1-s8-late-arrival"
PLAN = f"tools/live_batches_staged/hair_late/plans/{NAME}.json"
# The RTX Hair On bundle (common/settings.h exp_hair_groom_primary "on"), as hs1's; no test aid.
HAIR = ("rt_hair_groom=1,rt_hair_groom_primary=1,rt_hair_groom_primary_order=1,rt_hair_groom_primary_current=1,"
        "rt_hair_primary_frame_tag=1,rt_hair_groom_lod_keep=1,rt_hair_primary_hold=1,rt_hair_tangent_world=1,"
        "rt_hair_groom_shadow_t=1,rt_hair_groom_ao_t=1,rt_hair_groom_cascade_drop=1,rt_hair_fibre=1,"
        "rt_hair_fibre_gi=1,rt_hair_fibre_spec=1,rt_hair_fibre_mapped=1,rr_hair_guides=1,"
        "rt_hair_physical_origin=1,rt_hair_capture=1,rt_hair_coverage_union=1,rt_hair_coverage_confirm=1,"
        "rt_hair_slot_stable=1,rt_hair_live_warmup=1,rt_hair_groom_nearest=1")
SET = ("preset=ultra,fg=3,headless=0,crash_dump=1,rt_timing_frames=120,rt_scene_wait_log_frames=120,"
       "rt_debug=0,ls_debug=0,gi_debug=0,rf_debug=0,rr_show=0,rf_stats=0,ls_stats=0,ls_check=0,rt_stats=0,"
       "rt_primary_check=0,dump_images=0,dump_raw=0,dump_shaders=0,pan_capture=0,pan_inputs=0,pan_prepare=0,sharc=0,"
       "qa_exposure_hold=0,rt_alpha_floor_yield=0,rt_capture_rise_reserve=1,rt_capture_wide_split=1,"
       "rt_mesh_budget_own=0,detailed_logging=1,rt_stats_period_frames=60," + HAIR)


def phase(label, seconds, wheel=None, frames=None):
    p = {"label": label, "kind": "static", "speed": "static", "seconds": seconds, "repeat_ms": 0, "scan": 16,
         "capture_at": 0, "serial": 0, "motion_start": seconds, "motion_end": seconds, "pixels_per_second": 0,
         "direction": 0, "set": "rt_hair_groom_primary=1,rt_hair_groom_nearest=1"}
    if wheel:
        p["wheel"] = wheel
    if frames:   # a frame window keeps its length through live_flow's tightening (camera-plan: needs stats_reset)
        p.update(frames=frames, settle_frames=0, stats_reset=True, wait_stats=[], wait_log_timeout=30)
    return p


def make_plan():
    # 161106's first 28 s: warm-up 20 s, then "lower" (its phase start triggered the arrivals 0.06 s later), then 12 s
    # to watch the transient settle (161106: trips at frames 1992-1998, strands back after the 28-frame hold).
    return [
        phase("warmup", 20),
        phase("lower", 8, wheel=10, frames=240),
        phase("watch", 12, frames=360),
    ]


def make_job(plan):
    return {
        "name": NAME,
        "save": "Water Queens House",
        "session": False,
        "traces": 0,
        "hold": 120,
        "timeout": 420,
        "max_minutes": 3,
        "repeat": 1,
        "live_batch": "hair-late",
        "tree": ".claude/worktrees/test-main",
        "window_mode": "visible_unfocused_fg",
        "fail_fast": False,
        "stop_on_dump": False,
        "set": SET,
        "args": ["-RealPresent", "-Move", "-MovePlan", PLAN],
        "patterns": ["\\[auto\\]", "\\[cfg\\]", "\\[hot\\]", "\\[watch\\]", "\\[crash\\]", "FAILED",
                     "\\[hair primary\\]", "\\[hair groom\\]", "\\[rt\\] scene wait", "VK_ERROR", "\\[log\\]"],
        "expect": [
            "\\[auto\\] scene ready",
            "\\[hot\\] core generation \\d+ loaded",
            "\\[cfg\\] settings read",
            # Q1: the nearest-strand trace compiled in and its header flag on
            "\\[hair primary\\] G-buffer hair pipeline: .*nearest-strand trace compiled in \\(specialization 1047\\) 1",
            "\\[hair primary\\] nearest-strand trace: header flag 1 .*specialization 1047 in the strand G-buffer pipelines 1",
        ],
        # The observables, as events in their windows (not startup producers: the arrival is what the phase change
        # triggers): the first-seen lines in "lower", the TLAS check period line in "lower" and "watch".
        "expect_every": [
            {"name": "the arrival: 45252 waits for its UV join in the lower window",
             "select": "\\[hair groom\\] unmatched hair draw: 45252 indices", "match": "UV join pending",
             "windows": ["lower"], "event": True, "min": 1},
            {"name": "the first-seen frame lines in the lower window",
             "select": "\\[hair groom\\] frame \\d+ \\(a hair draw first seen",
             "match": "groom rows \\d+ \\(previous publish \\d+\\)", "windows": ["lower"], "event": True, "min": 1},
            {"name": "the groom TLAS check period line in both windows",
             "select": "\\[hair primary\\] groom TLAS check \\(last", "match": "G-buffer submits checked",
             "windows": ["watch"], "event": True, "min": 1},
        ],
        "forbid": [
            "\\[crash\\] exception 0x", "VK_ERROR_DEVICE_LOST", "\\[hot\\] core (rejected|faulted)",
            "FAILED, keeping previous", "compile call failed", "\\[log\\] reached \\d+ MB", "\\[sl\\] .*eWarnOutOfVRAM",
        ],
        "expect_runner": [
            "\\[window-mode\\].*visible_unfocused",
            "\\[wheel-input\\] phase=1 notches=10\\b",
            f"\\[motion-plan\\] complete phases={len(plan)}\\b",
            "^Run ended: done",
        ],
        "live_flow": {"expected_fps": 30, "settings_latency_s": 1, "trace_stall_s": 3},
        "estimated_phase_seconds": sum(p["seconds"] for p in plan),
        "purpose": "Natural late hair arrival at S8 (161106's 'lower' phase change), nearest-strand trace on, with the "
                   "groom TLAS check and the first-seen frame lines: which candidate breaks the existing keys' strands. "
                   "STAGED, not authorized.",
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
