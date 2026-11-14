"""hair_stable (docs/RTX_HAIR_PRIMARY.md 6g, 6h, 6i): STAGED, not queued, not authorized.

    py -3 -m tools.live_batches_staged.hair_stable.make_job

One Water Queens House (S8) launch for the RTX Hair On bundle of 6i (rt_hair_header_scene_pass inside it, a second
scene pass of a present interval with its own header and tag). The gated windows run the bundle alone: hl1's wheel
dolly in and out, hc1's orbit right and left, a short dolly with a PNG right after it. Then two A/B parts and the
control, each toggled inside the run: rt_hair_coverage_keep (6i, off: the last coverage verdict kept while a draw is
off the screen), the card fill for the visual record (still and right after a dolly, full-width PNGs), and last the
bundle without rt_hair_header_scene_pass (the state before 6h; its header pass line must show the labels the decal
pass took). The card fill (rt_hair_card_fill, specialization 1053) and the dense sampler (1052) are compiled in at
startup as diagnostics of the run; the fill's header flag is off in every gated window. predicted.json precedes the
job. The job lives outside every watched captures/queue/pending inbox.
"""
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))

NAME = "hx1-s8-hair-stable"
PLAN = f"tools/live_batches_staged/hair_stable/plans/{NAME}.json"
# The RTX Hair On bundle (common/settings.h exp_hair_groom_primary "on", 6i: with rt_hair_header_scene_pass).
HAIR = ("rt_hair_groom=1,rt_hair_groom_primary=1,rt_hair_groom_primary_order=1,rt_hair_groom_primary_current=1,"
        "rt_hair_primary_frame_tag=1,rt_hair_groom_lod_keep=1,rt_hair_primary_hold=1,rt_hair_tangent_world=1,"
        "rt_hair_groom_shadow_t=1,rt_hair_groom_ao_t=1,rt_hair_groom_cascade_drop=1,rt_hair_fibre=1,"
        "rt_hair_fibre_gi=1,rt_hair_fibre_spec=1,rt_hair_fibre_mapped=1,rr_hair_guides=1,"
        "rt_hair_physical_origin=1,rt_hair_capture=1,rt_hair_coverage_union=1,rt_hair_coverage_confirm=1,"
        "rt_hair_slot_stable=1,rt_hair_live_warmup=1,rt_hair_groom_nearest=1,rt_hair_header_cam_gpu=1,"
        "rt_hair_header_scene_pass=1,"
        # Not bundle keys. Compiled into the strand pipelines at their creation, so they must be on at startup for the
        # A/B parts: the card fill (6g; its header flag is toggled per phase, off in the gated windows) and the dense
        # sampler (6f, a diagnostic: the acceptance shares of the moving frames).
        "rt_hair_card_fill=1,rt_hair_dense_sample=1")
SET = ("preset=ultra,fg=3,headless=0,crash_dump=1,rt_timing_frames=120,rt_scene_wait_log_frames=120,"
       "rt_debug=0,ls_debug=0,gi_debug=0,rf_debug=0,rr_show=0,rf_stats=0,ls_stats=0,ls_check=0,rt_stats=0,"
       "rt_primary_check=0,dump_images=1,dump_png_width=2560,dump_raw=0,dump_shaders=0,pan_capture=0,pan_inputs=0,"
       "pan_prepare=0,sharc=0,"
       "qa_exposure_hold=0,rt_alpha_floor_yield=0,rt_capture_rise_reserve=1,rt_capture_wide_split=1,"
       "rt_mesh_budget_own=0,detailed_logging=1,rt_stats_period_frames=60," + HAIR)
# A dolly window is one stats period of 180 frames: the 10 wheel notches (150 ms apart, 1.7 s were not enough for them
# in hx1 180928's out-fill) end inside it at any frame rate from 30 to 90 fps. A shot dolly is 120 frames (pre-in and
# pre-out of hx1 190905 held their 10 notches in 120): the PNG of the next window is taken about a second after the
# last notch.
DOLLY_FRAMES, SHOT_FRAMES = 180, 120
DOLLY = dict(sample_frames=DOLLY_FRAMES, period_frames=DOLLY_FRAMES)
SHOT = dict(sample_frames=SHOT_FRAMES, period_frames=SHOT_FRAMES)
STILL = dict(period_frames=60)
# The bundle alone (fill flag 0, keep 0): the gated windows.
ON_DOLLY, ON_SHOT, ON_ORBIT, ON_STILL = ["in-on", "out-on"], ["in-shot", "out-shot"], ["orbit-r", "orbit-l"], ["still-on", "shot-on"]
KEEP = ["in-keep", "out-keep"]                        # + rt_hair_coverage_keep=1
FILL_SHOT, FILL_STILL = ["in-fill", "out-fill"], ["still-fill", "shot-fill"]   # + the card fill's header flag
BASE = ["in-base", "out-base"]                        # the bundle without rt_hair_header_scene_pass: the control
# select / match pairs
SWITCHES = "\\[hair primary\\] switches \\(last"
HEADER = "\\[hair primary\\] header pass \\(last"
# No switch to cards by any cause the bundle answers, no frame with every draw on cards, no frame under its bound.
# "coverage stale/none/invalidated" is left open here (hx1 190905: 1 in 3 of 7 moving windows with the key, a draw
# that left the screen for 28 frames: 6i) and closed in the windows with rt_hair_coverage_keep.
NO_LOSS = ("strands->cards \\(frame watchdog 0, key watchdog 0, coverage low 0, coverage high 0, "
           "coverage stale/none/invalidated \\d+, coverage latched 0, header not applied \\(camera gate, order\\) 0, "
           "live toggle 0, other 0\\), .* frames with hair draws, 0 with every draw on cards; "
           ".*frame watchdog trips 0 \\(.*key watchdog trips 0;")
NO_SWITCH = (": 0 strands->cards \\(frame watchdog 0, key watchdog 0, coverage low 0, coverage high 0, "
             "coverage stale/none/invalidated 0, coverage latched 0, header not applied \\(camera gate, order\\) 0, "
             "live toggle 0, other 0\\), .* frames with hair draws, 0 with every draw on cards; "
             ".*frame watchdog trips 0 \\(.*key watchdog trips 0;")
OWN_HEADER = ("rt_hair_header_scene_pass 1\\): applied at the scene G-buffer pass [1-9][0-9]*, at the decal pass 0 "
              ".*label taken by the decal pass 0 .*by another scene pass 0 ")


def phase(label, seconds, fill=0, scene=1, keep=0, wheel=None, frames=None, trace=None, rotate=0):
    p = {"label": label, "kind": "static", "speed": "static", "seconds": seconds, "repeat_ms": 0, "scan": 16,
         "capture_at": 0, "serial": 0, "motion_start": seconds, "motion_end": seconds,
         "pixels_per_second": 0, "direction": 0,
         "set": f"rt_hair_groom_primary=1,rt_hair_card_fill={fill},rt_hair_header_scene_pass={scene},"
                f"rt_hair_coverage_keep={keep},"
                # every phase names the stats period: a dolly window's 180 would stay for the windows after it
                "pan_inputs=0,pan_capture=0,rt_stats_period_frames=60"}
    if rotate:   # hc1's slow turn (60 px/s) from the first second to the end of the frame window
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
        # every value the measured windows use before the first one (live_flow warm-up lint): fill, key and keep 1 and 0
        phase("warmup", 20), phase("warm-ab", 3, fill=1, scene=0, keep=1), phase("warm-on", 3),
        # an unjudged dolly first: the near LODs of the zoom upload here (6 decodes of 47-94 ms in hx1 180928's first
        # dolly, 8 of 47-78 ms in 190905's)
        phase("pre-in", 4, wheel=10, frames=SHOT_FRAMES), phase("pre-out", 4, wheel=-10, frames=SHOT_FRAMES),
        # the bundle alone: hl1's dolly, hc1's orbit (the whole 180-frame window turns), the still image (PNG)
        phase("settle-on", 3), phase("in-on", 8, wheel=10, frames=DOLLY_FRAMES),
        phase("out-on", 8, wheel=-10, frames=DOLLY_FRAMES),
        phase("orbit-r", 8, rotate=1, frames=DOLLY_FRAMES), phase("orbit-l", 8, rotate=-1, frames=DOLLY_FRAMES),
        phase("still-on", 6, frames=60, trace=0.5),
        # a short dolly in and the PNG right after it (about a second after the last notch), then back out
        phase("in-shot", 5, wheel=10, frames=SHOT_FRAMES), phase("shot-on", 6, frames=60, trace=0.1),
        phase("out-shot", 5, wheel=-10, frames=SHOT_FRAMES),
        # 6i A/B: the last coverage verdict kept (a head back in view shows strands at once)
        phase("settle-keep", 3, keep=1), phase("in-keep", 8, keep=1, wheel=10, frames=DOLLY_FRAMES),
        phase("out-keep", 8, keep=1, wheel=-10, frames=DOLLY_FRAMES),
        # 6g, the visual record with the card fill: the same still image and the same shot after the same dolly
        phase("settle-fill", 3, fill=1), phase("still-fill", 6, fill=1, frames=60, trace=0.5),
        phase("in-fill", 5, fill=1, wheel=10, frames=SHOT_FRAMES), phase("shot-fill", 6, fill=1, frames=60, trace=0.1),
        phase("out-fill", 5, fill=1, wheel=-10, frames=SHOT_FRAMES),
        # last, the control in the same run: the bundle without the scene pass header (its cards end with the run)
        phase("settle-base", 3, scene=0), phase("in-base", 8, scene=0, wheel=10, frames=DOLLY_FRAMES),
        phase("out-base", 8, scene=0, wheel=-10, frames=DOLLY_FRAMES),
    ]


def every(name, select, match, windows, **kw):
    e = {"name": name, "select": select, "match": match, "min": 1, "windows": windows}
    e.update(kw)
    return e


def make_job(plan):
    wheels = [(i, p["wheel"]) for i, p in enumerate(plan) if p.get("wheel")]
    gated = [("dollies", ON_DOLLY, DOLLY), ("orbits", ON_ORBIT, DOLLY), ("shot dollies", ON_SHOT, SHOT),
             ("still", ON_STILL, STILL)]
    moving = [("dollies", ON_DOLLY + KEEP + BASE, DOLLY), ("shot dollies", ON_SHOT + FILL_SHOT, SHOT)]
    return {
        "name": NAME,
        "save": "Water Queens House",
        "session": False,
        "traces": 0,
        "hold": 120,
        "timeout": 480,
        "max_minutes": 6,
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
            # 6g: the card fill compiled into the strand pipelines, and both states of its header flag
            "\\[hair primary\\] G-buffer hair pipeline: .*card fill \\(specialization 1053\\) 1",
            "\\[hair primary\\] card fill: header flag 1 .*pipelines 1 .*draws its card; frame watchdog reports only",
            "\\[hair primary\\] card fill: header flag 0 .*is discarded; frame watchdog switches to cards",
            # 6h: both states of the header pass key ran; 6i: the layer counts second scene passes with their own header
            "\\[hair primary\\] header pass \\(last \\d+ frames, rt_hair_header_scene_pass 1\\)",
            "\\[hair primary\\] header pass \\(last \\d+ frames, rt_hair_header_scene_pass 0\\)",
            "\\[hair primary\\] header pass \\(last .*second scene passes of an interval with their own header and tag \\d+",
        ],
        "expect_every": [
            # 6h and 6i, the bundle alone: only the scene G-buffer pass applies the header, no scene pass runs on a
            # header the decal pass or another scene pass applied (predicted.json part1_bundle)
            *[every(f"every scene G-buffer pass has its own header: no label taken by the decal pass or by another "
                    f"scene pass ({what})", HEADER, OWN_HEADER, windows, **period)
              for what, windows, period in gated + [("keep dollies", KEEP, DOLLY)]],
            # the user's target with the bundle alone: strands in every frame, also under fast camera motion. No frame
            # under its bound, none with every draw on cards, no switch to cards by a watchdog, low or high coverage,
            # a latch, a header not applied or any other gate. Without the card fill a lost frame would be 28 frames
            # of cards.
            *[every(f"strands in every frame with the bundle alone, no switch to cards by a gate ({what})",
                    SWITCHES, NO_LOSS, windows, **period) for what, windows, period in gated],
            # 6i A/B: with the last verdict kept no key goes to cards at all, also not one that left the screen
            every("no strands->cards switch of any cause with rt_hair_coverage_keep (dollies)", SWITCHES, NO_SWITCH,
                  KEEP, **DOLLY),
            # the control: without the scene pass header the decal pass takes labels during the dolly (before 6h)
            every("without rt_hair_header_scene_pass the decal pass takes the label of scene G-buffer passes (dollies)",
                  HEADER, "rt_hair_header_scene_pass 0\\): .*label taken by the decal pass [1-9]", BASE, **DOLLY),
            # reported, not gated: the switches of the fill and control windows, the dropouts, the control's trips
            *[every(f"switches line of the fill and control windows (report; {what})", SWITCHES,
                    "\\d+ strands->cards \\(frame watchdog \\d+, .* \\d+ cards->strands; ", windows, min=0, **period)
              for what, windows, period in (("control dollies", BASE, DOLLY), ("fill shot dollies", FILL_SHOT, SHOT),
                                            ("fill still", FILL_STILL, STILL))],
            *[every(f"swapped draws under a tenth of their card pixels (report; {what})",
                    "\\[hair primary\\] dropouts \\(last",
                    "\\): \\d+ swapped G-buffer draws wrote under a tenth of their card pixels", windows, min=0, **period)
              for what, windows, period in moving],
            every("frame watchdog lines name the card period (control, report)",
                  "\\[hair primary\\] watchdog: G-buffer frame", "cards for (0|28) frames; build camera step -?[0-9.]+ m",
                  BASE, event=True, min=0),
            every("strands are drawn in every still window (swapped draws counted)",
                  "\\[hair primary\\] coverage \\(last", "\\d+ draws probed, [1-9][0-9]* swapped",
                  ON_STILL + FILL_STILL, **STILL),
            *[every(f"the rebase origin is 0 in every sampled frame (header camera copied on the GPU; {what})",
                    "\\[hair primary\\] rebase origin \\(last", " 0 with a non-zero origin .*rt_hair_header_cam_gpu 1",
                    windows, **period) for what, windows, period in moving],
            # 6f: the dense samples of the moving windows (acceptance and 'no strand' shares, moving against still)
            *[every(f"the dense sample period line, with sampled frames ({what})",
                    "\\[hair primary\\] dense samples \\(last",
                    "rt_hair_dense_sample 1\\): [1-9][0-9]* sampled frames, .* still: \\d+ samples: .* \\| moving .*: \\d+ samples: ",
                    windows, **period) for what, windows, period in moving + [("orbits", ON_ORBIT, DOLLY)]],
            every("strand G-buffer GPU ms with and without the fill (cost, report only)",
                  "\\[hair primary\\] strand G-buffer draws GPU", "GPU [0-9.]+ ms/frame",
                  ["still-on", "still-fill"], **STILL),
        ],
        "forbid": [
            "\\[crash\\] exception 0x", "VK_ERROR_DEVICE_LOST", "\\[hot\\] core (rejected|faulted)",
            "FAILED, keeping previous", "compile call failed", "\\[log\\] reached \\d+ MB", "\\[sl\\] .*eWarnOutOfVRAM",
        ],
        "expect_runner": [
            "\\[window-mode\\].*visible_unfocused",
        ] + [rf"\[wheel-input\] phase={i} notches={w}\b" for i, w in wheels] + [
            rf"\[rotation-input\] phase={i} " for i, p in enumerate(plan) if p["kind"] == "rotate"] + [
            f"\\[motion-plan\\] complete phases={len(plan)}\\b",
            "^Run ended: done",
        ],
        "live_flow": {"expected_fps": 30, "settings_latency_s": 1, "trace_stall_s": 3},
        "estimated_phase_seconds": sum(p["seconds"] for p in plan),
        "purpose": "6i: one launch, the RTX Hair On bundle with rt_hair_header_scene_pass inside it. With the bundle "
                   "alone (card fill flag off) every wheel dolly and orbit window must show: the header applied at "
                   "the scene G-buffer pass only, no label taken by the decal pass or by another scene pass, frame "
                   "watchdog trips 0, no frame on cards, no switch to cards by any gate. Then inside the same run: "
                   "rt_hair_coverage_keep (no switch to cards at all, also not for a draw that left the screen), the "
                   "card fill for the visual record (full-width PNGs, still and right after a dolly, next to the same "
                   "two without it), and last the control without rt_hair_header_scene_pass (the decal pass takes "
                   "labels). STAGED, not authorized.",
    }


def main():
    assert (HERE / "predicted.json").exists(), "predicted.json precedes the job"
    plan = make_plan()
    for folder, value in (("plans", plan), ("jobs", make_job(plan))):
        (HERE / folder).mkdir(exist_ok=True)
        (HERE / folder / f"{NAME}.json").write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8", newline="\n")
    from tools.live_batches.live_flow import tighten_files
    # The checked-in snapshot's frame rates (reproducible in any worktree). The live corpus of 2026-11-14 holds one
    # VALID S8 log (hx1 190905) whose two PNG trace windows (5.0 and 5.4 fps over the stall) are its tenth percentile:
    # every 180-frame window would be budgeted 33 s.
    tighten_files(HERE, job_names=[NAME], frozen=True)
    print(f"Generated {NAME}; no queue staging")


if __name__ == "__main__":
    main()
