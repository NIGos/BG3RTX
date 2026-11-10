"""hair_mv (branch claude/hair-mv, docs/RTX_HAIR_PRIMARY.md 5x): STAGED, not queued, not authorized.

    py -3 tools/live_batches_staged/hair_mv/make_job.py

User report: strand hair at isometric distance trails / blurs when characters or the camera move. The hs1 static bursts
show 73-86% of strand hair pixels with camera-only motion vectors (cards 5-7%). One Water Queens House (S8) launch with
the hs1 set plus rt_hair_mv_card=1 at startup (specialization 1049 in the velocity variant), the hs1 lowered view, then
a static camera with the party idling: the counter line in windows at rt_hair_mv_card 0, 1, 2 and four K8 edge
bursts with RR's motion vectors (pan_mvec, about 0.1 GB each): strands at rt_hair_mv_card 0, 1, 2 and cards.
Analysis: tools/hair_groom/mv_ratio.py. predicted.json precedes the job. The job lives outside every watched
captures/queue/pending inbox.
"""
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))

NAME = "hm1-s8-hair-mv"
PLAN = f"tools/live_batches_staged/hair_mv/plans/{NAME}.json"
# The user-tested RTX Hair set (common/settings.h exp_hair_groom_primary "on", docs 5l) with the strand resolve built
# at startup (rr_hair_strand_aa=2 creates the strand_resolve pipeline; the windows set the mode).
HAIR = ("rt_hair_groom=1,rt_hair_groom_primary=1,rt_hair_groom_primary_order=1,rt_hair_groom_primary_current=1,"
        "rt_hair_primary_frame_tag=1,rt_hair_groom_lod_keep=1,rt_hair_primary_hold=1,rt_hair_tangent_world=1,"
        "rt_hair_groom_shadow_t=1,rt_hair_groom_ao_t=1,rt_hair_groom_cascade_drop=1,rt_hair_fibre=1,"
        "rt_hair_fibre_gi=1,rt_hair_fibre_spec=1,rt_hair_fibre_mapped=1,rr_hair_guides=1,"
        "rt_hair_physical_origin=1,rt_hair_capture=1,rt_hair_coverage_union=1,rt_hair_coverage_confirm=1,"
        "rt_hair_slot_stable=1,rt_hair_live_warmup=1,rt_hair_groom_nearest=1,rr_hair_strand_aa=2")
# rv3's release settings (rel095_verify/make_job.py SET3), traces off.
SET = ("preset=ultra,fg=3,headless=0,crash_dump=1,rt_timing_frames=120,rt_scene_wait_log_frames=120,"
       "rt_debug=0,ls_debug=0,gi_debug=0,rf_debug=0,rr_show=0,rf_stats=0,ls_stats=0,ls_check=0,rt_stats=0,"
       "rt_primary_check=0,dump_images=1,dump_png_width=1280,dump_raw=0,dump_shaders=0,pan_capture=0,"
       "pan_inputs=0,pan_prepare=0,sharc=0,qa_exposure_hold=0,rt_alpha_floor_yield=0,rt_capture_rise_reserve=1,"
       "rt_capture_wide_split=1,rt_mesh_budget_own=0,rt_hair_mv_card=1," + HAIR)
SERIALS = {"burst-mv0": 99400, "burst-mv1": 99401, "burst-mv2": 99402, "burst-cards": 99403}


def phase(label, seconds, aa=0, strands=1, hold=1, wheel=None, inputs=0, serial=0, capture_at=0, prepare=0, mv=0):
    p = {"label": label, "kind": "static", "speed": "static", "seconds": seconds, "repeat_ms": 0, "scan": 16,
         "capture_at": capture_at, "serial": serial, "motion_start": seconds, "motion_end": seconds,
         "pixels_per_second": 0, "direction": 0,
         "set": f"rr_hair_strand_aa={aa},rt_hair_groom_primary={strands},qa_exposure_hold={hold},rt_hair_mv_card={mv}"}
    if serial:
        # Full burst with the RR inputs (rr_color, rr_mvec, material every frame), as he1's (hair_edge/make_job.py).
        # K8 edge burst with RR's motion vectors (pan_mvec, 5x): half-size depth, final and rr_mvec in 8 frames, material
        # and albedo in frames 1 and 8, about 0.1 GB per burst (a full RR-input burst is 4.9 GB).
        p["set"] += f",pan_prepare=1,pan_edge=1,pan_mvec=1,pan_post=0,pan_inputs=1,pan_capture={serial},pan_delay_ms=0"
    else:
        p["set"] += f",pan_inputs={inputs},pan_prepare={prepare},pan_capture=0,pan_delay_ms=0"
        if inputs:   # the burst modes before the measured windows (live_flow warm-up lint); no capture without a serial
            p["set"] += ",pan_edge=1,pan_mvec=1"
    if wheel:
        p["wheel"] = wheel
    return p


def make_plan():
    # hs1's proven static plan (bursts only in timed windows with plain expects; settle windows the periodic gates
    # judge; capture at 1 s so RR history converges after a switch). The counter line comes every 60 frames: the
    # settle windows (4 s at ~30 fps) hold at least one per rt_hair_mv_card value.
    return [
        # rt_hair_mv_card stays 1 until the measured windows: the velocity pipelines are created at the level load with
        # specialization 1049 only while the key is non-zero.
        phase("warmup", 20, hold=0, mv=1),
        phase("warm-cards", 3, strands=0, hold=0, mv=1),
        phase("lower", 8, wheel=10, hold=0, mv=1),
        phase("warm-inputs", 8, inputs=1, hold=0, prepare=1, mv=1),
        # every rt_hair_mv_card value the measured windows use, before them (live_flow warm-up lint)
        phase("warm-mv2", 2, hold=0, prepare=1, mv=2),
        phase("warm-mv0", 2, hold=0, prepare=1, mv=0),
        phase("settle-mv0", 4, prepare=1),
        phase("burst-mv0", 14, serial=SERIALS["burst-mv0"], capture_at=1.0),
        phase("settle-mv1", 4, prepare=1, mv=1),
        phase("burst-mv1", 14, serial=SERIALS["burst-mv1"], capture_at=1.0, mv=1),
        phase("settle-mv2", 4, prepare=1, mv=2),
        phase("burst-mv2", 14, serial=SERIALS["burst-mv2"], capture_at=1.0, mv=2),
        phase("settle-cards", 4, strands=0, prepare=1),
        phase("burst-cards", 14, strands=0, serial=SERIALS["burst-cards"], capture_at=1.0),
        phase("restore", 2, hold=0),
    ]


def every(name, select, match, windows, **kw):
    e = {"name": name, "select": select, "match": match, "min": 1, "windows": windows}
    e.update(kw)
    return e


def make_job(plan):
    settle = ["settle-mv0", "settle-mv1", "settle-mv2"]
    return {
        "name": NAME,
        "save": "Water Queens House",
        "session": False,
        "traces": 0,
        "hold": 120,
        "timeout": 420,
        "max_minutes": 5,
        "repeat": 1,
        "live_batch": "hair-mv",
        "tree": ".claude/worktrees/test-main",
        "window_mode": "visible_unfocused_fg",
        "fail_fast": True,
        "stop_on_dump": False,
        "set": SET,
        "args": ["-RealPresent", "-Move", "-MovePlan", PLAN],
        "patterns": ["\\[auto\\]", "\\[cfg\\]", "\\[hot\\]", "\\[watch\\]", "\\[crash\\]", "FAILED", "\\[pan\\]",
                     "\\[rrpost\\]", "\\[hair primary\\] frame", "\\[hair groom\\] strand motion",
                     "\\[hair primary\\] strand G-buffer draws GPU", "\\[rt\\] scene wait", "rt passes: GPU cost",
                     "\\[sl\\] DLSS-G frame", "\\[sl\\] warn", "VK_ERROR", "\\[log\\]"],
        "expect": [
            "\\[auto\\] scene ready",
            "\\[hot\\] core generation \\d+ loaded",
            "\\[cfg\\] settings read",
            "\\[rrpost\\] exposure held \\(qa_exposure_hold=1\\) at EV [-0-9.]+",
            # strands drawn in the main G-buffer (a 300-frame period inside the strand windows), then cards only
            "\\[hair primary\\] frame \\d+: \\d+ grooms published; .*main G-buffer [1-9]\\d*/\\d+",
            "\\[hair primary\\] frame \\d+: RTX Hair off \\(live\\)",
        ] + [rf"\[pan\] burst {s} complete: 8 frames, \d+ samples, transport ok" for s in SERIALS.values()],
        "forbid": [
            "\\[crash\\] exception 0x", "VK_ERROR_DEVICE_LOST", "\\[hot\\] core (rejected|faulted)",
            "FAILED, keeping previous", "compile call failed", "\\[log\\] reached \\d+ MB", "\\[sl\\] .*eWarnOutOfVRAM",
            "\\[rrpost\\] rr_post\\.hlsl:strand_resolve pipeline creation failed",
        ],
        "expect_every": [
            every("render-res chain GPU ms in every settle window (cost, report only)",
                  "\\[rrpost\\] gpu: render-res chain", "render-res chain [0-9.]+ ms/frame", settle, period_frames=60),
            every("scene wait: 0 timeouts in every settle window", "\\[rt\\] scene wait: \\d+ frames",
                  "got this frame's scene, 0 timeouts", settle),
            every("strand motion counter line in every settle window (rows with and without previous points)",
                  r"\[hair groom\] strand motion", r"keyed groom rows [1-9]\d*, with previous points \d+", settle),
        ],
        "expect_runner": [
            "\\[window-mode\\].*visible_unfocused",
            "\\[wheel-input\\] phase=2 notches=10\\b",
            f"\\[motion-plan\\] complete phases={len(plan)}\\b",
            "^Run ended: done",
        ],
        "live_flow": {"expected_fps": 30, "settings_latency_s": 1, "trace_stall_s": 3},
        "estimated_phase_seconds": sum(p["seconds"] for p in plan),
        "purpose": "RTX Hair strand motion vectors at S8 (user set, fg=3, static camera, idle party): the counter "
                   "line at rt_hair_mv_card 0/1/2 and K8 edge bursts with RR motion vectors at 0, 1, 2 and cards in one run; "
                   "mv_ratio.py (camera-only share, hair/ring ratio). STAGED, not authorized.",
    }


def main():
    assert (HERE / "predicted.json").exists(), "predicted.json precedes the job"
    plan = make_plan()
    for folder, value in (("plans", plan), ("jobs", make_job(plan))):
        (HERE / folder).mkdir(exist_ok=True)
        (HERE / folder / f"{NAME}.json").write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8", newline="\n")
    from tools.live_batches.live_flow import tighten_files
    tighten_files(HERE, job_names=[NAME])
    tight = json.loads((HERE / "plans" / f"{NAME}.json").read_text(encoding="utf-8"))
    for p in tight:
        if p["label"] in SERIALS:
            assert "frames" not in p and not p.get("stats_reset") and p["seconds"] >= 14, (p["label"], p.get("frames"), p["seconds"])
    print(f"Generated {NAME}; no queue staging")


if __name__ == "__main__":
    main()
