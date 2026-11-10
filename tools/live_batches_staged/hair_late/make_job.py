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
        "rt_hair_slot_stable=1,rt_hair_live_warmup=1,rt_hair_groom_nearest=1,"
        # the fix under test (branch claude/hair-row-stable, 5w): not in the bundle
        "rt_hair_row_stable=1,"
        # 6c (branch claude/hair-holes-root): the header camera copied on the GPU by the build (the camera its points
        # were converted with) instead of the CPU read, which lags under motion (hc1 234025: origin exactly 0 with the
        # copy, 1-12 cm without; hf1 073932 zoom: 0.496 m). rt_hair_stale_publish compares two CPU reads: off here.
        "rt_hair_header_cam_gpu=1,rt_hair_stale_publish=0,"
        # part 2 (branch claude/hair-pixel-lod, 5v): off until its windows
        "rt_hair_pixel_lod=0,rt_hair_pixel_lod_k=1.0")
SET = ("preset=ultra,fg=3,headless=0,crash_dump=1,rt_timing_frames=120,rt_scene_wait_log_frames=120,"
       "rt_debug=0,ls_debug=0,gi_debug=0,rf_debug=0,rr_show=0,rf_stats=0,ls_stats=0,ls_check=0,rt_stats=0,"
       "rt_primary_check=0,dump_images=1,dump_png_width=1280,dump_raw=0,dump_shaders=0,pan_capture=0,pan_inputs=0,"
       "pan_prepare=0,sharc=0,"
       "qa_exposure_hold=0,rt_alpha_floor_yield=0,rt_capture_rise_reserve=1,rt_capture_wide_split=1,"
       "rt_mesh_budget_own=0,detailed_logging=1,rt_stats_period_frames=60," + HAIR)


# Part 2, rt_hair_pixel_lod (5v): PNG bursts of the heads (scene colour, no RR inputs) with the key off and on.
SERIALS = {"pl-off-png": 99310, "pl-on-png": 99311}


def phase(label, seconds, wheel=None, frames=None, pixel_lod=None, serial=0, capture_at=0):
    p = {"label": label, "kind": "static", "speed": "static", "seconds": seconds, "repeat_ms": 0, "scan": 16,
         "capture_at": capture_at, "serial": serial, "motion_start": seconds, "motion_end": seconds,
         "pixels_per_second": 0, "direction": 0, "set": "rt_hair_groom_primary=1,rt_hair_groom_nearest=1"}
    if pixel_lod is not None:   # part 2: the pan pipelines prepared before the bursts (hs1: compiled inside one)
        p["set"] += f",rt_hair_pixel_lod={pixel_lod},pan_prepare=1,pan_inputs=0,pan_edge=0,pan_delay_ms=0"
        p["set"] += f",pan_capture={serial}" if serial else ",pan_capture=0"
    if wheel:
        p["wheel"] = wheel
    if frames:   # a frame window keeps its length through live_flow's tightening (camera-plan: needs stats_reset)
        p.update(frames=frames, settle_frames=0, stats_reset=True, wait_stats=[], wait_log_timeout=30)
    return p


def make_plan():
    # 161106's first 28 s: warm-up 20 s, then "lower" (its phase start triggered the arrivals 0.06 s later), then 12 s
    # to watch the transient settle (161106: trips at frames 1992-1998, strands back after the 28-frame hold).
    # Part 2 (rt_hair_pixel_lod, 5v): the same zoomed-out (isometric) static camera after "watch": off, on, off again,
    # toggled in this run; each frame window resets the stats (the period line and the strand G-buffer gpu line per
    # 60-frame period); a PNG burst of the heads in a timed window after the off and the on window. The on settle covers
    # the 30-frame level vote, the camera array upload and the first full camera BLAS builds (~2 s at 30 fps).
    warm_off = phase("warm-off", 4)   # part 1 runs with the key off and no pan preparation, as before
    warm_off["set"] += ",rt_hair_pixel_lod=0,pan_prepare=0"
    return [
        # every value the part 2 windows use, before the first measured window (live_flow warm-up lint): the key on and
        # the pan pipelines prepared during the warm-up, then off again before "lower"
        phase("warmup", 20, pixel_lod=1),
        warm_off,
        phase("lower", 8, wheel=10, frames=240),
        phase("watch", 12, frames=360),
        phase("pl-settle", 4, pixel_lod=0),
        phase("pl-off", 6, frames=180, pixel_lod=0),
        phase("pl-off-png", 6, pixel_lod=0, serial=SERIALS["pl-off-png"], capture_at=1.0),
        phase("pl-on-settle", 4, pixel_lod=1),
        phase("pl-on", 6, frames=180, pixel_lod=1),
        phase("pl-on-png", 6, pixel_lod=1, serial=SERIALS["pl-on-png"], capture_at=1.0),
        phase("pl-off2", 6, frames=180, pixel_lod=0),
    ]


def make_job(plan):
    return {
        "name": NAME,
        "save": "Water Queens House",
        "session": False,
        "traces": 0,
        "hold": 120,
        "timeout": 420,
        "max_minutes": 4,
        "repeat": 1,
        "live_batch": "hair-late",
        "tree": ".claude/worktrees/test-main",
        "window_mode": "visible_unfocused_fg",
        "fail_fast": False,
        "stop_on_dump": False,
        "set": SET,
        "args": ["-RealPresent", "-Move", "-MovePlan", PLAN],
        "patterns": ["\\[auto\\]", "\\[cfg\\]", "\\[hot\\]", "\\[watch\\]", "\\[crash\\]", "FAILED",
                     "\\[hair primary\\]", "\\[hair groom\\]", "\\[rt\\] scene wait", "VK_ERROR", "\\[log\\]",
                     "\\[pan\\]"],
        "expect": [
            "\\[auto\\] scene ready",
            "\\[hot\\] core generation \\d+ loaded",
            "\\[cfg\\] settings read",
            # Q1: the nearest-strand trace compiled in and its header flag on
            "\\[hair primary\\] G-buffer hair pipeline: .*nearest-strand trace compiled in \\(specialization 1047\\) 1",
            "\\[hair primary\\] nearest-strand trace: header flag 1 .*specialization 1047 in the strand G-buffer pipelines 1",
        ] + [rf"\[pan\] burst {s} complete: \d+ frames, \d+ samples, transport ok" for s in SERIALS.values()],
        # The observables, as events in their windows (not startup producers: the arrival is what the phase change
        # triggers): the first-seen lines in "lower", the TLAS check period line in "lower" and "watch".
        "expect_every": [
            {"name": "the arrival: 45252 waits for its UV join in the lower window",
             "select": "\\[hair groom\\] unmatched hair draw: 45252 indices", "match": "UV join pending",
             "windows": ["lower"], "event": True, "min": 1},
            {"name": "the first-seen frame lines in the lower window",
             "select": "\\[hair groom\\] frame \\d+ \\(a hair draw first seen",
             "match": "groom rows \\d+ \\(previous publish \\d+\\)", "windows": ["lower"], "event": True, "min": 1},
            # 5w (rt_hair_row_stable=1): hl1 164930 had 11, 7, 4, 4, 0, 7 ... here; every line must say 0.
            {"name": "no keyed row holds another key than before (rt_hair_row_stable)",
             "select": "\\[hair groom\\] frame \\d+ \\(a hair draw first seen",
             "match": "holds another key than before 0 \\(", "windows": ["lower"], "event": True, "min": 1},
            # 5x (branch claude/hair-pool-reuse): candidate D (pool reuse) is disproved by 180301/180949; every trip line
            # names the build camera step of its frame (the zoom candidate). Trips are not required (min 0).
            {"name": "every frame watchdog line in lower and watch names the build camera step",
             "select": "\\[hair primary\\] watchdog: G-buffer frame",
             # 5z (branch claude/hair-cam-step): and the publish it applied, with the VP eye offsets (predicted.json)
             "match": "build camera step -?[0-9.]+ m at that frame.*applied the publish of build frame \\d+ .*VP eye -?[0-9.]+ m",
             "windows": ["lower", "watch"], "event": True,
             "min": 0},
            # 6c (rt_hair_header_cam_gpu=1): no frame watchdog trip in the zoom or after it (the six runs without the key
            # had 4-6 in lower), judged on every complete 'header safety' period line (frame windows reset the stats).
            {"name": "no frame watchdog trip in lower and watch, header camera copied on the GPU",
             "select": "\\[hair primary\\] header safety \\(last",
             "match": ", watchdog trips 0, .* [1-9][0-9]* header cameras copied on the GPU",
             "windows": ["lower", "watch"], "period_frames": 60, "min": 1},
            {"name": "the rebase origin is 0 in every sampled frame of lower and watch (GPU sample, no label)",
             "select": "\\[hair primary\\] rebase origin \\(last",
             "match": " 0 with a non-zero origin .*rt_hair_header_cam_gpu 1",
             "windows": ["lower", "watch"], "period_frames": 60, "min": 1},
            {"name": "the groom TLAS check period line in both windows",
             "select": "\\[hair primary\\] groom TLAS check \\(last", "match": "G-buffer submits checked",
             "windows": ["watch"], "event": True, "min": 1},
            # Part 2 (rt_hair_pixel_lod): the strand G-buffer cost in each toggle window, the period line while on.
            {"name": "strand G-buffer GPU ms in the off, on and off-again windows (cost, report only)",
             "select": "\\[hair primary\\] strand G-buffer draws GPU", "match": "GPU [0-9.]+ ms/frame",
             "windows": ["pl-off", "pl-on", "pl-off2"], "period_frames": 60, "min": 1},
            {"name": "the pixel LOD period line in the on window, with a projection read",
             "select": "\\[hair groom\\] pixel LOD \\(last", "match": "\\([1-9][0-9]* px per radian, 0 groom frames without",
             "windows": ["pl-on"], "period_frames": 60, "min": 1},
        ],
        "forbid": [
            "\\[crash\\] exception 0x", "VK_ERROR_DEVICE_LOST", "\\[hot\\] core (rejected|faulted)",
            "FAILED, keeping previous", "compile call failed", "\\[log\\] reached \\d+ MB", "\\[sl\\] .*eWarnOutOfVRAM",
        ],
        "expect_runner": [
            "\\[window-mode\\].*visible_unfocused",
            # the wheel phase's index in the plan (warm-off moved "lower" to 2: run 180301 was INVALID on phase=1)
            rf"\[wheel-input\] phase={next(i for i, p in enumerate(plan) if p.get('wheel'))} notches=10\b",
            f"\\[motion-plan\\] complete phases={len(plan)}\\b",
            "^Run ended: done",
        ],
        "live_flow": {"expected_fps": 30, "settings_latency_s": 1, "trace_stall_s": 3},
        "estimated_phase_seconds": sum(p["seconds"] for p in plan),
        "purpose": "6c: rt_hair_header_cam_gpu on (header camera from the GPU, the origin of the groom points), 0 frame "
                   "watchdog trips in the zoom (lower) and after it (watch), rebase origin 0 in every sampled frame. "
                   "Natural late hair arrival at S8 (161106's 'lower' phase change), nearest-strand trace on, with the "
                   "groom TLAS check and the first-seen frame lines: which candidate breaks the existing keys' strands. "
                   "Then, same launch, rt_hair_pixel_lod off/on/off at the static zoomed-out camera: period line, strand "
                   "G-buffer GPU ms, PNG bursts of the heads. STAGED, not authorized.",
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
