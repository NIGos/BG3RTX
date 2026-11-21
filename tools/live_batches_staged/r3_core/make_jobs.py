"""r3_core: rr_post_jitter_align 0 / 1 / 0 in Test Dialogue3 and rt_sun_overlap 0 / 1 / 0 at S8. STAGED, not queued.

    py -3 -m tools.live_batches_staged.r3_core.make_jobs

r3-d3-jitter: Test Dialogue3, High (rr_post_temporal is off there, so the plain `final` grade runs), static camera,
visible_unfocused_fg. Per state a settle, a rendered K8 burst of the rr_post A/C and B images (pan_post, with the
a presented K8 capture and one float energy trace (its "[rrpost] energy jitter" line names the shift used).
r3-s8-sun-overlap: Water Queens House, Ultra (the user's configuration), static camera; three 3-period cost windows
off / on / off. The decision line is "rt passes: GPU cost" (the total RT interval: with the overlap the sun trace runs
inside the GI trace's interval and has no per-pass line of its own).
predicted.json precedes the jobs. Helpers are perf_ab's (one copy). Nothing here writes a watched queue inbox.
"""
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))
from tools.live_batches import live_flow as flow
from tools.live_batches_staged.perf_ab import make_jobs as pab

BATCH = "tools/live_batches_staged/r3_core"
PERIOD = pab.PERIOD
STATIC = 3 * PERIOD
ORDER = (("off-a", 0), ("on", 1), ("off-b", 0))
SERIAL = 98900   # rendered K8 bursts: 98900 + window index
D3 = "Test Dialogue3"


def d3_jitter_job():
    key = "rr_post_jitter_align"
    base = f"rr_post_temporal=0,pan_post=1,pan_edge=1,pan_prepare=1,pan_capture=0,fg_snap=0,qa_exposure_hold=1"

    def keys(on, extra=""):
        return f"{key}={on},{base}" + (f",{extra}" if extra else "")

    plan = [pab.phase("warmup", 20, keys(0, "qa_exposure_hold=0")), pab.phase("warm-on", 6, keys(1)),
            pab.phase("warm-off", 4, keys(0))]
    every, traces = [], []
    for i, (label, on) in enumerate(ORDER):
        plan.append(pab.phase(f"settle-{label}", 5, keys(on)))
        rendered = pab.phase(f"rendered-{label}", 5, keys(on, f"pan_capture={SERIAL + i}"))
        rendered.update(serial=SERIAL + i, capture_at=.1)
        plan += [rendered, pab.phase(f"presented-{label}", 5, keys(on, "fg_snap=8"))]
        energy = pab.phase(f"energy-{label}", 5, keys(on))
        energy["trace_at"] = [1]
        plan.append(energy)
        traces.append(rf"\[motion-trace\] phase={len(plan) - 1} label=energy-{label} repeat=0")
        every += [
            dict(name=f"AC/EB K8 {label}", select=rf"\[pan\] burst {SERIAL + i} complete",
                 match=rf"\[pan\] burst {SERIAL + i} complete: 8 frames, \d+ samples, transport ok", event=True, min=1,
                 windows=[f"rendered-{label}"]),
            dict(name=f"shift logged {label}", select=r"\[rrpost\] energy jitter",
                 match=rf"\[rrpost\] energy jitter [-\d.e+]+ [-\d.e+]+, render \d+x\d+, temporal 0, align {on}\b",
                 event=True, min=1, windows=[f"energy-{label}"]),
            dict(name=f"presented K8 {label}", select=r"\[sl\] present capture done", match=r"\[sl\] present capture done",
                 event=True, min=1, windows=[f"presented-{label}"]),
            dict(name=f"float energy {label}", select=r"\[trace\] frame \d+ written:", match=r"\[trace\] frame \d+ written:",
                 event=True, min=1, windows=[f"energy-{label}"])]
    plan.append(pab.phase("restore", 4, f"{key}=0,pan_post=0,pan_edge=0,pan_prepare=0,qa_exposure_hold=0"))
    start = (pab.USER.replace("preset=ultra", "preset=high").replace("fg=3", "fg=2")
             + ",dump_png_width=384,fg_snap_any=1,fg_snap_delay_ms=0,fg_letterbox=1," + keys(0, "qa_exposure_hold=0"))
    name = "r3-d3-jitter"
    j = pab.focused_job(name, D3, start, plan, [], ["\\[ngx\\] RR create .*: Success"],
                        "rr_post_jitter_align 0 / 1 / 0 in Test Dialogue3 on High (no temporal post), static camera: "
                        "per state a rendered K8 burst of A/C/B with the logged shift, a presented K8 capture and a "
                        "float energy trace. Decides whether the aligned lookup removes the DoF-edge shimmer, and "
                        "with it the sign of the temporal path's jitter convention.", batch=BATCH, live_batch="r3-core")
    j.update(window_mode="visible_unfocused_fg", args=[a for a in j["args"] if a != "-Foreground"], hold=90,
             timeout=480, max_minutes=8, fail_fast=False, expect_every=every)
    j["expect"] += ["\\[rrpost\\] exposure held \\(qa_exposure_hold=1\\) at EV [-0-9.]+",
                    "\\[rrpost\\] jitter align 1: A/C/B sampled at uv \\+ jitter"]
    j["expect_runner"] += [r"\[window-mode\].*visible_unfocused"] + traces
    j["forbid"] = j["forbid"] + [flow.NONFINITE_EVENT]
    j["patterns"] = j["patterns"] + ["\\[pan\\]", "\\[trace\\]", "\\[rrpost\\]", "\\[sl\\] present capture"]
    j["live_flow"].update(expected_fps=30, trace_stall_s=1)
    j.pop("post", None)
    j["post_gate"] = False   # read by hand: predicted.json "offline" (K8 analysis), energy from dumps.json
    return plan, j


def s8_sun_job():
    key = "rt_sun_overlap"
    plan = pab.toggled(["off", "on"], (("off-a", "off"), ("on", "on"), ("off-b", "off")),
                       lambda s: f"{key}={int(s == 'on')}")
    name = "r3-s8-sun-overlap"
    j = pab.focused_job(name, pab.S8, f"{pab.USER},{key}=0", plan, [], ["\\[ngx\\] RR create .*: Success"],
                        "rt_sun_overlap 0 / 1 / 0 at S8, Ultra, static camera: the total RT interval (rt passes: GPU "
                        "cost) and the frame cost lines per 3-period window.", batch=BATCH, live_batch="r3-core")
    timed = [p["label"] for p in plan if p.get("frames")]
    j.update(window_mode="visible_unfocused_fg", args=[a for a in j["args"] if a != "-Foreground"],
             timeout=420, max_minutes=7, fail_fast=False,
             expect_every=[pab.every(n + " in every window", s, s, timed, min=1) for n, s in pab.LINES])
    j.pop("post", None)
    j["post_gate"] = False   # sane_gates by hand on "rt passes: GPU cost": noise = off-a vs off-b, floor max(0.1 ms, 1%)
    return plan, j


def measured(p):
    if p.get("trace_at") or not p.get("frames"):
        return None
    return STATIC


def main():
    assert (HERE / "predicted.json").exists(), "predicted.json precedes the jobs"
    made = [d3_jitter_job(), s8_sun_job()]
    pab.write_made(HERE, made)
    added = pab.retime([j["name"] for _, j in made], here=HERE, measured=measured)
    print("Generated", ", ".join(j["name"] for _, j in made), f"- no queue staging; {added} new entries appended to "
          "lint_corpus_snapshot.json")


if __name__ == "__main__":
    main()
