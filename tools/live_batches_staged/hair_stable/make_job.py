"""hair_stable (docs/RTX_HAIR_PRIMARY.md 6g to 6k): STAGED, not queued, not authorized.

    py -3 -m tools.live_batches_staged.hair_stable.make_job

One Water Queens House (S8) launch for the RTX Hair On bundle of 6k, exactly as the launcher writes it
(rt_hair_header_cam_gpu, rt_hair_header_scene_pass and rt_hair_coverage_keep inside it) and nothing else that changes
a pipeline or a decision: no card fill, no dense sampler. The gated windows run the bundle: hl1's wheel dolly in and
out, hc1's orbit right and left, and the dolly again after the A/B. The A/B is rt_hair_coverage_keep off, toggled
inside the run: it restores the 10 card frames of a head that enters the view (5 with keep in hx1 194658, 3 or 4
predicted with 6k's handshake). The fill windows, the PNG pairs and the control without rt_hair_header_scene_pass of
the 194658 job are gone (decided or reproduced twice, predicted.json "dropped_since_194658"). predicted.json precedes
the job. The job lives outside every watched captures/queue/pending inbox.
"""
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))

NAME = "hx1-s8-hair-stable"
PLAN = f"tools/live_batches_staged/hair_stable/plans/{NAME}.json"
# The RTX Hair On bundle (common/settings.h exp_hair_groom_primary "on", 6k: with rt_hair_coverage_keep). main()
# checks it against data/settings_schema.json.
HAIR = ("rt_hair_groom=1,rt_hair_groom_primary=1,rt_hair_groom_primary_order=1,rt_hair_groom_primary_current=1,"
        "rt_hair_primary_frame_tag=1,rt_hair_groom_lod_keep=1,rt_hair_primary_hold=1,rt_hair_tangent_world=1,"
        "rt_hair_groom_shadow_t=1,rt_hair_groom_ao_t=1,rt_hair_groom_cascade_drop=1,rt_hair_fibre=1,"
        "rt_hair_fibre_gi=1,rt_hair_fibre_spec=1,rt_hair_fibre_mapped=1,rr_hair_guides=1,"
        "rt_hair_physical_origin=1,rt_hair_capture=1,rt_hair_coverage_union=1,rt_hair_coverage_confirm=1,"
        "rt_hair_slot_stable=1,rt_hair_live_warmup=1,rt_hair_groom_nearest=1,rt_hair_header_cam_gpu=1,"
        "rt_hair_header_scene_pass=1,rt_hair_coverage_keep=1,"
        # claude/hair-async: applied overnight 2026-11-21, confirm or revert (tools/live_batches_staged/hair_async)
        "rt_hair_groom_async=1")
SET = ("preset=ultra,fg=3,headless=0,crash_dump=1,rt_timing_frames=120,rt_scene_wait_log_frames=120,"
       "rt_debug=0,ls_debug=0,gi_debug=0,rf_debug=0,rr_show=0,rf_stats=0,ls_stats=0,ls_check=0,rt_stats=0,"
       "rt_primary_check=0,dump_images=1,dump_png_width=2560,dump_raw=0,dump_shaders=0,pan_capture=0,pan_inputs=0,"
       "pan_prepare=0,sharc=0,"
       "qa_exposure_hold=0,rt_alpha_floor_yield=0,rt_capture_rise_reserve=1,rt_capture_wide_split=1,"
       "rt_mesh_budget_own=0,detailed_logging=1,rt_stats_period_frames=60," + HAIR)
# A dolly window is one stats period of 180 frames: the 10 wheel notches (150 ms apart, 1.7 s were not enough for them
# in hx1 180928's out-fill) end inside it at any frame rate from 30 to 90 fps. The unjudged first dolly is 120 frames
# (pre-in and pre-out of hx1 190905 held their 10 notches in 120).
DOLLY_FRAMES, PRE_FRAMES = 180, 120
DOLLY = dict(sample_frames=DOLLY_FRAMES, period_frames=DOLLY_FRAMES)
ON_DOLLY, ON_ORBIT, ON_AGAIN = ["in-on", "out-on"], ["orbit-r", "orbit-l"], ["in-on2", "out-on2"]   # the bundle
NOKEEP = ["in-nokeep", "out-nokeep"]                                                               # keep off: the A/B
# select / match pairs
SWITCHES = "\\[hair primary\\] switches \\(last"
HEADER = "\\[hair primary\\] header pass \\(last"
# The cause list of the switches line as common/hair_primary.h SwapTransitions::format prints it since 6k (the CTest
# hair_groom_asset_test pins the same text). CAUSES(stale): every cause 0, "coverage stale/none/invalidated" as given.
def causes(stale):
    return ("\\(frame watchdog 0, coverage low 0, coverage high 0, "
            f"coverage stale/none/invalidated {stale}, header not applied \\(camera gate, order\\) 0, "
            "live toggle 0, other 0\\), .* frames with hair draws, 0 with every draw on cards; "
            ".*frame watchdog trips 0 \\(")
# The bundle: no switch to cards of any cause, no frame with every draw on cards, no frame under its bound.
NO_SWITCH = ": 0 strands->cards " + causes("0")
# Keep off is the CONTROL: its windows only need their switches line (user rule 2026-11-14, AGENTS.md sensible gates).
# hx1 202905 was INVALID only because in-nokeep required "other 0" and saw "coverage stale 2, other 1"; the counts of
# the control are reported next to the bundle's, never gated.
CONTROL_LINE = "\\d+ strands->cards \\(frame watchdog \\d+, .*card runs \\d+: \\d+/\\d+/\\d+; "
OWN_HEADER = ("rt_hair_header_scene_pass 1\\): applied at the scene G-buffer pass [1-9][0-9]*, at the decal pass 0 "
              ".*label taken by the decal pass 0 .*by another scene pass 0 ")


def phase(label, seconds, keep=1, wheel=None, frames=None, rotate=0):
    p = {"label": label, "kind": "static", "speed": "static", "seconds": seconds, "repeat_ms": 0, "scan": 16,
         "capture_at": 0, "serial": 0, "motion_start": seconds, "motion_end": seconds,
         "pixels_per_second": 0, "direction": 0,
         "set": f"rt_hair_groom_primary=1,rt_hair_coverage_keep={keep},"
                # every phase names the stats period: a dolly window's 180 would stay for the windows after it
                "pan_inputs=0,pan_capture=0,rt_stats_period_frames=60"}
    if rotate:   # hc1's slow turn (60 px/s) from the first second to the end of the frame window
        p.update(kind="rotate", speed="slow", motion_start=1, motion_end=seconds - 1, pixels_per_second=60,
                 direction=rotate)
    if wheel:
        p["wheel"] = wheel
    if frames:   # a frame window keeps its length through live_flow's tightening (camera-plan: needs stats_reset)
        p.update(frames=frames, settle_frames=0, stats_reset=True, wait_stats=[], wait_log_timeout=30)
    return p


def make_plan():
    return [
        # every value the measured windows use before the first one (live_flow warm-up lint): keep 1 and 0
        phase("warmup", 20), phase("warm-ab", 3, keep=0), phase("warm-on", 3),
        # an unjudged dolly first: the near LODs of the zoom upload here (6 decodes of 47-94 ms in hx1 180928's first
        # dolly, 8 of 47-78 ms in 190905's)
        phase("pre-in", 4, wheel=10, frames=PRE_FRAMES), phase("pre-out", 4, wheel=-10, frames=PRE_FRAMES),
        # the bundle: hl1's dolly, hc1's orbit (the whole 180-frame window turns)
        phase("settle-on", 3), phase("in-on", 8, wheel=10, frames=DOLLY_FRAMES),
        phase("out-on", 8, wheel=-10, frames=DOLLY_FRAMES),
        phase("orbit-r", 8, rotate=1, frames=DOLLY_FRAMES), phase("orbit-l", 8, rotate=-1, frames=DOLLY_FRAMES),
        # the A/B inside the run: the verdict expires again (10 card frames for a head that enters the view)
        phase("settle-nokeep", 3, keep=0), phase("in-nokeep", 8, keep=0, wheel=10, frames=DOLLY_FRAMES),
        phase("out-nokeep", 8, keep=0, wheel=-10, frames=DOLLY_FRAMES),
        # and the bundle again: the same dolly after the A/B (its card runs next to the keep-off ones)
        phase("settle-on2", 3), phase("in-on2", 8, wheel=10, frames=DOLLY_FRAMES),
        phase("out-on2", 8, wheel=-10, frames=DOLLY_FRAMES),
    ]


def every(name, select, match, windows, **kw):
    e = {"name": name, "select": select, "match": match, "min": 1, "windows": windows}
    e.update(kw)
    return e


def make_job(plan):
    wheels = [(i, p["wheel"]) for i, p in enumerate(plan) if p.get("wheel")]
    gated = [("dollies", ON_DOLLY, DOLLY), ("orbits", ON_ORBIT, DOLLY), ("dollies after the A/B", ON_AGAIN, DOLLY)]
    moving = gated + [("keep off", NOKEEP, DOLLY)]
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
            # 6h: the header pass key is on; 6i: the layer counts second scene passes with their own header
            "\\[hair primary\\] header pass \\(last \\d+ frames, rt_hair_header_scene_pass 1\\)",
            "\\[hair primary\\] header pass \\(last .*second scene passes of an interval with their own header and tag \\d+",
            # 6k: the layer under test is the one without the removed keys (its switches line has seven causes)
            "\\[hair primary\\] switches \\(last \\d+ frames\\): \\d+ strands->cards \\(frame watchdog \\d+, coverage low \\d+, "
            "coverage high \\d+, coverage stale/none/invalidated \\d+, header not applied \\(camera gate, order\\) \\d+, "
            "live toggle \\d+, other \\d+\\), \\d+ cards->strands; ",
        ],
        "expect_every": [
            # 6h and 6i: only the scene G-buffer pass applies the header, no scene pass runs on a header the decal pass
            # or another scene pass applied (predicted.json part1_bundle)
            *[every(f"every scene G-buffer pass has its own header: no label taken by the decal pass or by another "
                    f"scene pass ({what})", HEADER, OWN_HEADER, windows, **period) for what, windows, period in moving],
            # the user's target with the bundle: strands in every frame, also under fast camera motion. No switch to
            # cards of any cause (a count of real faults: 0, no tolerance), no frame with every draw on cards, no frame
            # under its bound.
            *[every(f"strands in every frame with the bundle: 0 strands->cards of any cause ({what})",
                    SWITCHES, NO_SWITCH, windows, **period) for what, windows, period in gated],
            # keep off: the control of the A/B; its line must exist, its values are the report
            every("keep off (control): the switches line exists in every window", SWITCHES, CONTROL_LINE, NOKEEP, **DOLLY),
            *[every(f"the rebase origin is 0 in every sampled frame (header camera copied on the GPU; {what})",
                    "\\[hair primary\\] rebase origin \\(last", " 0 with a non-zero origin .*rt_hair_header_cam_gpu 1",
                    windows, **period) for what, windows, period in moving],
            # reported, not gated (predicted.json part1_bundle predicted_not_gated): the card runs of the heads that
            # enter the view (3 or 4 frames with the bundle, 10 with keep off), the first refusing gates, the dropouts
            *[every(f"card runs of entering heads, min/median/max (report; {what})", SWITCHES,
                    "\\d+ cards->strands; .*card runs \\d+: \\d+/\\d+/\\d+; ", windows, min=0, **period)
              for what, windows, period in moving],
            *[every(f"swap refusals by first refusing gate (report; {what})",
                    "\\[hair primary\\] frame \\d+: \\d+ grooms published",
                    "hair draws strand/total: main G-buffer \\d+/\\d+, .*swap refusals \\(main-view hair draws kept as "
                    "cards, first refusing gate\\):", windows, min=0, **period) for what, windows, period in moving],
            *[every(f"swapped draws under a tenth of their card pixels (report; {what})",
                    "\\[hair primary\\] dropouts \\(last",
                    "\\): \\d+ swapped G-buffer draws wrote under a tenth of their card pixels", windows, min=0, **period)
              for what, windows, period in moving],
            # 6k: a hitch frame was never seen live; if one comes, the line names what the second pass did
            every("hitch frames: a second scene G-buffer pass in one present interval (report, once per session)",
                  "\\[hair primary\\] hitch frame", "a second scene G-buffer pass was recorded under frame label \\d+",
                  ON_DOLLY + ON_ORBIT + NOKEEP + ON_AGAIN, event=True, min=0),
            every("strand G-buffer GPU ms with and without rt_hair_coverage_keep (cost, report only)",
                  "\\[hair primary\\] strand G-buffer draws GPU", "GPU [0-9.]+ ms/frame",
                  ON_DOLLY + NOKEEP, min=0, **DOLLY),
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
        "purpose": "6k: one launch, the RTX Hair On bundle as the launcher writes it (rt_hair_header_scene_pass and "
                   "rt_hair_coverage_keep inside it), no card fill, no dense sampler. In every wheel dolly and orbit "
                   "window with the bundle: the header applied at the scene G-buffer pass only, no label taken by "
                   "the decal pass or by another scene pass, 0 switches to cards of any cause, no frame on cards, "
                   "frame watchdog trips 0. Reported: the card runs of the heads that enter the view (3 or 4 frames "
                   "predicted, 5 in hx1 194658) next to those of the same dolly with rt_hair_coverage_keep off "
                   "(10), toggled inside the run. STAGED, not authorized.",
    }


def check_bundle():
    """HAIR is the On choice of exp_hair_groom_primary (data/settings_schema.json), key for key."""
    schema = json.loads((ROOT / "data/settings_schema.json").read_text(encoding="utf-8"))
    knob = next(k for k in schema["knobs"] if k["id"] == "exp_hair_groom_primary")
    on = next(c for c in knob["choices"] if c["id"] == "on")
    bundle = dict(on["keys"])
    mine = dict(pair.split("=") for pair in HAIR.split(","))
    assert mine == bundle, (sorted(set(mine) ^ set(bundle)), "HAIR is not the RTX Hair On bundle")


def main():
    assert (HERE / "predicted.json").exists(), "predicted.json precedes the job"
    check_bundle()
    plan = make_plan()
    for folder, value in (("plans", plan), ("jobs", make_job(plan))):
        (HERE / folder).mkdir(exist_ok=True)
        (HERE / folder / f"{NAME}.json").write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8", newline="\n")
    from tools.live_batches.live_flow import tighten_files
    # The checked-in snapshot's frame rates (reproducible in any worktree). The live corpus of 2026-11-14 holds
    # VALID S8 logs whose PNG trace windows (5.0 and 5.4 fps over the stall) are their tenth percentile: every
    # 180-frame window would be budgeted 33 s.
    tighten_files(HERE, job_names=[NAME], frozen=True)
    print(f"Generated {NAME}; no queue staging")


if __name__ == "__main__":
    main()
