"""hair_pixel_lod (branch claude/hair-pixel-lod-small, docs/RTX_HAIR_PRIMARY.md 5v small heads): STAGED, not queued,
not authorized.

NOT CURRENT since 2026-11-14 (docs/RTX_HAIR_PRIMARY.md 6k): pl1 sets rt_hair_row_stable, which is removed (the key
is ignored), and its bundle predates rt_hair_header_scene_pass and rt_hair_coverage_keep. Regenerate it from the
RTX Hair On bundle of common/settings.h before any use. The job still lints.

    py -3 -m tools.live_batches_staged.hair_pixel_lod.make_job

hl1 200821 showed every head at S8's zoomed-out camera is 6-53 px, so the 60 px floor left every groom at camera level 0.
The ladder sweep (tools/hair_groom/pixel_lod.py --ladder) found that heads of 35-60 px may trace keep 1/4 of the groom
(coverage within today's LOD step, HF +15 to +32%); 1/8 adds about +2% coverage. One Water Queens House (S8) launch,
the RTX Hair bundle as hl1, the same "lower" wheel step to the isometric view hl1 measured, then a static camera:
rt_hair_pixel_lod off / on (small keep 1/4) / on (small keep 1/8, the dev knob) / off, each with a 60-frame window (the
period line, the strand G-buffer GPU ms) and one PNG trace. predicted.json precedes the job. The job lives outside every
watched captures/queue/pending inbox.
"""
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))

NAME = "pl1-s8-small-heads"
PLAN = f"tools/live_batches_staged/hair_pixel_lod/plans/{NAME}.json"
# The RTX Hair On bundle with hl1's additions (rt_hair_groom_nearest, rt_hair_row_stable); no test aid.
HAIR = ("rt_hair_groom=1,rt_hair_groom_primary=1,rt_hair_groom_primary_order=1,rt_hair_groom_primary_current=1,"
        "rt_hair_primary_frame_tag=1,rt_hair_groom_lod_keep=1,rt_hair_primary_hold=1,rt_hair_tangent_world=1,"
        "rt_hair_groom_shadow_t=1,rt_hair_groom_ao_t=1,rt_hair_groom_cascade_drop=1,rt_hair_fibre=1,"
        "rt_hair_fibre_gi=1,rt_hair_fibre_spec=1,rt_hair_fibre_mapped=1,rr_hair_guides=1,"
        "rt_hair_physical_origin=1,rt_hair_capture=1,rt_hair_coverage_union=1,rt_hair_coverage_confirm=1,"
        "rt_hair_slot_stable=1,rt_hair_live_warmup=1,rt_hair_groom_nearest=1,rt_hair_row_stable=1,"
        "rt_hair_pixel_lod=0,rt_hair_pixel_lod_k=1.0,rt_hair_pixel_lod_small_keep=0.25")
SET = ("preset=ultra,fg=3,headless=0,crash_dump=1,rt_timing_frames=120,rt_scene_wait_log_frames=120,"
       "rt_debug=0,ls_debug=0,gi_debug=0,rf_debug=0,rr_show=0,rf_stats=0,ls_stats=0,ls_check=0,rt_stats=0,"
       "rt_primary_check=0,dump_images=1,dump_png_width=1280,dump_raw=0,dump_preview_f32=1,dump_shaders=0,pan_capture=0,pan_inputs=0,"
       "pan_prepare=0,sharc=0,"
       "qa_exposure_hold=0,rt_alpha_floor_yield=0,rt_capture_rise_reserve=1,rt_capture_wide_split=1,"
       "rt_mesh_budget_own=0,detailed_logging=1,rt_stats_period_frames=60," + HAIR)
# (label, key, small keep): the measured toggle windows, in order
WINDOWS = (("off-a", 0, 0.25), ("on-q", 1, 0.25), ("on-e", 1, 0.125), ("off-b", 0, 0.25))


def phase(label, seconds, pixel_lod, small_keep, wheel=None, frames=None, trace=False):
    p = {"label": label, "kind": "static", "speed": "static", "seconds": seconds, "repeat_ms": 0, "scan": 16,
         "capture_at": 0, "serial": 0, "motion_start": seconds, "motion_end": seconds, "pixels_per_second": 0,
         "direction": 0,
         "set": f"rt_hair_groom_primary=1,rt_hair_groom_nearest=1,rt_hair_pixel_lod={pixel_lod},"
                f"rt_hair_pixel_lod_small_keep={small_keep}"}
    if wheel:
        p["wheel"] = wheel
    if frames:   # a frame window keeps its length through live_flow's tightening (camera-plan: needs stats_reset)
        p.update(frames=frames, settle_frames=0, stats_reset=True, wait_stats=[], wait_log_timeout=30)
    if trace:    # one PNG trace of the frame (dump_images=1, 1280 px wide, no raw)
        p["trace_at"] = [2]
    return p


def make_plan():
    # Warm-up: every value the windows use before the first measured window (live_flow warm-up lint), then key off
    # before "lower" (hl1's wheel 10 to the zoomed-out view: heads 6-53 px in 200821). "settle": the arrivals that
    # zoom brings (hl1: late hair draws) end before the first window. Each "-settle" covers the 30-frame level vote, the
    # camera array upload and the first full camera BLAS builds (~2 s at 30 fps).
    plan = [phase("warmup", 20, 1, 0.125), phase("warm-q", 4, 1, 0.25), phase("warm-off", 4, 0, 0.25),
            phase("lower", 8, 0, 0.25, wheel=10), phase("settle", 6, 0, 0.25)]
    for label, key, small in WINDOWS:
        plan += [phase(f"pl-{label}-settle", 4, key, small), phase(f"pl-{label}", 4, key, small, frames=120),
                 phase(f"shot-{label}", 5, key, small, trace=True)]
    return plan


def make_job(plan):
    windows = [f"pl-{w[0]}" for w in WINDOWS]
    shots = [(i, p["label"]) for i, p in enumerate(plan) if p.get("trace_at")]
    return {
        "name": NAME,
        "save": "Water Queens House",
        "session": False,
        "traces": 0,
        "hold": 120,
        "timeout": 420,
        "max_minutes": 4,
        "repeat": 1,
        "live_batch": "hair-pixel-lod",
        "tree": ".claude/worktrees/test-main",
        "window_mode": "visible_unfocused_fg",
        "fail_fast": False,
        "stop_on_dump": False,
        "set": SET,
        "args": ["-RealPresent", "-Move", "-MovePlan", PLAN],
        "patterns": ["\\[auto\\]", "\\[cfg\\]", "\\[hot\\]", "\\[watch\\]", "\\[crash\\]", "FAILED",
                     "\\[hair primary\\]", "\\[hair groom\\]", "\\[rt\\] scene wait", "VK_ERROR", "\\[log\\]",
                     "\\[trace\\]"],
        "expect": [
            "\\[auto\\] scene ready",
            "\\[hot\\] core generation \\d+ loaded",
            "\\[cfg\\] settings read",
            "\\[trace\\] frame \\d+ written",
        ],
        "expect_every": [
            {"name": "strand G-buffer GPU ms in every toggle window (cost, report only)",
             "select": "\\[hair primary\\] strand G-buffer draws GPU", "match": "GPU [0-9.]+ ms/frame",
             "windows": windows, "period_frames": 60, "min": 1},
            {"name": "small keep 1/4: the period line with a projection read and some groom at camera level 1 or 2",
             "select": "\\[hair groom\\] pixel LOD \\(last",
             "match": "level 0/1/2/3/4/5 \\d+/(?:[1-9]\\d*/\\d+|\\d+/[1-9]\\d*)/\\d+/\\d+/\\d+;.*\\([1-9][0-9]* px per "
                      "radian, 0 groom frames without.*small keep 0\\.250\\)",
             "windows": ["pl-on-q"], "period_frames": 60, "min": 1},
            {"name": "small keep 1/8: the period line with some groom at camera level 1-3",
             "select": "\\[hair groom\\] pixel LOD \\(last",
             "match": "level 0/1/2/3/4/5 \\d+/(?:[1-9]\\d*/\\d+/\\d+|\\d+/[1-9]\\d*/\\d+|\\d+/\\d+/[1-9]\\d*)/\\d+/\\d+;"
                      ".*small keep 0\\.125\\)",
             "windows": ["pl-on-e"], "period_frames": 60, "min": 1},
        ],
        "forbid": [
            "\\[crash\\] exception 0x", "VK_ERROR_DEVICE_LOST", "\\[hot\\] core (rejected|faulted)",
            "FAILED, keeping previous", "compile call failed", "\\[log\\] reached \\d+ MB", "\\[sl\\] .*eWarnOutOfVRAM",
        ],
        "expect_runner": [
            "\\[window-mode\\].*visible_unfocused",
            rf"\[wheel-input\] phase={next(i for i, p in enumerate(plan) if p.get('wheel'))} notches=10\b",
        ] + [rf"\[motion-trace\] phase={i} label={label} repeat=0" for i, label in shots] + [
            f"\\[motion-plan\\] complete phases={len(plan)}\\b",
            "^Run ended: done",
        ],
        "live_flow": {"expected_fps": 30, "settings_latency_s": 1, "trace_stall_s": 3},
        "estimated_phase_seconds": sum(p["seconds"] for p in plan),
        "purpose": "rt_hair_pixel_lod for heads of 35-60 px (claude/hair-pixel-lod-small) at S8's zoomed-out view: key "
                   "off / on (small keep 1/4) / on (1/8, dev knob) / off in one launch, static camera; the period line "
                   "(groom frames per camera level), the strand G-buffer GPU ms and one PNG trace per window. STAGED, "
                   "not authorized.",
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
