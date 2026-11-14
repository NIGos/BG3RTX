"""hair_stable (docs/RTX_HAIR_PRIMARY.md 6g, 6h): STAGED, not queued, not authorized.

    py -3 -m tools.live_batches_staged.hair_stable.make_job

One Water Queens House (S8) launch for the RTX Hair state of 2026-11-14 (the On bundle with the header camera copied on
the GPU) and the fix of 6h: rt_hair_header_scene_pass (the strand header applied only at the scene G-buffer pass, never at
the decal pass). The key is toggled inside the run. hl1's wheel dolly in and out three times: with the fix and without
the card fill (the gate: strands in every moving frame), with the fix and the fill, and last without the fix (the state
of hx1 180928; its header pass line must show the labels the decal pass took). Then hc1's slow orbit. The card fill
(rt_hair_card_fill, specialization 1053) and the dense sampler (1052) are compiled in at startup; the fill is toggled by
its header flag. predicted.json precedes the job. The job lives outside every watched captures/queue/pending inbox.
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
       "rt_primary_check=0,dump_images=1,dump_png_width=2560,dump_raw=0,dump_shaders=0,pan_capture=0,pan_inputs=0,"
       "pan_prepare=0,sharc=0,"
       "qa_exposure_hold=0,rt_alpha_floor_yield=0,rt_capture_rise_reserve=1,rt_capture_wide_split=1,"
       "rt_mesh_budget_own=0,detailed_logging=1,rt_stats_period_frames=60," + HAIR)
FIX = ["in-fix", "out-fix", "in-fill", "out-fill"]          # rt_hair_header_scene_pass=1, the camera dollies
FILL = ["in-fill", "watch-fill", "out-fill", "orbit-r", "orbit-l"]   # rt_hair_card_fill=1 (with the fix)
BASE = ["in-base", "out-base"]                              # rt_hair_header_scene_pass=0, fill off: hx1 180928's state
ZOOMS = FIX + BASE
# A dolly window is one stats period of 180 frames: the 10 wheel notches (150 ms apart, 1.7 s were not enough for them
# in hx1 180928's out-fill) end inside it at any frame rate from 30 to 90 fps.
DOLLY_FRAMES = 180
DOLLY = dict(sample_frames=DOLLY_FRAMES, period_frames=DOLLY_FRAMES)


def phase(label, seconds, fill=1, scene=1, wheel=None, frames=None, trace=None, rotate=0):
    p = {"label": label, "kind": "static", "speed": "static", "seconds": seconds, "repeat_ms": 0, "scan": 16,
         "capture_at": 0, "serial": 0, "motion_start": seconds, "motion_end": seconds,
         "pixels_per_second": 0, "direction": 0,
         "set": f"rt_hair_groom_primary=1,rt_hair_card_fill={fill},rt_hair_header_scene_pass={scene},"
                # every phase names the stats period: a dolly window's 180 would stay for the windows after it
                "pan_inputs=0,pan_capture=0,rt_stats_period_frames=60"}
    if rotate:   # hc1's slow turn (60 px/s, motion from 1 s to seconds - 1)
        p.update(kind="rotate", speed="slow", motion_start=1, motion_end=seconds - 1, pixels_per_second=60,
                 direction=rotate)
    if wheel:
        p["wheel"] = wheel
    if frames:   # a frame window keeps its length through live_flow's tightening (camera-plan: needs stats_reset)
        p.update(frames=frames, settle_frames=0, stats_reset=True, wait_stats=[], wait_log_timeout=30)
    if trace is not None:   # one PNG trace (dump_images=1, full width, no raw): never inside a dolly (it stalls 2.5 s)
        p["trace_at"] = [trace]
    return p


def make_plan():
    return [
        # every value the measured windows use before the first one (live_flow warm-up lint): fill and fix 1, 0, 1
        phase("warmup", 20), phase("warm-off", 3, fill=0, scene=0), phase("warm-on", 3),
        # an unjudged dolly first: the near LODs of the zoom upload here (6 decodes of 47-94 ms in hx1 180928's first
        # dolly, each a hitch with two G-buffer frames in one present interval)
        phase("pre-in", 4, wheel=10, frames=120), phase("pre-out", 4, wheel=-10, frames=120),
        # the fix without the card fill: every lost strand would be a hole and trip the frame watchdog
        phase("settle-fix", 3, fill=0), phase("in-fix", 8, fill=0, wheel=10, frames=DOLLY_FRAMES),
        phase("out-fix", 8, fill=0, wheel=-10, frames=DOLLY_FRAMES),
        # the fix with the card fill; the still image with the fill (PNG) between the dollies
        phase("settle-fill", 3), phase("in-fill", 8, wheel=10, frames=DOLLY_FRAMES),
        phase("watch-fill", 6, frames=60, trace=0.5), phase("out-fill", 8, wheel=-10, frames=DOLLY_FRAMES),
        # hc1's slow orbit with the fix and the fill, right and back left
        phase("orbit-r", 10, rotate=1), phase("orbit-l", 10, rotate=-1),
        # the still image without the fill (PNG, cost)
        phase("settle-off", 3, fill=0), phase("watch-off", 6, fill=0, frames=60, trace=0.5),
        # last, the state of hx1 180928 in the same run: no fix, no fill (its frame watchdog cards end with the run)
        phase("settle-base", 3, fill=0, scene=0), phase("in-base", 8, fill=0, scene=0, wheel=10, frames=DOLLY_FRAMES),
        phase("out-base", 8, fill=0, scene=0, wheel=-10, frames=DOLLY_FRAMES),
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
            # 6h: both states of the header pass key ran
            "\\[hair primary\\] header pass \\(last \\d+ frames, rt_hair_header_scene_pass 1\\)",
            "\\[hair primary\\] header pass \\(last \\d+ frames, rt_hair_header_scene_pass 0\\)",
        ],
        "expect_every": [
            # 6h, the fix: with the key only the scene G-buffer pass applies the header and no scene pass runs on a
            # header the decal pass applied (predicted.json part1_header_pass)
            every("the header is applied at the scene G-buffer pass only, no label taken by the decal pass (dollies)",
                  "\\[hair primary\\] header pass \\(last",
                  "rt_hair_header_scene_pass 1\\): applied at the scene G-buffer pass [1-9][0-9]*, at the decal pass 0 "
                  ".*label taken by the decal pass 0 ", FIX, **DOLLY),
            every("the header is applied at the scene G-buffer pass only, no label taken by the decal pass (still, orbit)",
                  "\\[hair primary\\] header pass \\(last",
                  "rt_hair_header_scene_pass 1\\): applied at the scene G-buffer pass [1-9][0-9]*, at the decal pass 0 "
                  ".*label taken by the decal pass 0 ", ["watch-fill", "orbit-r", "orbit-l", "watch-off"], period_frames=60),
            # 6h, the cause: without the key the decal pass takes labels during the dolly (hx1 180928's state)
            every("without the key the decal pass takes the label of scene G-buffer passes during the dolly",
                  "\\[hair primary\\] header pass \\(last",
                  "rt_hair_header_scene_pass 0\\): .*label taken by the decal pass [1-9]", BASE, **DOLLY),
            # the user's target with the fix: strands in every frame of the dolly. No frame of the window under its
            # bound (frame watchdog trips 0), no frame with every draw on cards, no switch to cards by a watchdog or a
            # low coverage. Without the card fill (in-fix, out-fix) a lost frame would also be 28 frames of cards.
            every("strands in every frame of the dolly with the fix (no frame under its bound, none on cards)",
                  "\\[hair primary\\] switches \\(last",
                  "strands->cards \\(frame watchdog 0, key watchdog 0, coverage low 0,.* frames with hair draws, 0 with "
                  "every draw on cards; .*frame watchdog trips 0 \\(", FIX, **DOLLY),
            # reported, not gated (a draw first seen in the window has no card marker yet: 30 of hx1 180928 in-fill 64)
            every("swapped draws under a tenth of their card pixels in the dollies (report)",
                  "\\[hair primary\\] dropouts \\(last",
                  "\\): \\d+ swapped G-buffer draws wrote under a tenth of their card pixels", ZOOMS, min=0, **DOLLY),
            # 6g gate: with the fill no head goes to cards for a lost frame (predicted.json part2_card_fill)
            every("no frame watchdog, key watchdog or coverage low switch while the card fill runs",
                  "\\[hair primary\\] switches \\(last",
                  "strands->cards \\(frame watchdog 0, key watchdog 0, coverage low 0,",
                  ["watch-fill", "orbit-r", "orbit-l"], period_frames=60),
            every("strands are drawn in every still window (swapped draws counted)",
                  "\\[hair primary\\] coverage \\(last", "\\d+ draws probed, [1-9][0-9]* swapped",
                  ["watch-fill", "watch-off"], period_frames=60),
            # reported, not gated: the baseline's trips (hx1 180928: 6-12 a dolly) and their card period
            every("frame watchdog lines name the card period (0 with the fill, 28 without)",
                  "\\[hair primary\\] watchdog: G-buffer frame", "cards for (0|28) frames; build camera step -?[0-9.]+ m",
                  ZOOMS, event=True, min=0),
            every("the rebase origin is 0 in every sampled frame (header camera copied on the GPU)",
                  "\\[hair primary\\] rebase origin \\(last", " 0 with a non-zero origin .*rt_hair_header_cam_gpu 1",
                  ZOOMS, **DOLLY),
            # 6f: the dense samples of each dolly (the moving side's reasons, with and without the fix)
            every("the dense sample period line in the dollies, with sampled frames",
                  "\\[hair primary\\] dense samples \\(last",
                  "rt_hair_dense_sample 1\\): [1-9][0-9]* sampled frames, .* still: \\d+ samples: .* \\| moving .*: \\d+ samples: ",
                  ZOOMS, **DOLLY),
            # hx1 180928: 74 of 600 lines had 'camera step -1.000 m' (no pair: the frame after a tag gap); the sign is allowed
            every("dense sample lines of the dolly's moving frames",
                  "\\[hair primary\\] dense sample: G-buffer frame",
                  "origin -?[0-9.]+ m, ray against view x projection [0-9.]+ mrad, card [0-9.]+ m, hit -?[0-9.]+ m, .*"
                  "anchor moved -?[0-9.]+ m in the world .* camera step -?[0-9.]+ m", ["in-fix", "in-base"], event=True),
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
        "purpose": "6h: one launch. rt_hair_header_scene_pass (the strand header applied only at the scene G-buffer "
                   "pass, never at the decal pass) toggled inside the run under hl1's wheel dolly in and out. With the "
                   "key and without the card fill: no label taken by the decal pass and strands in every frame (frame "
                   "watchdog trips 0, no frame on cards, no draw under a tenth of its card pixels). With the key and "
                   "the fill: the same, then the still image (PNG) and hc1's slow orbit. Last without the key: the "
                   "header pass line shows the labels the decal pass took (the cause of hx1 180928's loss). STAGED, "
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
