"""omm_ab: OMM accelerated against reference scene, 1 / 0 / 1, in one S8 launch. STAGED, not queued, not authorized.

    py -3 -m tools.live_batches_staged.omm_ab.make_jobs

oa-s8: the release Ultra profile (release_final's exported state), then the release High profile through its
settings-reset phase, both at Water Queens House, visible_unfocused_fg. Per preset and state (off-a / on / off-b with
rt_omm_reference 1 / 0 / 1, live) the release smoke's camera motions: static, Q and E pans, right and left rotations;
then three float traces per state with the exposure held (energy). rt_omm itself is a restart key. predicted.json
precedes the job; analyze.py judges with tools/live_batches/sane_gates.py. Helpers are perf_ab's (one copy). Nothing
here writes a watched queue inbox.
"""
from copy import deepcopy
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))
from tools.live_batches import live_flow as flow
from tools.live_batches_staged.perf_ab import make_jobs as pab

BATCH = "tools/live_batches_staged/omm_ab"
NAME = "oa-s8"
FINAL = ROOT / "tools/live_batches_staged/release_final/final"
STATES = (("off-a", 1), ("on", 0), ("off-b", 1))
# The release smoke's camera windows (release_qual COST) and their group labels: <preset>-<group>-<state>[-q|-e].
MOTIONS = (("static-a", "static", ""), ("pan-q", "pan", "-q"), ("pan-e", "pan", "-e"),
           ("rotate-r", "rotr", ""), ("rotate-l", "rotl", ""))
MEASURED = pab.PERIOD   # frames after the 60-frame settle: one complete 120-frame period per window (the lint charges
# rt_omm_reference=1 80 ms per frame, fps_windows.COST_MS, so two periods would pass the 15 minute cap on paper)
HANDOUT = r"\[rt\] scene handout: \d+ frames, reserved"
REF = r"gi: rt_omm_reference (\d) \(OMM variants trace the"
LINES = [("rt passes GPU ms", "rt passes: GPU cost"), ("GI GPU ms", "gi: GPU cost"),
         ("scene wait", r"\[rt\] scene wait: \d+ frames"), ("handout", HANDOUT)]
PATTERNS = ["gi: rt_omm_reference", r"\[omm", r"\[rt\] scene", r"\[trace\]", r"\[rrpost\] exposure", r"\[stats\]"]


def keys(reference, hold=0):
    return f"rt_omm_reference={reference},qa_exposure_hold={hold}"


def override(state, extra):
    """`state` (k=v,...) with the keys of `extra` replaced in place (a set string holds each key once)."""
    pairs = dict(kv.split("=", 1) for kv in state.split(","))
    pairs.update(kv.split("=", 1) for kv in extra.split(","))
    return ",".join(f"{k}={v}" for k, v in pairs.items())


def source():
    job, plan, _ = flow.load_job(FINAL / "jobs" / "final-s8-ultra-high.json")
    by = {p["label"].rsplit("--", 1)[-1] + ("@high" if "rel2" in p["label"] else ""): p for p in plan}
    return job, by


def windows(preset, by):
    """The smoke's motion phases (their kind, speed and direction) per state, re-labelled and keyed."""
    out = []
    for state, reference in STATES:
        for smoke, group, side in MOTIONS:
            p = deepcopy(by[smoke + ("@high" if preset == "h" else "")])
            for k in ("wait_log", "wait_log_fresh", "wait_stats"):
                p.pop(k, None)
            p.update(label=f"{preset}-{group}-{state}{side}", set=keys(reference), frames=MEASURED, settle_frames=0,
                     stats_reset=True, wait_stats=[], wait_log_timeout=30)
            out.append(p)
    return out


def energy(preset):
    return [pab.energy_phase(f"{preset}-energy-{state}", keys(reference, hold=1)) for state, reference in STATES]


def warmup(phase, label, reference):
    """A copy of the release smoke's warm-up (its scene readiness waits), keyed."""
    p = deepcopy(phase)
    p.update(label=label, set=override(p["set"], keys(reference)))
    return p


def job():
    final, by = source()
    warm = pab.phase
    # dev=1: the release policy (dev=0) resets the developer knob rt_omm_reference to 0; the exported profile is the
    # resolved state already, so dev=1 changes no other value (perf_ab's USER does the same).
    ultra = override(final["set"], keys(1) + ",dev=1")
    reset = deepcopy(by["settings-reset@high"])
    reset.update(label="h-settings-reset", set=override(reset["set"], keys(1) + ",dev=1"))
    high = reset["set"]
    u = [warmup(by["warmup"], "u-warmup", 1), warm("u-warm-on", 4, keys(0)), warm("u-warm-off", 4, keys(1))]
    u += windows("u", by) + energy("u")
    h = [warmup(by["warmup@high"], "h-warmup", 1), warm("h-warm-on", 4, keys(0)), warm("h-warm-off", 4, keys(1))]
    h += windows("h", by) + energy("h") + [warm("restore", 2, keys(0))]
    plan = u + [reset] + h
    timed = [p["label"] for p in plan if p.get("frames") and not p.get("trace_at")]
    on = [w for w in timed if "-on" in w]
    off = [w for w in timed if "-off-" in w]
    j = pab.focused_job(NAME, pab.S8, ultra, plan, [], [r"\[ngx\] RR create .*: Success"],
                        "OMM at S8, release Ultra then release High (settings reset), visible_unfocused_fg: rt_omm_reference "
                        "1 / 0 / 1 live in the release smoke's motions (static, Q/E pans, R/L rotations) and three float "
                        "traces per state with the exposure held. Per group: RT passes and GI GPU ms ON against the OFF pair, "
                        "energy, OMM downgrades (INFO), handout fallbacks.", batch=BATCH, live_batch="omm-ab")
    every = pab.every
    j.update(window_mode="visible_unfocused_fg", args=[a for a in j["args"] if a != "-Foreground"],
             timeout=900, max_minutes=15, fail_fast=False,
             patterns=list(dict.fromkeys(final["patterns"] + PATTERNS)),
             forbid=list(dict.fromkeys(final["forbid"] + pab.FORBID)),
             expect_every=[every(n + " in every window", s, s, timed, min=1) for n, s in LINES] +
                          [every("handout: 0 fallbacks and 0 pairing mismatches in every window", HANDOUT,
                                 r"in order [1-9]\d*, fallbacks 0 \(.*\), pairing mismatches 0,", timed, min=1),
                           every("handout: the accelerated scene is committed in every window", HANDOUT,
                                 r"OMM accelerated commits [1-9]\d*", timed, min=1)])
    j["expect"] += [r"\[trace\] frame \d+ written", r"\[rrpost\] exposure held \(qa_exposure_hold=1\) at EV [-0-9.]+",
                    "gi: rt_omm_reference 0 \\(OMM variants trace the accelerated scene\\)",
                    "gi: rt_omm_reference 1 \\(OMM variants trace the reference scene\\)"]
    # Two segments as release_final's job: the Ultra phases on the job's state, the High phases on the reset's state
    # (the reset phase itself between them); live_flow re-indexes them when it inserts phases.
    meta = {k: v for k, v in final["live_flow"].items() if k not in ("segments", "fps_windows")}
    j["live_flow"] = dict(meta, trace_stall_s=5.18, segments=[
        dict(offset=0, count=len(u), set=ultra, live_flow=dict(meta)),
        dict(offset=len(u) + 1, count=len(h), set=high, live_flow=dict(meta))])
    j["post"] = (f"py -3 {BATCH}/analyze.py --job {BATCH}/jobs/{NAME}.json "
                 '--log "{log}" --runner "{runner}" --out "{log}.perf.md"')
    assert len(on) == 10 and len(off) == 20, (on, off)
    return plan, j


def measured(p):
    return None if p.get("trace_at") else MEASURED


def main():
    assert (HERE / "predicted.json").exists(), "predicted.json precedes the jobs"
    made = [job()]
    pab.write_made(HERE, made)
    added = pab.retime([NAME], here=HERE, measured=measured)
    # retime sets every window's motion to its end: a moving window moves from 1 s to its end (the smoke's own
    # timing), and keeps moving until its frames are complete (scripts/auto-run.ps1 clamps a frame window's elapsed).
    path = HERE / "plans" / f"{NAME}.json"
    plan = flow.read(path)
    for p in plan:
        if p.get("frames") and p.get("kind") in ("pan", "rotate"):
            p["motion_start"], p["motion_end"] = 1, p["seconds"]
        if p["label"] == "u-warmup":   # the cold-start allowance of the first readiness wait (release_qual main)
            p["wait_log_timeout"] = 180
    flow.write(path, plan)
    j, plan, _ = flow.load_job(HERE / "jobs" / f"{NAME}.json")
    j["estimated_phase_seconds"] = round(sum(p["seconds"] for p in plan), 3)
    flow.write(HERE / "jobs" / f"{NAME}.json", j)
    print("Generated", NAME, f"- no queue staging; {added} new entries appended to lint_corpus_snapshot.json")


if __name__ == "__main__":
    main()
