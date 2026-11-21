"""r5_ultra_shift: rr_post_jitter_align 0 / 3 / 0 in Test Dialogue3 on Ultra. STAGED, not queued.

    py -3 -m tools.live_batches_staged.r5_ultra_shift.make_jobs

r5-d3-ultra-shift: Test Dialogue3, Ultra (rr_post_temporal=1), static camera, visible_unfocused_fg. Value 3 removes
both jitter terms of the temporal path (layer/rrpost.cpp). Per state a settle, a rendered K8 burst of the temporal
A/C/B and the output grade (pan_post), a presented K8 capture and one float energy trace (its "[rrpost] energy jitter"
line names the output shift: 0 with value 3). Analysis: analyze_jitter.py (r3-d3-jitter's metrics). predicted.json
precedes the job. Helpers are perf_ab's (one copy). Nothing here writes a watched queue inbox.
"""
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))
from tools.live_batches import live_flow as flow
from tools.live_batches_staged.perf_ab import make_jobs as pab

BATCH = "tools/live_batches_staged/r5_ultra_shift"
PERIOD = pab.PERIOD
STATIC = 3 * PERIOD
ORDER = (("off-a", 0), ("on", 3), ("off-b", 0))
SERIAL = 98950   # rendered K8 bursts: 98900 + window index
D3 = "Test Dialogue3"


def d3_jitter_job():
    key = "rr_post_jitter_align"
    base = f"rr_post_temporal=1,pan_post=1,pan_edge=1,pan_prepare=1,pan_capture=0,fg_snap=0,qa_exposure_hold=1"

    def keys(on, extra=""):
        return f"{key}={on},{base}" + (f",{extra}" if extra else "")

    plan = [pab.phase("warmup", 20, keys(0, "qa_exposure_hold=0")), pab.phase("warm-on", 6, keys(3)),
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
                 match=(r"\[rrpost\] energy jitter -?0 -?0, render \d+x\d+, temporal 1, align 0" if on else
                        r"\[rrpost\] energy jitter [-\d.e+]+ [-\d.e+]+, render \d+x\d+, temporal 1, align 0"),
                 event=True, min=1, windows=[f"energy-{label}"]),
            dict(name=f"presented K8 {label}", select=r"\[sl\] present capture done", match=r"\[sl\] present capture done",
                 event=True, min=1, windows=[f"presented-{label}"]),
            dict(name=f"float energy {label}", select=r"\[trace\] frame \d+ written:", match=r"\[trace\] frame \d+ written:",
                 event=True, min=1, windows=[f"energy-{label}"])]
    plan.append(pab.phase("restore", 4, f"{key}=0,pan_post=0,pan_edge=0,pan_prepare=0,qa_exposure_hold=0"))
    start = (pab.USER.replace("fg=3", "fg=2")
             + ",dump_png_width=384,fg_snap_any=1,fg_snap_delay_ms=0,fg_letterbox=1," + keys(0, "qa_exposure_hold=0"))
    name = "r5-d3-ultra-shift"
    j = pab.focused_job(name, D3, start, plan, [], ["\\[ngx\\] RR create .*: Success"],
                        "rr_post_jitter_align 0 / 3 / 0 in Test Dialogue3 on Ultra (temporal post on), static camera: "
                        "per state a rendered K8 burst of the temporal A/C/B, a presented K8 capture and a float "
                        "energy trace. Decides whether the temporal path's jitter terms add background motion.", batch=BATCH, live_batch="r5-ultra-shift")
    j.update(window_mode="visible_unfocused_fg", args=[a for a in j["args"] if a != "-Foreground"], hold=90,
             timeout=480, max_minutes=8, fail_fast=False, expect_every=every)
    j["expect"] += ["\\[rrpost\\] exposure held \\(qa_exposure_hold=1\\) at EV [-0-9.]+",
                    "\\[rrpost\\] temporal jitter shift off \\(rr_post_jitter_align=3"]
    j["expect_runner"] += [r"\[window-mode\].*visible_unfocused"] + traces
    j["forbid"] = j["forbid"] + [flow.NONFINITE_EVENT, "\\[rrpost\\] temporal REFUSED"]
    j["patterns"] = j["patterns"] + ["\\[pan\\]", "\\[trace\\]", "\\[rrpost\\]", "\\[sl\\] present capture"]
    j["live_flow"].update(expected_fps=30, trace_stall_s=1)
    j.pop("post", None)
    j["post_gate"] = False   # read by hand: predicted.json "offline" (K8 analysis), energy from dumps.json
    return plan, j


def measured(p):
    if p.get("trace_at") or not p.get("frames"):
        return None
    return STATIC


def main():
    assert (HERE / "predicted.json").exists(), "predicted.json precedes the jobs"
    made = [d3_jitter_job()]
    pab.write_made(HERE, made)
    added = pab.retime([j["name"] for _, j in made], here=HERE, measured=measured)
    print("Generated", ", ".join(j["name"] for _, j in made), f"- no queue staging; {added} new entries appended to "
          "lint_corpus_snapshot.json")


if __name__ == "__main__":
    main()
