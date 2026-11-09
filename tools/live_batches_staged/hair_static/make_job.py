"""hair_static (branch claude/hair-static-flicker, docs/RTX_HAIR_PRIMARY.md 5p): STAGED, not queued, not authorized.

    py -3 tools/live_batches_staged/hair_static/make_job.py

User report: with a static camera the RTX Hair strands appear and disappear very fast ("a flickering mass"), with no
strand/card swapping (rv3 220145: rebase 0, strand draws 720/720, coverage 94-96%, no refusals). One Water Queens House
(S8) launch with the user's set (release Ultra, fg=3, the RTX Hair bundle), the rv1/rv3 lowered view, then a static
camera: three 16-frame bursts with the RR inputs, strands (rr_hair_strand_aa=0), strands (=2), cards
(rt_hair_groom_primary=0, live). Observable: flicker in the pre-RR strand image (rr_color, class-5 toggles) vs only
after RR (final), against the cards of the same run. Analysis: tools/hair_groom/temporal_burst.py. predicted.json
precedes the job. The job lives outside every watched captures/queue/pending inbox.
"""
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))

NAME = "hs1-s8-static-hair"
PLAN = f"tools/live_batches_staged/hair_static/plans/{NAME}.json"
# The user-tested RTX Hair set (common/settings.h exp_hair_groom_primary "on", docs 5l) with the strand resolve built
# at startup (rr_hair_strand_aa=2 creates the strand_resolve pipeline; the windows set the mode).
HAIR = ("rt_hair_groom=1,rt_hair_groom_primary=1,rt_hair_groom_primary_order=1,rt_hair_groom_primary_current=1,"
        "rt_hair_primary_frame_tag=1,rt_hair_groom_lod_keep=1,rt_hair_primary_hold=1,rt_hair_tangent_world=1,"
        "rt_hair_groom_shadow_t=1,rt_hair_groom_ao_t=1,rt_hair_groom_cascade_drop=1,rt_hair_fibre=1,"
        "rt_hair_fibre_gi=1,rt_hair_fibre_spec=1,rt_hair_fibre_mapped=1,rr_hair_guides=1,"
        "rt_hair_physical_origin=1,rt_hair_capture=1,rr_hair_strand_aa=2")
# rv3's release settings (rel095_verify/make_job.py SET3), traces off.
SET = ("preset=ultra,fg=3,headless=0,crash_dump=1,rt_timing_frames=120,rt_scene_wait_log_frames=120,"
       "rt_debug=0,ls_debug=0,gi_debug=0,rf_debug=0,rr_show=0,rf_stats=0,ls_stats=0,ls_check=0,rt_stats=0,"
       "rt_primary_check=0,dump_images=1,dump_png_width=1280,dump_raw=0,dump_shaders=0,pan_capture=0,"
       "pan_inputs=0,pan_prepare=0,sharc=0,qa_exposure_hold=0,rt_alpha_floor_yield=0,rt_capture_rise_reserve=1,"
       "rt_capture_wide_split=1,rt_mesh_budget_own=0," + HAIR)
SERIALS = {"burst-strands": 99300, "burst-aa": 99301, "burst-cards": 99302}


def phase(label, seconds, aa=0, strands=1, hold=1, wheel=None, inputs=0, serial=0, capture_at=0):
    p = {"label": label, "kind": "static", "speed": "static", "seconds": seconds, "repeat_ms": 0, "scan": 16,
         "capture_at": capture_at, "serial": serial, "motion_start": seconds, "motion_end": seconds,
         "pixels_per_second": 0, "direction": 0,
         "set": f"rr_hair_strand_aa={aa},rt_hair_groom_primary={strands},qa_exposure_hold={hold}"}
    if serial:
        # Full burst with the RR inputs (rr_color, rr_mvec, material every frame), as he1's (hair_edge/make_job.py).
        p["set"] += f",pan_prepare=1,pan_edge=0,pan_inputs=1,pan_capture={serial},pan_delay_ms=0"
    else:
        p["set"] += f",pan_inputs={inputs},pan_capture=0,pan_delay_ms=0"
    if wheel:
        p["wheel"] = wheel
    return p


def make_plan():
    # Bursts only in timed windows with plain expects (he1 03:17: a burst inside a periodic gate's window was cut to a
    # 60-frame stats window before it completed). Each burst is preceded by a 60-frame settle window that the
    # periodic gates judge; capture at 1 s lets RR history converge after a mode switch (30 frames at 30 fps).
    return [
        phase("warmup", 20, aa=2, hold=0),
        # every value the measured windows use, before them (live_flow warm-up lint): cards for 3 s, then strands again
        # for the 8 s of the wheel phase (the swap gates need ~12-15 frames; no level load, no LoadGuard window)
        phase("warm-cards", 3, strands=0, hold=0),
        phase("lower", 8, wheel=10, hold=0),
        phase("warm-inputs", 1.1, inputs=1, hold=0),    # the RR input dump pipelines once before the bursts
        phase("settle-strands", 4),
        phase("burst-strands", 14, serial=SERIALS["burst-strands"], capture_at=1.0),
        phase("settle-aa", 4, aa=2),
        phase("burst-aa", 14, aa=2, serial=SERIALS["burst-aa"], capture_at=1.0),
        phase("settle-cards", 4, strands=0),
        phase("burst-cards", 14, strands=0, serial=SERIALS["burst-cards"], capture_at=1.0),
        phase("restore", 2, strands=1, hold=0),
    ]


def every(name, select, match, windows, **kw):
    e = {"name": name, "select": select, "match": match, "min": 1, "windows": windows}
    e.update(kw)
    return e


def make_job(plan):
    settle = ["settle-strands", "settle-aa", "settle-cards"]
    return {
        "name": NAME,
        "save": "Water Queens House",
        "session": False,
        "traces": 0,
        "hold": 120,
        "timeout": 420,
        "max_minutes": 5,
        "repeat": 1,
        "live_batch": "hair-static",
        "tree": ".claude/worktrees/test-main",
        "window_mode": "visible_unfocused_fg",
        "fail_fast": True,
        "stop_on_dump": False,
        "set": SET,
        "args": ["-RealPresent", "-Move", "-MovePlan", PLAN],
        "patterns": ["\\[auto\\]", "\\[cfg\\]", "\\[hot\\]", "\\[watch\\]", "\\[crash\\]", "FAILED", "\\[pan\\]",
                     "\\[rrpost\\]", "\\[hair primary\\] frame", "\\[rt\\] scene wait", "rt passes: GPU cost",
                     "\\[sl\\] DLSS-G frame", "\\[sl\\] warn", "VK_ERROR", "\\[log\\]"],
        "expect": [
            "\\[auto\\] scene ready",
            "\\[hot\\] core generation \\d+ loaded",
            "\\[cfg\\] settings read",
            "\\[rrpost\\] exposure held \\(qa_exposure_hold=1\\) at EV [-0-9.]+",
            "\\[rrpost\\] rr_hair_strand_aa=0:",
            "\\[rrpost\\] rr_hair_strand_aa=2:",
            # strands drawn in the main G-buffer (a 300-frame period inside the strand windows), then cards only
            "\\[hair primary\\] frame \\d+: \\d+ grooms published; .*main G-buffer [1-9]\\d*/\\d+",
            "\\[hair primary\\] frame \\d+: RTX Hair off \\(live\\)",
        ] + [rf"\[pan\] burst {s} complete: \d+ frames, \d+ samples, transport ok" for s in SERIALS.values()],
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
        ],
        "expect_runner": [
            "\\[window-mode\\].*visible_unfocused",
            "\\[wheel-input\\] phase=2 notches=10\\b",
            f"\\[motion-plan\\] complete phases={len(plan)}\\b",
            "^Run ended: done",
        ],
        "live_flow": {"expected_fps": 30, "settings_latency_s": 1, "trace_stall_s": 3},
        "estimated_phase_seconds": sum(p["seconds"] for p in plan),
        "purpose": "Static-camera RTX Hair flicker at S8 (user set, fg=3): 16-frame RR-input bursts of strands "
                   "(rr_hair_strand_aa 0, then 2) and cards (rt_hair_groom_primary=0) in one run; temporal_burst.py "
                   "decides whether the flicker is in the pre-RR strand image (class-5 toggles, rr_color) or only "
                   "after RR (final). STAGED, not authorized.",
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
