"""vram_rightsize: the five VRAM right-sizing policies off / on / off in one launch per scene. STAGED, not queued.

    py -3 -m tools.live_batches_staged.vram_rightsize.make_jobs

vr-s8 and vr-inn: Ultra, RTX Hair On, FG 3x, visible_unfocused_fg (the game never takes the focus). Per state a static
window, a pan Q and E back, a wheel dolly out and in, and a static energy window (exposure held, float traces).
predicted.json precedes the jobs. Helpers are perf_ab's and handout_qual's (one copy). Nothing here writes a watched
queue inbox.
"""
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))
from tools.live_batches_staged.perf_ab import make_jobs as pab

BATCH = "tools/live_batches_staged/vram_rightsize"
PERIOD = pab.PERIOD
# Measured frames after the 60-frame settle. Static: 3 periods (the capture shrink needs 120 low samples, the pool
# trim a quiet window of 254 frames). A moving window: 2 periods per direction.
FRAMES = {"static": 3 * PERIOD, "pan": 2 * PERIOD, "dolly": 2 * PERIOD}
TRACES = 4
KEYS = ("rt_capture_peak_reserve", "rt_capture_demand_size", "rt_capture_split_mb", "rt_mesh_pool_trim", "rt_as_chunk_fit")
START = pab.USER.replace("dump_images=0", "dump_images=1,dump_png_width=1280")
POOLS = "\\[mem\\] pools reserved/used MB: "
STATE = {0: "policies chunk_fit=0 demand_size=0 split_mb=0 pool_trim=0 peak_reserve=0",
         1: "policies chunk_fit=1 demand_size=1 split_mb=1 pool_trim=1 peak_reserve=1"}
PAN = dict(kind="pan", speed="slow", repeat_ms=100, key_period_ms=400, key_down_ms=100)   # opt_profile's keyboard pan
PATTERNS = pab.PATTERNS + ["\\[geo\\] RT capture", "\\[cascade\\] (?:capture|casters)", "\\[bvh\\] mesh (?:cache|pool)",
                           "\\[trace\\]", "\\[rrpost\\] exposure"]


def keys(on, hold=0):
    return ",".join(f"{k}={int(on)}" for k in KEYS) + f",qa_exposure_hold={hold}"


def window(label, kind, on):
    p = pab.phase(label, 12, keys(on), frames=FRAMES[kind])
    if kind == "pan":
        p.update(PAN, direction=1 if label.endswith("-e") else -1)
    if kind == "dolly":   # mouse wheel notches: out (the demand rises), then back in
        p.update(wheel=-10 if label.endswith("-out") else 10)
    return p


def energy(state, on):
    """Static, exposure held, TRACES float traces (handout_qual's energy phase)."""
    return pab.energy_phase(f"energy-{state}", keys(on, hold=1), TRACES)


def every(*a, **kw):
    return pab.every(*a, **dict(kw, min=1))


def job(slug, save):
    warm = pab.phase
    plan = [warm("warmup", 20, keys(0)), warm("warm-on", 6, keys(1)), warm("warm-off", 4, keys(0))]
    for state, on in (("off-a", 0), ("on", 1), ("off-b", 0)):
        plan += [window(f"static-{state}", "static", on), window(f"pan-{state}-q", "pan", on),
                 window(f"pan-{state}-e", "pan", on), window(f"dolly-{state}-out", "dolly", on),
                 window(f"dolly-{state}-in", "dolly", on), energy(state, on)]
    plan += [warm("restore", 2, keys(0))]
    name = f"vr-{slug}"
    j = pab.focused_job(name, save, f"{START},{pab.HAIR_START},{pab.hair('on')},{keys(0)}", plan, [],
                        ["\\[ngx\\] RR create .*: Success"],
                        f"VRAM right-sizing at {save}, Ultra, RTX Hair On, FG 3x, unfocused: the five policies ("
                        + ", ".join(KEYS) + ") 0 / 1 / 0 live, per state a static window, pan Q and E, wheel dolly "
                        "out and in, and 4 float traces with the exposure held. Per window: reserved and used MB per "
                        "pool, VRAM used and budget, reallocation, refusal and left-out counts, GPU ms, frame time "
                        "max and p99, scene wait max; energy of the ON state against the OFF pair.",
                        batch=BATCH, live_batch="vram-rightsize")
    timed = [p["label"] for p in plan if p.get("frames") and not p.get("trace_at")]
    on = [w for w in timed if "-on" in w]
    off = [w for w in timed if "-off-" in w]
    # Runner gates: the measured lines exist in the windows that print them and the keys' state is the planned one
    # (AGENTS.md sensible gates). Values (MB, left-out counts, GPU ms, energy) are judged by analyze.py.
    j.update(window_mode="visible_unfocused_fg", args=[a for a in j["args"] if a != "-Foreground"],
             timeout=840, max_minutes=14, fail_fast=False, patterns=PATTERNS,
             expect_every=[every(n + " in every window", s, s, timed) for n, s in pab.LINES] +
                          [every("pools line in every window", POOLS, POOLS, timed),
                           every("policies on in every ON window", POOLS, STATE[1], on),
                           every("policies off in every OFF window", POOLS, STATE[0], off),
                           every("RT capture line in every window", "\\[geo\\] RT capture, last \\d+ frames:",
                                 "\\[geo\\] RT capture, last \\d+ frames:", timed)])
    j["expect"] += ["\\[trace\\] frame \\d+ written", "\\[rrpost\\] exposure held \\(qa_exposure_hold=1\\) at EV [-0-9.]+"]
    j["live_flow"].update(startup_only=["fg"], trace_stall_s=3)
    return plan, j


def measured(p):
    if p.get("trace_at"):
        return None
    return FRAMES["pan" if p.get("kind") == "pan" else "dolly" if p.get("wheel") else "static"]


def main():
    assert (HERE / "predicted.json").exists(), "predicted.json precedes the jobs"
    made = [job("s8", pab.S8), job("inn", pab.INN)]
    pab.write_made(HERE, made)
    added = pab.retime([j["name"] for _, j in made], here=HERE, measured=measured)
    print("Generated", ", ".join(j["name"] for _, j in made), f"- no queue staging; {added} new entries appended to "
          "lint_corpus_snapshot.json")


if __name__ == "__main__":
    main()
