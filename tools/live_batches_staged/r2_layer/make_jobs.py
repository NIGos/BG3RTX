"""r2_layer: BLAS chunk size classes and the kept cascade BLAS, off / on / off in one inn launch. STAGED, not queued.

    py -3 -m tools.live_batches_staged.r2_layer.make_jobs

r2-inn: Ultra, RTX Hair On, FG 3x, visible_unfocused_fg. Keys rt_as_size_class and rt_cascade_keep_refused toggled
together. Per state a static window, a wheel dolly out and in twice, and a static energy window (exposure held, float
traces). predicted.json precedes the jobs. Helpers are vram_rightsize's and perf_ab's (one copy). Nothing here writes
a watched queue inbox.
"""
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))
from tools.live_batches_staged.perf_ab import make_jobs as pab
from tools.live_batches_staged.vram_rightsize import make_jobs as vr

BATCH = "tools/live_batches_staged/r2_layer"
KEYS = ("rt_as_size_class", "rt_cascade_keep_refused")
MEM = r"\[mem\] ours \("
STATE = {0: "size classes 0, cascade keep 0", 1: "size classes 1, cascade keep 1"}
PATTERNS = vr.PATTERNS + [r"\[bvh\] BLAS refusals in"]


def keys(on, hold=0):
    return ",".join(f"{k}={int(on)}" for k in KEYS) + f",qa_exposure_hold={hold}"


def window(label, kind, on):
    p = pab.phase(label, 12, keys(on), frames=vr.FRAMES[kind])
    if kind == "dolly":   # mouse wheel notches: out (the demand rises), then back in
        p.update(wheel=-10 if "-out" in label else 10)
    return p


def energy(state, on):
    p = vr.energy(state, on)
    p["set"] = keys(on, hold=1)
    return p


def job():
    warm = pab.phase
    plan = [warm("warmup", 20, keys(0)), warm("warm-on", 6, keys(1)), warm("warm-off", 4, keys(0))]
    for state, on in (("off-a", 0), ("on", 1), ("off-b", 0)):
        plan += [window(f"static-{state}", "static", on)]
        for n in (1, 2):
            plan += [window(f"dolly-{state}-out{n}", "dolly", on), window(f"dolly-{state}-in{n}", "dolly", on)]
        plan += [energy(state, on)]
    plan += [warm("restore", 2, keys(0))]
    name = "r2-inn"
    j = pab.focused_job(name, pab.INN, f"{vr.START},{pab.HAIR_START},{pab.hair('on')},{keys(0)}", plan, [],
                        [r"\[ngx\] RR create .*: Success"],
                        f"BLAS chunk size classes at {pab.INN}, Ultra, RTX Hair On, FG 3x, unfocused: " + ", ".join(KEYS)
                        + " 0 / 1 / 0 live; per state a static window, two wheel dollies out and in, 4 float traces with "
                        "the exposure held. Per window: BLAS chunks reserved, live, pending, free space inside chunks "
                        "over a base chunk, refusals by class, casters kept or left out, GPU ms; energy ON against OFF.",
                        batch=BATCH, live_batch="r2-layer")
    timed = [p["label"] for p in plan if p.get("frames") and not p.get("trace_at")]
    on = [w for w in timed if "-on" in w]
    off = [w for w in timed if "-off-" in w]
    every = vr.every
    j.update(window_mode="visible_unfocused_fg", args=[a for a in j["args"] if a != "-Foreground"],
             timeout=600, max_minutes=10, fail_fast=False, patterns=PATTERNS,
             expect_every=[every(n + " in every window", s, s, timed) for n, s in pab.LINES] +
                          [every("mem ours line in every window", MEM, MEM, timed),
                           every("keys on in every ON window", MEM, STATE[1], on),
                           every("keys off in every OFF window", MEM, STATE[0], off)])
    j["expect"] += [r"\[trace\] frame \d+ written", r"\[rrpost\] exposure held \(qa_exposure_hold=1\) at EV [-0-9.]+"]
    j["post"] = (f"py -3 {BATCH}/analyze.py --job {BATCH}/jobs/{name}.json "
                 '--log "{log}" --runner "{runner}" --out "{log}.perf.md"')
    j["live_flow"].update(startup_only=["fg"], trace_stall_s=3)
    return plan, j


def measured(p):
    if p.get("trace_at"):
        return None
    return vr.FRAMES["dolly" if p.get("wheel") else "static"]


def main():
    assert (HERE / "predicted.json").exists(), "predicted.json precedes the jobs"
    plan, j = job()
    for folder, value in (("plans", plan), ("jobs", j)):
        (HERE / folder).mkdir(exist_ok=True)
        (HERE / folder / f"{j['name']}.json").write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8",
                                                         newline="\n")
    added = pab.retime([j["name"]], here=HERE, measured=measured)
    print("Generated", j["name"], f"- no queue staging; {added} new entries appended to lint_corpus_snapshot.json")


if __name__ == "__main__":
    main()
