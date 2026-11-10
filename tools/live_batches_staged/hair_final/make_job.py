"""hair_final: the final RTX Hair qualification in the user's real preset. STAGED, not queued, not authorized.

    py -3 -m tools.live_batches_staged.hair_final.make_job

One Water Queens House (S8) launch, preset=ultra with fg=3 (the hl1/pl1/hm1 set), the RTX Hair On bundle and every new
hair key together: rt_hair_stale_publish=1 (6a), rt_hair_mv_card=2 (5y; non-zero from the level load so the velocity
pipelines get specialization 1049), rt_hair_row_stable=1 (5w) and RTX Hair detail at distance 1/4 (hair_distant_detail:
rt_hair_pixel_lod=1, rt_hair_pixel_lod_small_keep=0.25). Phases: (a) static warm-up; (b) hl1's wheel zoom ("lower",
"watch") with 0 frame watchdog trips; (c) hc1's slow rotation at the isometric distance, right and back, with one K8
edge burst with RR motion vectors (tools/hair_groom/mv_ratio.py offline); (d) static isometric: distant detail off /
1/4 / 1/8 / off, a PNG trace (float preview) 1.5 s after each switch, then a 120-frame cost window; (e) strand
G-buffer, rt passes and frame time lines in every window. predicted.json precedes the job. The job lives outside every
watched captures/queue/pending inbox.
"""
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))

NAME = "hf2-s8-hair-final"
PLAN = f"tools/live_batches_staged/hair_final/plans/{NAME}.json"
# The RTX Hair On bundle (common/settings.h exp_hair_groom_primary "on") and the new keys, all on.
HAIR = ("rt_hair_groom=1,rt_hair_groom_primary=1,rt_hair_groom_primary_order=1,rt_hair_groom_primary_current=1,"
        "rt_hair_primary_frame_tag=1,rt_hair_groom_lod_keep=1,rt_hair_primary_hold=1,rt_hair_tangent_world=1,"
        "rt_hair_groom_shadow_t=1,rt_hair_groom_ao_t=1,rt_hair_groom_cascade_drop=1,rt_hair_fibre=1,"
        "rt_hair_fibre_gi=1,rt_hair_fibre_spec=1,rt_hair_fibre_mapped=1,rr_hair_guides=1,"
        "rt_hair_physical_origin=1,rt_hair_capture=1,rt_hair_coverage_union=1,rt_hair_coverage_confirm=1,"
        "rt_hair_slot_stable=1,rt_hair_live_warmup=1,rt_hair_groom_nearest=1,"
        "rt_hair_stale_publish=1,rt_hair_mv_card=2,rt_hair_row_stable=1,"
        "rt_hair_pixel_lod=1,rt_hair_pixel_lod_k=1.0,rt_hair_pixel_lod_small_keep=0.25")
SET = ("preset=ultra,fg=3,headless=0,crash_dump=1,rt_timing_frames=120,rt_scene_wait_log_frames=120,"
       "rt_debug=0,ls_debug=0,gi_debug=0,rf_debug=0,rr_show=0,rf_stats=0,ls_stats=0,ls_check=0,rt_stats=0,"
       "rt_primary_check=0,dump_images=1,dump_png_width=1280,dump_raw=0,dump_preview_f32=1,dump_shaders=0,"
       "pan_capture=0,pan_inputs=0,pan_prepare=0,sharc=0,"
       "qa_exposure_hold=0,rt_alpha_floor_yield=0,rt_capture_rise_reserve=1,rt_capture_wide_split=1,"
       "rt_mesh_budget_own=0,detailed_logging=1,rt_stats_period_frames=60," + HAIR)
BASE = "rt_hair_groom_primary=1,rt_hair_stale_publish=1,rt_hair_mv_card=2,rt_hair_row_stable=1"
DETAIL = {"off": "rt_hair_pixel_lod=0,rt_hair_pixel_lod_small_keep=0.25",
          "q": "rt_hair_pixel_lod=1,rt_hair_pixel_lod_small_keep=0.25",
          "e": "rt_hair_pixel_lod=1,rt_hair_pixel_lod_small_keep=0.125"}
# The pan modes of the burst (hm1: K8 edge burst with RR's motion vectors, half size, 8 frames, ~0.1 GB).
PAN_MODES = "pan_prepare=1,pan_edge=1,pan_mvec=1,pan_post=0,pan_inputs=1,pan_delay_ms=0"
SERIAL = 99500
# (d): the static toggle windows, in order
STATES = (("off-a", "off"), ("q", "q"), ("e", "e"), ("off-b", "off"))


def phase(label, seconds, detail="q", pan=False, wheel=None, frames=None, trace=None, rotate=0, serial=0,
          capture_at=0):
    p = {"label": label, "kind": "static", "speed": "static", "seconds": seconds, "repeat_ms": 0, "scan": 16,
         "capture_at": capture_at, "serial": serial, "motion_start": seconds, "motion_end": seconds,
         "pixels_per_second": 0, "direction": 0, "set": f"{BASE},{DETAIL[detail]}"}
    p["set"] += f",{PAN_MODES},pan_capture={serial}" if pan else ",pan_inputs=0,pan_capture=0"
    if rotate:   # hc1's slow turn at the isometric distance (60 px/s, motion from 1 s to seconds - 1)
        p.update(kind="rotate", speed="slow", motion_start=1, motion_end=seconds - 1, pixels_per_second=60,
                 direction=rotate)
    if wheel:
        p["wheel"] = wheel
    if frames:   # a frame window keeps its length through live_flow's tightening (camera-plan: needs stats_reset)
        p.update(frames=frames, settle_frames=0, stats_reset=True, wait_stats=[], wait_log_timeout=30)
    if trace is not None:   # one PNG trace (dump_images=1, 1280 px, float preview, no raw)
        p["trace_at"] = [trace]
    return p


def make_plan():
    # (a) warm-up: every value the measured windows use before the first one (live_flow warm-up lint): 1/8, off, then
    # the burst modes prepared (no capture without a serial), ending in the user state (1/4).
    plan = [phase("warmup", 20, "e"), phase("warm-off", 3, "off"), phase("warm-inputs", 6, "q", pan=True),
            # (b) hl1's zoom to the isometric view and the transient after it
            phase("lower", 8, wheel=10, frames=240), phase("watch", 12, frames=360),
            # (c) slow orbit right with the burst 3 s into the turn, then back left to the same view
            phase("orbit-r", 14, pan=True, rotate=1, serial=SERIAL, capture_at=4),
            # capture_at 4 on both: a frame window ends 60 frames after its settings apply (live_flow tightens frames
            # to the stats period), so each turn lasts ~4 s + 60 frames and the left turn returns about as far
            phase("orbit-l", 14, rotate=-1, capture_at=4),
            phase("iso-settle", 3)]
    # (d) the trace within ~45 frames of the switch (30-frame level vote + camera BLAS build), then the cost window
    for label, detail in STATES:
        plan += [phase(f"shot-{label}", 2.5, detail, trace=1.5), phase(f"win-{label}", 4, detail, frames=120)]
    return plan


def every(name, select, match, windows, **kw):
    e = {"name": name, "select": select, "match": match, "min": 1, "windows": windows}
    e.update(kw)
    return e


def make_job(plan):
    shots = [(i, p["label"]) for i, p in enumerate(plan) if p.get("trace_at")]
    wins = [f"win-{s[0]}" for s in STATES]
    cost = ["lower", "watch", "orbit-r", "orbit-l"] + wins
    return {
        "name": NAME,
        "save": "Water Queens House",
        "session": False,
        "traces": 0,
        "hold": 120,
        "timeout": 420,
        "max_minutes": 4,
        "repeat": 1,
        "live_batch": "hair-final",
        "tree": ".claude/worktrees/test-main",
        "window_mode": "visible_unfocused_fg",
        "fail_fast": True,   # a nonmatching period line (no strands drawn) ends the run at once; trips are report only
        "stop_on_dump": False,
        "set": SET,
        "args": ["-RealPresent", "-Move", "-MovePlan", PLAN],
        "patterns": ["\\[auto\\]", "\\[cfg\\]", "\\[hot\\]", "\\[watch\\]", "\\[crash\\]", "FAILED",
                     "\\[hair primary\\]", "\\[hair groom\\]", "\\[rt\\] scene wait", "VK_ERROR", "\\[log\\]",
                     "\\[pan\\]", "\\[trace\\]", "\\[pace\\]", "rt passes: GPU cost", "\\[sl\\] warn"],
        "expect": [
            "\\[auto\\] scene ready",
            "\\[hot\\] core generation \\d+ loaded",
            "\\[cfg\\] settings read",
            "\\[trace\\] frame \\d+ written",
            rf"\[pan\] burst {SERIAL} complete: 8 frames, \d+ samples, transport ok",
        ],
        "expect_every": [
            # (b) 6a: frame watchdog trips in the zoom and after it, REPORT ONLY (233301: holes still occur, cause under
            # separate investigation; the strict gate stopped the run in lower). The gate only requires the header
            # safety line with its trip count in each window; 0 trips is judged afterwards (predicted.json), never
            # fatal and never an INVALID reason.
            every("frame watchdog trips reported in lower and watch (report only; 0 judged offline)",
                  "\\[hair primary\\] header safety \\(last", ", watchdog trips \\d+, ", ["lower", "watch"],
                  period_frames=60),
            every("the stale publish check acts in the lower window (key on, the G-buffer camera known)",
                  "\\[hair primary\\] stale publish check \\(last",
                  "\\(rt_hair_stale_publish 1\\); G-buffer camera from its UBO [1-9]", ["lower"], period_frames=60),
            # (d) the distant detail pick in the 1/4 and 1/8 windows (pl1's period-line matches)
            every("1/4: the pixel LOD line with a projection read and some groom at camera level 1 or 2",
                  "\\[hair groom\\] pixel LOD \\(last",
                  "level 0/1/2/3/4/5 \\d+/(?:[1-9]\\d*/\\d+|\\d+/[1-9]\\d*)/\\d+/\\d+/\\d+;.*\\([1-9][0-9]* px per "
                  "radian, 0 groom frames without.*small keep 0\\.250\\)", ["win-q"], period_frames=60),
            every("1/8: the pixel LOD line with some groom at camera level 1-3",
                  "\\[hair groom\\] pixel LOD \\(last",
                  "level 0/1/2/3/4/5 \\d+/(?:[1-9]\\d*/\\d+/\\d+|\\d+/[1-9]\\d*/\\d+|\\d+/\\d+/[1-9]\\d*)/\\d+/\\d+;"
                  ".*small keep 0\\.125\\)", ["win-e"], period_frames=60),
            # Strands drawn in every measured window, judged on the line the layer prints every period whatever it
            # draws (225504: every strand frame drew cards, the strand G-buffer GPU line never came and "lower" waited
            # for it until the run deadline). A line with 0 G-buffer strand draws fails the run at once (fail_fast).
            every("strands drawn in the G-buffer in every window",
                  "\\[hair primary\\] frame \\d+: \\d+ grooms published; last",
                  "\\d+ prepass, [1-9]\\d* G-buffer and \\d+ velocity strand draws", cost, period_frames=60),
            # (e) cost in every measured window (report only). The strand G-buffer GPU line exists only while strands
            # draw: when_present keeps it out of the frame windows' wait_stats (no hang); the gate above requires them.
            every("strand G-buffer GPU ms in every window", "\\[hair primary\\] strand G-buffer draws GPU",
                  "GPU [0-9.]+ ms/frame", cost, period_frames=60, when_present=True),
            every("rt passes GPU ms in every window", "rt passes: GPU cost", "GPU cost [0-9.]+ ms/frame", cost,
                  period_frames=60),
            every("rendered frame time in every window", "\\[pace\\] rendered frame time over \\d+ frames",
                  "median [0-9.]+ ms", cost, period_frames=60),
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
        "purpose": "Final RTX Hair qualification, user preset (ultra, fg=3), all new hair keys on (stale publish, "
                   "mv_card 2, row stable, distant detail 1/4): 0 watchdog trips in hl1's zoom; a slow orbit with one "
                   "K8 RR-motion burst (mv_ratio.py); off / 1/4 / 1/8 / off PNG traces at the static isometric view; "
                   "strand G-buffer, rt passes and frame time per window. STAGED, not authorized.",
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
