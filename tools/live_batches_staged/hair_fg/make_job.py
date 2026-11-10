"""hair_fg (branch claude/hair-slot-identity, docs/RTX_HAIR_PRIMARY.md 5s): STAGED, not queued, not authorized.

    py -3 tools/live_batches_staged/hair_fg/make_job.py

User eyewitness: all visible heads "switch on and off quickly" with a static camera, although the rendered-frame
counters and bursts of hs1 are clean (strands drawn 1438-1440 of 1440, pre-RR flicker strands / cards 1.02). Those
captures are pre-FG; with fg=3 two of three presented frames are generated. One Water Queens House (S8) launch in the
hs1 view with the user's set, then five watch windows (8 s each, then 4 s with 12 presented frames captured by fg_snap):
fg=3, fg=0, fg=3, fg=3 with rr_hair_strand_aa=0, fg=3 with cards. fg is toggled live (layer/streamline.cpp reads it
every present once Streamline is loaded; precedent tools/live_batches/edge_flicker 'fg-off'). The user says in which
window the toggling stops. predicted.json precedes the job. The job lives outside every watched pending inbox.
"""
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(HERE.parent / "hair_static"))
import make_job as hs1  # the user's set and the S8 view: one copy

NAME = "hf1-s8-hair-fg"
PLAN = f"tools/live_batches_staged/hair_fg/plans/{NAME}.json"
SNAP = 12
ZOOM = 20   # wheel notches in: hs1's 10 left the heads at about 30 px of the 1920 x 800 captures (hf1 073932)
# The generated frames are really shown: the layer's count of presents per rendered frame over a full period.
FG_SHOWN = (r"\[sl\] DLSS-G frame \d+: status 0x0, presented per app frame 3, displayed/rendered over the last \d+ "
            r"frames (?:2\.[5-9]\d|[3-9]\.\d+)")
# The last PNG of a capture. "[sl] present capture done" is logged when the last frame is requested, 0.1 s after the
# start and 1 s before this line.
SNAP_DONE = rf"\[sl\] presented frame {SNAP - 1} -> .*\(ok\)"
SET = hs1.SET + ",fg_snap=0,fg_snap_any=1,fg_snap_after=60,fg_state_every=60"
# label, fg, rr_hair_strand_aa, rt_hair_groom_primary
WINDOWS = [("fg3-a", 3, 2, 1), ("fg0", 0, 2, 1), ("fg3-b", 3, 2, 1), ("fg3-aa0", 3, 0, 1), ("fg3-cards", 3, 2, 0)]


def phase(label, seconds, fg=3, aa=2, strands=1, snap=0, wheel=None):
    p = {"label": label, "kind": "static", "speed": "static", "seconds": seconds, "repeat_ms": 0, "scan": 16,
         "capture_at": 0, "serial": 0, "motion_start": seconds, "motion_end": seconds,
         "pixels_per_second": 0, "direction": 0,
         "set": f"fg={fg},rr_hair_strand_aa={aa},rt_hair_groom_primary={strands},fg_snap={snap},"
                "qa_exposure_hold=0,pan_inputs=0,pan_prepare=0,pan_capture=0,pan_delay_ms=0"}
    if wheel:
        p["wheel"] = wheel
    return p


def make_plan():
    plan = [
        phase("warmup", 20),
        # every value the watch windows use, before them (live_flow warm-up lint)
        phase("warm-cards", 3, strands=0),
        phase("warm-aa0", 3, aa=0),
        # zoom before warm-fg0, so that the first watch window changes fg (a phase whose set equals the previous one
        # gets no fresh settings read to anchor its wait condition)
        phase("zoom", 8, wheel=ZOOM),
        phase("warm-fg0", 6, fg=0),
    ]
    for label, fg, aa, strands in WINDOWS:
        # survive- (live_flow keeps a wall-clock observation window at its declared length): the user watches here.
        # fg_snap=0 re-arms the capture; 8 s also covers FG's return (30 stable frames, then fg_snap_after)
        plan.append(phase("survive-" + label, 8, fg, aa, strands))
        plan.append(phase("snap-" + label, 4, fg, aa, strands, snap=SNAP))
    plan.append(phase("restore", 2))
    return plan


def every(name, select, match, windows, **kw):
    e = {"name": name, "select": select, "match": match, "min": 1, "windows": windows}
    e.update(kw)
    return e


def make_job(plan):
    snaps = ["snap-" + w[0] for w in WINDOWS]
    return {
        "name": NAME,
        "save": "Water Queens House",
        "session": False,
        "traces": 0,
        "hold": 120,
        "timeout": 420,
        "max_minutes": 5,
        "repeat": 1,
        "live_batch": "hair-fg",
        "tree": ".claude/worktrees/test-main",
        "window_mode": "visible_unfocused_fg",
        "fail_fast": True,
        "stop_on_dump": False,
        "set": SET,
        "args": ["-RealPresent", "-Move", "-MovePlan", PLAN],
        "patterns": ["\\[auto\\]", "\\[cfg\\]", "\\[hot\\]", "\\[watch\\]", "\\[crash\\]", "FAILED", "\\[rrpost\\]",
                     "\\[hair primary\\] frame", "\\[rt\\] scene wait", "rt passes: GPU cost", "\\[sl\\] DLSS-G frame",
                     "\\[sl\\] FG decision", "\\[sl\\] slDLSSGSetOptions\\(mode", "\\[sl\\] capturing",
                     "\\[sl\\] present", "\\[sl\\] FG cadence", "\\[sl\\] foreground change", "\\[sl\\] warn", "VK_ERROR", "\\[log\\]"],
        "expect": [
            "\\[auto\\] scene ready",
            "\\[hot\\] core generation \\d+ loaded",
            "\\[cfg\\] settings read",
            # FG off by request, then on again, inside the run
            "\\[sl\\] FG decision frame \\d+: applied 3 -> want 0 requested 0; reason requested-off",
            "\\[sl\\] slDLSSGSetOptions\\(mode off, frames to generate \\d+, flags 0x[0-9a-f]+\\): ok",
            "\\[sl\\] FG decision frame \\d+: applied 0 -> want 3 requested 3; reason stable-inputs",
            "\\[rrpost\\] rr_hair_strand_aa=0:",
            "\\[rrpost\\] rr_hair_strand_aa=2:",
            "\\[hair primary\\] frame \\d+: \\d+ grooms published; .*main G-buffer [1-9]\\d*/\\d+",
            "\\[hair primary\\] frame \\d+: RTX Hair off \\(live\\)",
        ],
        "forbid": [
            "\\[crash\\] exception 0x", "VK_ERROR_DEVICE_LOST", "\\[hot\\] core (rejected|faulted)",
            "FAILED, keeping previous", "compile call failed", "\\[log\\] reached \\d+ MB", "\\[sl\\] .*eWarnOutOfVRAM",
            "PNG write failed", "capture writer backlog",
        ],
        "expect_every": [
            # Only this event gate on the snap windows. hf1 073932 was INVALID because a second, periodic gate
            # ('[sl] DLSS-G frame') made live_flow turn them into stats_reset frame windows: such a window starts at
            # its motion-ready record (07:41:30.365), and the capture, started by the settings read (07:41:29.806),
            # had logged its line before it. The generated frames are now proven by the watch windows' wait condition.
            every("12 presented frames written in every snap window", SNAP_DONE, SNAP_DONE, snaps, event=True),
        ],
        "expect_runner": [
            "\\[window-mode\\].*visible_unfocused",
            f"\\[wheel-input\\] phase=3 notches={ZOOM}\\b",
            f"\\[motion-plan\\] complete phases={len(plan)}\\b",
            "^Run ended: done",
        ],
        "live_flow": {"expected_fps": 30, "settings_latency_s": 1, "trace_stall_s": 3},
        "estimated_phase_seconds": sum(p["seconds"] for p in plan),
        "purpose": "User eyewitness: heads switch on and off quickly at a static camera while the rendered-frame "
                   "numbers are clean. S8, user set: watch windows fg=3, fg=0, fg=3, fg=3 + rr_hair_strand_aa=0, "
                   "fg=3 + cards, each 8 s plus 12 presented frames (fg_snap). The user says in which window the "
                   "toggling stops; the presented captures give the generated-against-rendered difference in the "
                   "head region. STAGED, not authorized.",
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
    fg3 = {"survive-" + w[0] for w in WINDOWS if w[1]}
    for p in tight:
        if p["label"].startswith("survive-"):
            assert p["seconds"] >= 8, (p["label"], p["seconds"])   # the user's watch time is the observable
        if p["label"].startswith("snap-"):
            assert not p.get("stats_reset") and "frames" not in p, p["label"]   # a timed event window (see the gate)
            # live_flow sizes an event window for the request line; the 12th PNG came 1.12 s after the settings
            # read in hf1 073932 (writer thread), so keep 4 s.
            p["seconds"] = p["motion_start"] = p["motion_end"] = 4.0
        if p["label"] in fg3:
            # Wait condition: the 8 s start only after a fresh line shows the generated frames on screen (the runner
            # starts the phase clock at motion-ready). Not shown within 30 s: "pipeline readiness timed out:
            # <label>; missing: <this pattern>", fail-fast. Added after tighten_files, which owns the build witnesses.
            p["wait_log"] = list(p.get("wait_log", [])) + [FG_SHOWN]
            p["wait_log_fresh"] = list(p.get("wait_log_fresh", [])) + [FG_SHOWN]
            p["wait_log_timeout"] = 30
    (HERE / "plans" / f"{NAME}.json").write_text(json.dumps(tight, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"Generated {NAME}: {sum(p['seconds'] for p in tight)} s of phases; no queue staging")


if __name__ == "__main__":
    main()
