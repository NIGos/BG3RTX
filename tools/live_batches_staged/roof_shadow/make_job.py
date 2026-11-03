"""roof_shadow: rt_sun_roof_world off / on / off in one launch at the port dialogue. STAGED, not queued.

    py -3 -m tools.live_batches_staged.roof_shadow.make_job

rs1-dialogue3-roof-world: Test Dialogue3 (static dialogue shot under the port roofs), Ultra, FG 3x,
visible_unfocused_fg. Per state a static window of two complete sun stats periods and a static energy window (exposure
held, float traces with PNG previews). predicted.json precedes the job. Helpers are perf_ab's (one copy). Nothing here
writes a watched queue inbox.
"""
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))
from tools.live_batches_staged.perf_ab import make_jobs as pab

BATCH = "tools/live_batches_staged/roof_shadow"
NAME, SAVE = "rs1-dialogue3-roof-world", "Test Dialogue3"
# core/sun_trust.cpp prints "sun stats" every 600 sampled frames and has no stats reset: the counters of a state's
# first line mix two states (its roof volumes and roof query are the print-time state), so a window holds two lines
# (min 2) plus perf_ab's 60-frame settle; analyze.py reads the last one.
SUN_PERIOD = 600
FRAMES = 2 * SUN_PERIOD
TRACES = 4
KEY = "rt_sun_roof_world"
FIXED = ("rt_shadows=1,rt_sun_replace=1,rt_sun_roofs=1,rt_sun_roof_mode=0,rt_sun_roof_pad=3.0,rt_sun_roof_up=50.0,"
         "rt_sun_roof_radius=0,rt_cascade_casters=1")
START = pab.USER.replace("dump_images=0", "dump_images=1,dump_png_width=1280")
SUN = "sun stats \\(\\d+ frames, replace 1"
PATTERNS = pab.PATTERNS + ["sun: ", "sun stats", "sun check", "\\[trace\\]", "\\[rrpost\\] exposure"]


def keys(on, hold=0):
    return f"{KEY}={int(on)},qa_exposure_hold={hold}"


def energy(state, on):
    """Static, exposure held, TRACES float traces 5 s apart (handout_qual's energy phase): energy and the band."""
    p = pab.phase(f"energy-{state}", 14.2 + 5 * (TRACES - 3), keys(on, hold=1), frames=60)
    p.update(trace_at=[1 + 5 * i for i in range(TRACES)], motion_start=17 + 5 * (TRACES - 3),
             motion_end=17 + 5 * (TRACES - 3))
    return p


def sun(name, match, windows):
    return dict(pab.every(name, SUN, match, windows), period_frames=SUN_PERIOD, sample_frames=SUN_PERIOD, min=2)


def job():
    plan = [pab.phase("warmup", 20, keys(0)), pab.phase("warm-on", 4, keys(1)), pab.phase("warm-off", 4, keys(0))]
    for state, on in (("off-a", 0), ("on", 1), ("off-b", 0)):
        plan += [pab.phase(f"static-{state}", 12, keys(on), frames=FRAMES), energy(state, on)]
    plan += [pab.phase("restore", 2, keys(0))]
    j = pab.focused_job(NAME, SAVE, f"{START},{FIXED},{keys(0)}", plan, [], ["\\[ngx\\] RR create .*: Success"],
                        f"Whole-level roof selection ({KEY}) 0 / 1 / 0 live at {SAVE}, Ultra, FG 3x, unfocused, static "
                        "dialogue shot: per state two sun stats periods and 4 float traces with the exposure held. "
                        "Per window: roof volumes and roof query of every sun stats line, px under roofs, kept by "
                        "roofs, rt passes GPU ms; energy of the ON state against the OFF pair; the PNGs show whether "
                        "the band over the left character still moves.",
                        batch=BATCH, live_batch="roof-shadow")
    timed = [p["label"] for p in plan if p.get("frames") and not p.get("trace_at")]
    on = [w for w in timed if w.endswith("-on")]
    off = [w for w in timed if "-off-" in w]
    j.update(window_mode="visible_unfocused_fg", args=[a for a in j["args"] if a != "-Foreground"],
             timeout=600, max_minutes=10, fail_fast=False, patterns=PATTERNS,
             expect_every=[pab.every(n + " in every window", s, s, timed, min=1) for n, s in pab.LINES] +
                          [sun("whole-level query in the ON window", "roof query -1 m", on),
                           sun("camera query in every OFF window", "roof query \\d+ m", off),
                           sun("no roof volume dropped in any window", "over kSunMaxVolumes on 0 frames", timed)])
    # The enable line: every roof of the level uploaded (total = uploaded).
    j["expect"] += ["sun: world roof selection: level \\S+, (\\d+) volumes \\(\\1 uploaded\\)",
                    "\\[trace\\] frame \\d+ written", "\\[rrpost\\] exposure held \\(qa_exposure_hold=1\\) at EV [-0-9.]+"]
    j["live_flow"].update(startup_only=["fg"], trace_stall_s=3)
    return plan, j


def measured(p):
    return None if p.get("trace_at") else FRAMES


def main():
    assert (HERE / "predicted.json").exists(), "predicted.json precedes the job"
    plan, j = job()
    for folder, value in (("plans", plan), ("jobs", j)):
        (HERE / folder).mkdir(exist_ok=True)
        (HERE / folder / f"{NAME}.json").write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8", newline="\n")
    added = pab.retime([NAME], here=HERE, measured=measured)
    print("Generated", NAME, f"- no queue staging; {added} new entries appended to lint_corpus_snapshot.json")


if __name__ == "__main__":
    main()
