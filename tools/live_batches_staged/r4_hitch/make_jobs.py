"""r4_hitch: groom LOD decodes on a worker (rt_hair_groom_async) off / on / off in one inn launch. STAGED, not queued.

    py -3 -m tools.live_batches_staged.r4_hitch.make_jobs

r4-inn: r2_layer's r2-inn (same save, Ultra, RTX Hair On, FG 3x, visible_unfocused_fg, the same windows) with the one
key rt_hair_groom_async toggled. Cause (queue log r2-inn.1.020820): every frame of 90 ms to 1 s in the dollies
followed a synchronous '[hair groom] ... decoded (v3)' line within 30 ms. The decode line says ', worker' when the
worker ran it: the gates check the state through it. predicted.json precedes the jobs. Nothing writes a queue inbox.
"""
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))
from tools.live_batches_staged.perf_ab import make_jobs as pab
from tools.live_batches_staged.vram_rightsize import make_jobs as vr

BATCH = "tools/live_batches_staged/r4_hitch"
KEYS = ("rt_hair_groom_async",)
DECODE = r"\[hair groom\] [0-9a-f]{16} LOD \d+ decoded \(v3"
PATTERNS = vr.PATTERNS + [DECODE, r"\[sl\] warn .*Frame rate over"]


def keys(on, hold=0):
    return ",".join(f"{k}={int(on)}" for k in KEYS) + f",qa_exposure_hold={hold}"


def window(label, kind, on):
    p = pab.phase(label, 12, keys(on), frames=vr.FRAMES[kind])
    if kind == "dolly":   # mouse wheel notches: out (new groom LODs are decoded), then back in
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
    name = "r4-inn"
    j = pab.focused_job(name, pab.INN, f"{vr.START},{pab.HAIR_START},{pab.hair('on')},{keys(0)}", plan, [],
                        [r"\[ngx\] RR create .*: Success"],
                        f"Groom LOD decodes on a worker at {pab.INN}, Ultra, RTX Hair On, FG 3x, unfocused: "
                        "rt_hair_groom_async 0 / 1 / 0 live; per state a static window, two wheel dollies out and in, "
                        "4 float traces with the exposure held. Per window: rendered frame time p99 and max, scene wait "
                        "max, groom decodes in the build and on the worker; energy ON against OFF.",
                        batch=BATCH, live_batch="r4-hitch")
    timed = [p["label"] for p in plan if p.get("frames") and not p.get("trace_at")]
    out_on = [w for w in timed if "-on-out" in w]
    out_off = [w for w in timed if "-off-" in w and "-out" in w]
    every = vr.every
    decodes = lambda n, match, windows: {"name": n, "select": DECODE, "match": match, "windows": windows,
                                         "event": True, "min": 1, "settle_s": 1}
    j.update(window_mode="visible_unfocused_fg", args=[a for a in j["args"] if a != "-Foreground"],
             timeout=600, max_minutes=10, fail_fast=False, patterns=PATTERNS,
             expect_every=[every(n + " in every window", s, s, timed) for n, s in pab.LINES] +
                          [decodes("groom decodes only on the worker in every ON outward dolly", r", worker\)", out_on),
                           decodes("groom decodes in the build in every OFF outward dolly", r"decoded \(v3\)", out_off)])
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
