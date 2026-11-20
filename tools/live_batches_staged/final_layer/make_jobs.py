"""final_layer: rt_hold_pack_base 0 / 1 / 0 at S8, and the two-launch strand header check. STAGED, not queued.

    py -3 -m tools.live_batches_staged.final_layer.make_jobs

fl-s8-holdpack: Ultra, FG 3x, visible_unfocused_fg, static camera; rt_hold_pack_base 0 / 1 / 0 live in one launch, per
state a static window of 3 periods and a static energy window (exposure held, float traces).
fl-header-early: two launches (repeat 2) with rt_hair_header_early=1 from the start; the strand header address and the
strand variants' pipeline time of each launch are read from the logs. predicted.json precedes the jobs. Helpers are
perf_ab's and vram_rightsize's (one copy). Nothing here writes a watched queue inbox.
"""
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))
from tools.live_batches_staged.perf_ab import make_jobs as pab

BATCH = "tools/live_batches_staged/final_layer"
PERIOD = pab.PERIOD
STATIC = 3 * PERIOD
TRACES = 3
START = pab.USER.replace("dump_images=0", "dump_images=1,dump_png_width=1280")
HOLD = "\\[bvh\\] last \\d+ builds, per build: .*hold pack \\d+ jobs"


def keys(on, hold=0):
    return f"rt_hold_pack_base={int(on)},qa_exposure_hold={hold}"


def energy(state, on):
    """Static, exposure held, TRACES float traces (vram_rightsize's energy phase)."""
    return pab.energy_phase(f"energy-{state}", keys(on, hold=1), TRACES)


def holdpack_job():
    warm = pab.phase
    plan = [warm("warmup", 20, keys(0)), warm("warm-on", 6, keys(1)), warm("warm-off", 4, keys(0))]
    for state, on in (("off-a", 0), ("on", 1), ("off-b", 0)):
        plan += [pab.phase(f"static-{state}", 12, keys(on), frames=STATIC), energy(state, on)]
    plan += [warm("restore", 2, keys(0))]
    name = "fl-s8-holdpack"
    j = pab.focused_job(name, pab.S8, f"{START},{keys(0)}", plan, [], ["\\[ngx\\] RR create .*: Success"],
                        "Hold snapshot pack at Water Queens House, Ultra, FG 3x, unfocused, static camera: "
                        "rt_hold_pack_base 0 / 1 / 0 live. Per window: hold pack and copy bytes per build, pool used, "
                        "scene build GPU ms, held draws; energy of the ON state against the OFF pair.",
                        batch=BATCH, live_batch="final-layer")
    timed = [p["label"] for p in plan if p.get("frames") and not p.get("trace_at")]
    j.update(window_mode="visible_unfocused_fg", args=[a for a in j["args"] if a != "-Foreground"],
             timeout=480, max_minutes=8, fail_fast=False,
             expect_every=[pab.every(n + " in every window", s, s, timed, min=1) for n, s in pab.LINES] +
                          [pab.every("hold pack line in every window", HOLD, HOLD, timed, min=1)])
    j["expect"] += ["\\[trace\\] frame \\d+ written", "\\[rrpost\\] exposure held \\(qa_exposure_hold=1\\) at EV [-0-9.]+"]
    j["live_flow"].update(startup_only=["fg"], trace_stall_s=3)
    j.pop("post", None)
    j["post_gate"] = False   # read by hand: bytes from the [bvh] line, energy from dumps.json (predicted.json)
    return plan, j


def header_job():
    plan = [pab.phase("warmup", 20, "rt_hair_header_early=1"), pab.phase("static", 12, "rt_hair_header_early=1",
                                                                         frames=2 * PERIOD)]
    name = "fl-header-early"
    j = pab.focused_job(name, pab.S8, f"{pab.USER},rt_hair_header_early=1", plan, [],
                        ["\\[hair primary\\] header at 0x[0-9a-f]+: static hair pipelines get strand variants",
                         "\\[hair primary\\] strand variants: 519 pipelines in [0-9.]+ ms"],
                        "Strand header at device creation (rt_hair_header_early=1), two launches at Water Queens "
                        "House: the same header address in both, and the second launch's 519 strand variants under "
                        "1 s of pipeline creation.", batch=BATCH, live_batch="final-layer")
    j.update(window_mode="visible_unfocused_fg", args=[a for a in j["args"] if a != "-Foreground"], repeat=2,
             timeout=300, max_minutes=5, fail_fast=False)
    j.pop("post", None)
    j["post_gate"] = False   # compare the two logs' header and strand variant lines (predicted.json)
    return plan, j


def measured(p):
    if p.get("trace_at"):
        return None
    return STATIC if p["label"].startswith("static-") else 2 * PERIOD


def main():
    assert (HERE / "predicted.json").exists(), "predicted.json precedes the jobs"
    made = [holdpack_job(), header_job()]
    pab.write_made(HERE, made)
    added = pab.retime([j["name"] for _, j in made], here=HERE, measured=measured)
    print("Generated", ", ".join(j["name"] for _, j in made), f"- no queue staging; {added} new entries appended to "
          "lint_corpus_snapshot.json")


if __name__ == "__main__":
    main()
