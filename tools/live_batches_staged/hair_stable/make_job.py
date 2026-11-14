"""hair_stable (branch claude/hair-stable, docs/RTX_HAIR_PRIMARY.md 6g): STAGED, not queued, not authorized.

    py -3 -m tools.live_batches_staged.hair_stable.make_job

One Water Queens House (S8) launch that qualifies the RTX Hair state of 2026-11-14: the On bundle with the header
camera copied on the GPU (rt_hair_header_cam_gpu, now inside the bundle), the per-pixel card fill (rt_hair_card_fill,
specialization 1053, compiled in at startup and toggled by its header flag inside the run) and the dense sampler
(rt_hair_dense_sample, 6f) for the cause of the strand loss that remains under a fast dolly. hl1's wheel zoom (10
notches in, then out) with the fill on, the zoom in again with the fill off (the state of hl1 003637), then hc1's slow
orbit with the fill on. predicted.json precedes the job. The job lives outside every watched captures/queue/pending inbox.
"""
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))

NAME = "hx1-s8-hair-stable"
PLAN = f"tools/live_batches_staged/hair_stable/plans/{NAME}.json"
# The RTX Hair On bundle (common/settings.h exp_hair_groom_primary "on", 2026-11-14: with rt_hair_header_cam_gpu).
HAIR = ("rt_hair_groom=1,rt_hair_groom_primary=1,rt_hair_groom_primary_order=1,rt_hair_groom_primary_current=1,"
        "rt_hair_primary_frame_tag=1,rt_hair_groom_lod_keep=1,rt_hair_primary_hold=1,rt_hair_tangent_world=1,"
        "rt_hair_groom_shadow_t=1,rt_hair_groom_ao_t=1,rt_hair_groom_cascade_drop=1,rt_hair_fibre=1,"
        "rt_hair_fibre_gi=1,rt_hair_fibre_spec=1,rt_hair_fibre_mapped=1,rr_hair_guides=1,"
        "rt_hair_physical_origin=1,rt_hair_capture=1,rt_hair_coverage_union=1,rt_hair_coverage_confirm=1,"
        "rt_hair_slot_stable=1,rt_hair_live_warmup=1,rt_hair_groom_nearest=1,rt_hair_header_cam_gpu=1,"
        # the user's play state (High and Ultra): card motion on every strand pixel
        "rt_hair_mv_card=2,"
        # 6g: the card fill, compiled into the strand pipelines at their creation; the header flag toggles it per phase
        "rt_hair_card_fill=1,"
        # 6f: the dense sampler (specialization 1052), read at pipeline creation
        "rt_hair_dense_sample=1,rt_hair_stale_publish=0,rt_hair_pixel_lod=0")
SET = ("preset=ultra,fg=3,headless=0,crash_dump=1,rt_timing_frames=120,rt_scene_wait_log_frames=120,"
       "rt_debug=0,ls_debug=0,gi_debug=0,rf_debug=0,rr_show=0,rf_stats=0,ls_stats=0,ls_check=0,rt_stats=0,"
       "rt_primary_check=0,dump_images=1,dump_png_width=1280,dump_raw=0,dump_shaders=0,pan_capture=0,pan_inputs=0,"
       "pan_prepare=0,sharc=0,"
       "qa_exposure_hold=0,rt_alpha_floor_yield=0,rt_capture_rise_reserve=1,rt_capture_wide_split=1,"
       "rt_mesh_budget_own=0,detailed_logging=1,rt_stats_period_frames=60," + HAIR)
FILL = ["in-fill", "watch-fill", "out-fill", "orbit-r", "orbit-l"]
ZOOMS = ["in-fill", "out-fill", "in-off"]


def phase(label, seconds, fill=1, wheel=None, frames=None, trace=None, rotate=0):
    p = {"label": label, "kind": "static", "speed": "static", "seconds": seconds, "repeat_ms": 0, "scan": 16,
         "capture_at": 0, "serial": 0, "motion_start": seconds, "motion_end": seconds,
         "pixels_per_second": 0, "direction": 0,
         "set": f"rt_hair_groom_primary=1,rt_hair_card_fill={fill},pan_inputs=0,pan_capture=0"}
    if rotate:   # hc1's slow turn (60 px/s, motion from 1 s to seconds - 1)
        p.update(kind="rotate", speed="slow", motion_start=1, motion_end=seconds - 1, pixels_per_second=60,
                 direction=rotate)
    if wheel:
        p["wheel"] = wheel
    if frames:   # a frame window keeps its length through live_flow's tightening (camera-plan: needs stats_reset)
        p.update(frames=frames, settle_frames=0, stats_reset=True, wait_stats=[], wait_log_timeout=30)
    if trace is not None:   # one PNG trace (dump_images=1, 1280 px, no raw)
        p["trace_at"] = [trace]
    return p


def make_plan():
    return [
        # every value the measured windows use before the first one (live_flow warm-up lint): fill 1, 0, 1
        phase("warmup", 20), phase("warm-off", 3, fill=0), phase("warm-on", 3),
        # hl1's dolly with the fill: in (the camera lowers), the transient after it, out again
        phase("in-fill", 8, wheel=10, frames=240, trace=0.9), phase("watch-fill", 12, frames=360),
        phase("out-fill", 8, wheel=-10, frames=240),
        # the same dolly without the fill (the state of hl1 003637), toggled inside this run
        phase("settle-off", 3, fill=0), phase("in-off", 8, fill=0, wheel=10, frames=240, trace=0.9),
        phase("watch-off", 8, fill=0, frames=240),
        # hc1's slow orbit with the fill, right and back left
        phase("settle-on", 3), phase("orbit-r", 10, rotate=1), phase("orbit-l", 10, rotate=-1),
    ]


def every(name, select, match, windows, **kw):
    e = {"name": name, "select": select, "match": match, "min": 1, "windows": windows}
    e.update(kw)
    return e


def make_job(plan):
    wheels = [(i, p["wheel"]) for i, p in enumerate(plan) if p.get("wheel")]
    return {
        "name": NAME,
        "save": "Water Queens House",
        "session": False,
        "traces": 0,
        "hold": 120,
        "timeout": 480,
        "max_minutes": 5,
        "repeat": 1,
        "live_batch": "hair-stable",
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
            "\\[hair primary\\] G-buffer hair pipeline: .*nearest-strand trace compiled in \\(specialization 1047\\) 1",
            "\\[hair primary\\] G-buffer hair pipeline: .*dense samples \\(specialization 1052\\) 1",
            # 6g: the card fill compiled into the strand pipelines, and running with its header flag
            "\\[hair primary\\] G-buffer hair pipeline: .*card fill \\(specialization 1053\\) 1",
            "\\[hair primary\\] card fill: header flag 1 .*pipelines 1 .*draws its card; frame watchdog reports only",
            "\\[hair primary\\] card fill: header flag 0 .*is discarded; frame watchdog switches to cards",
        ],
        "expect_every": [
            # 6g gate: with the fill no head goes to cards for a lost frame (predicted.json part1_card_fill)
            every("no frame watchdog, key watchdog or coverage low switch while the card fill runs",
                  "\\[hair primary\\] switches \\(last",
                  "strands->cards \\(frame watchdog 0, key watchdog 0, coverage low 0,", FILL, period_frames=60),
            every("strands are drawn in every window (swapped draws counted)",
                  "\\[hair primary\\] coverage \\(last", "\\d+ draws probed, [1-9][0-9]* swapped",
                  ["watch-fill", "watch-off"], period_frames=60),
            # reported, not gated: the trips of each dolly (in-off: 5-7 as hl1 003637; fill windows: 'cards for 0 frames')
            every("frame watchdog lines of the dollies name the card period (0 with the fill, 28 without)",
                  "\\[hair primary\\] watchdog: G-buffer frame", "cards for (0|28) frames; build camera step -?[0-9.]+ m",
                  ZOOMS, event=True, min=0),
            every("the rebase origin is 0 in every sampled frame (header camera copied on the GPU)",
                  "\\[hair primary\\] rebase origin \\(last", " 0 with a non-zero origin .*rt_hair_header_cam_gpu 1",
                  ZOOMS + ["watch-fill", "watch-off"], period_frames=60),
            # 6f: the dense samples decide the cause of the remaining loss (predicted.json part2_remaining_loss)
            every("the dense sample period line in the dollies, with sampled frames",
                  "\\[hair primary\\] dense samples \\(last",
                  "rt_hair_dense_sample 1\\): [1-9][0-9]* sampled frames, .* still: \\d+ samples: .* \\| moving .*: \\d+ samples: ",
                  ZOOMS, period_frames=60),
            every("dense sample lines of the dolly's moving frames",
                  "\\[hair primary\\] dense sample: G-buffer frame",
                  "origin -?[0-9.]+ m, ray against view x projection [0-9.]+ mrad, card [0-9.]+ m, hit -?[0-9.]+ m, .*"
                  "anchor moved -?[0-9.]+ m in the world .* camera step [0-9.]+ m", ["in-fill", "in-off"], event=True),
            every("strand G-buffer GPU ms with and without the fill (cost, report only)",
                  "\\[hair primary\\] strand G-buffer draws GPU", "GPU [0-9.]+ ms/frame",
                  ["watch-fill", "watch-off"], period_frames=60),
        ],
        "forbid": [
            "\\[crash\\] exception 0x", "VK_ERROR_DEVICE_LOST", "\\[hot\\] core (rejected|faulted)",
            "FAILED, keeping previous", "compile call failed", "\\[log\\] reached \\d+ MB", "\\[sl\\] .*eWarnOutOfVRAM",
        ],
        "expect_runner": [
            "\\[window-mode\\].*visible_unfocused",
        ] + [rf"\[wheel-input\] phase={i} notches={w}\b" for i, w in wheels] + [
            f"\\[motion-plan\\] complete phases={len(plan)}\\b",
            "^Run ended: done",
        ],
        "live_flow": {"expected_fps": 30, "settings_latency_s": 1, "trace_stall_s": 3},
        "estimated_phase_seconds": sum(p["seconds"] for p in plan),
        "purpose": "6g: one launch for the RTX Hair state of 2026-11-14. Card fill on (rt_hair_card_fill, specialization "
                   "1053): under hl1's wheel dolly in and out and hc1's slow orbit no head switches to cards (switches "
                   "line: frame watchdog 0, key watchdog 0, coverage low 0 in every period) and the PNG of the dolly "
                   "shows no hole; the same dolly with the fill off in the same run is the comparison (hl1 003637: 5-7 "
                   "trips). The dense samples of both dollies name the cause of the strand loss that remains with the "
                   "header camera copied on the GPU (predicted.json part2: P1-P6). STAGED, not authorized.",
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
