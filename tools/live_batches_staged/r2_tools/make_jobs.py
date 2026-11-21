"""r2_tools: gi_far_roof_emission 0 / 1 / 0 at the inn, with one GI replay pack for the PT reference. STAGED, not queued.

    py -3 -m tools.live_batches_staged.r2_tools.make_jobs

r2-inn-roof-em: The Blushing Mermaid (roofed interior, emitters outside the cut-away roof), Ultra (gi_far=1), FG 3x,
visible_unfocused_fg, static camera; gi_far_roof_emission 0 / 1 / 0 live in one launch. Per state a static cost window
of 3 periods and a static energy window (exposure held, float traces). Last, one GI replay pack (replay_pack=1, key 0)
for the offline PT reference: frame_replay re-runs the pack's hit_sun pass with constant 1054 at 0 and 1 against
`--pt-reference` (predicted.json "offline"). predicted.json precedes the jobs. Helpers are perf_ab's (one copy).
r2-d1-roof-em: Test Dialogue1, the release Ultra state of release_final's final-stock-hair-dialogue1 (its exported
profile, stock hair), visible_unfocused_fg, static camera; gi_far_roof_emission 0 / 1 / 0 live (specialization 1054:
each toggle waits for a fresh pipelines-quiet period), a 3-period cost window and a 3-trace energy window per state.
analyze.py judges GI GPU ms, the GI trace median and RT passes against the OFF pair with tools/live_batches/sane_gates.py,
energy within 0.5% (predicted_d1.json). Nothing here writes a watched queue inbox.
"""
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))
from tools.live_batches_staged.perf_ab import make_jobs as pab

BATCH = "tools/live_batches_staged/r2_tools"
PERIOD = pab.PERIOD
STATIC = 3 * PERIOD
TRACES = 3
KEY = "gi_far_roof_emission"
# Ultra already has gi_far=1, gi_hit_sun=1 and rt_hit_emission=1 (common/settings.h, the GI choice of Ultra).
START = pab.USER.replace("dump_images=0", "dump_images=1,dump_png_width=1280") + ",replay_pack=0,replay_pack_cap_mb=2048"
PACK = ("\\[replay\\] pack 1 written: [0-9]+\\.[0-9] MB \\(\\d+ images omitted, \\d+ read by shader\\), frame \\d+, "
        "pass gi_hit_direct, instances [1-9]\\d* -> .+")


def keys(on, hold=0, pack=0):
    return f"{KEY}={int(on)},qa_exposure_hold={hold},replay_pack={pack}"


def energy(state, on):
    """Static, exposure held, TRACES float traces (final_layer's energy phase)."""
    return pab.energy_phase(f"energy-{state}", keys(on, hold=1), TRACES)


def roof_em_job():
    warm = pab.phase
    plan = [warm("warmup", 20, keys(0)), warm("warm-on", 6, keys(1)), warm("warm-off", 4, keys(0))]
    for state, on in (("off-a", 0), ("on", 1), ("off-b", 0)):
        plan += [pab.phase(f"static-{state}", 12, keys(on), frames=STATIC), energy(state, on)]
    plan += [warm("diag-pack", 20, keys(0, pack=1)), warm("restore", 4, keys(0))]
    name = "r2-inn-roof-em"
    j = pab.focused_job(name, pab.INN, f"{START},{keys(0)}", plan, [], ["\\[ngx\\] RR create .*: Success"],
                        "Far roof rule for hidden-hit emission at The Blushing Mermaid, Ultra, FG 3x, unfocused, static "
                        "camera: gi_far_roof_emission 0 / 1 / 0 live. Per window: GI GPU ms and the other cost lines; "
                        "energy of the ON state against the OFF pair; then one GI replay pack (key 0) for the offline "
                        "PT reference A/B of constant 1054.", batch=BATCH, live_batch="r2-tools")
    timed = [p["label"] for p in plan if p.get("frames") and not p.get("trace_at")]
    j.update(window_mode="visible_unfocused_fg", args=[a for a in j["args"] if a != "-Foreground"],
             timeout=540, max_minutes=9, fail_fast=False,
             expect_every=[pab.every(n + " in every window", s, s, timed, min=1) for n, s in pab.LINES])
    j["expect"] += ["\\[trace\\] frame \\d+ written", "\\[rrpost\\] exposure held \\(qa_exposure_hold=1\\) at EV [-0-9.]+",
                    PACK]
    j["forbid"] = j["forbid"] + ["\\[replay\\] pack 1 (?:failed|over budget)", "\\[replay\\] the image read shader"]
    j["patterns"] = j["patterns"] + ["\\[replay\\]", "\\[trace\\]", "\\[rrpost\\] exposure held"]
    j["live_flow"].update(startup_only=["fg", "replay_pack_cap_mb"], trace_stall_s=3)
    j.pop("post", None)
    j["post_gate"] = False   # read by hand: GI ms from the gi lines, energy from dumps.json, PT offline (predicted.json)
    return plan, j


D1 = "Test Dialogue1"
D1_FINAL = ROOT / "tools/live_batches_staged/release_final/final/jobs/final-stock-hair-dialogue1.json"


def d1_roof_em_job():
    """Cross-run Dialogue1 GI cost rose with the key (062427 key 0 against 080029 key 1): one in-run A/B."""
    from tools.live_batches import live_flow as flow
    from tools.live_batches_staged.omm_ab.make_jobs import override
    final, fplan, _ = flow.load_job(D1_FINAL)
    # dev=1 keeps the developer key toggled by the plan (the release policy resets it at dev=0); the exported profile is
    # already resolved, so dev=1 changes no other value (omm_ab does the same).
    start = override(final["set"], keys(0) + ",dev=1")
    warmup = dict(fplan[0], label="warmup", set=override(fplan[0].get("set", "qa_exposure_hold=0"), keys(0)))
    plan = [warmup, pab.phase("warm-on", 6, keys(1)), pab.phase("warm-off", 4, keys(0))]
    for state, on in (("off-a", 0), ("on", 1), ("off-b", 0)):
        plan += [pab.phase(f"d-static-{state}", 12, keys(on), frames=STATIC),
                 pab.energy_phase(f"d-energy-{state}", keys(on, hold=1), TRACES)]
    plan += [pab.phase("restore", 2, keys(1))]
    name = "r2-d1-roof-em"
    j = pab.focused_job(name, D1, start, plan, [], [r"\[ngx\] RR create .*: Success"],
                        "gi_far_roof_emission 0 / 1 / 0 live in Test Dialogue1, release Ultra (stock hair), static camera, "
                        "visible_unfocused_fg: GI GPU ms, the GI trace median and RT passes of ON against the OFF pair "
                        "(cross-run 062427 / 080029 showed about +1 ms GI), energy per state from float traces.",
                        batch=BATCH, live_batch="r2-tools")
    timed = [p["label"] for p in plan if p.get("frames") and not p.get("trace_at")]
    j.update(window_mode="visible_unfocused_fg", args=[a for a in j["args"] if a != "-Foreground"],
             timeout=600, max_minutes=10, fail_fast=False,
             patterns=list(dict.fromkeys(final["patterns"] + pab.PATTERNS + [r"\[trace\]", r"\[rrpost\] exposure held"])),
             forbid=list(dict.fromkeys(final["forbid"] + pab.FORBID)),
             expect_every=[pab.every(n + " in every window", s, s, timed, min=1) for n, s in pab.LINES[:2]])
    j["expect"] += [r"\[trace\] frame \d+ written", r"\[rrpost\] exposure held \(qa_exposure_hold=1\) at EV [-0-9.]+"]
    j["live_flow"] = dict({k: v for k, v in final["live_flow"].items() if k != "fps_windows"}, trace_stall_s=5.18)
    j["post"] = (f"py -3 {BATCH}/analyze.py --job {BATCH}/jobs/{name}.json "
                 '--log "{log}" --runner "{runner}" --out "{log}.perf.md"')
    j["post_gate"] = True
    return plan, j


def measured(p):
    if p.get("trace_at"):
        return None
    return STATIC if p["label"].startswith(("static-", "d-static-")) else 2 * PERIOD


def main():
    assert (HERE / "predicted.json").exists() and (HERE / "predicted_d1.json").exists(), "predicted*.json precede the jobs"
    made = [roof_em_job(), d1_roof_em_job()]
    pab.write_made(HERE, made)
    added = pab.retime([j["name"] for _, j in made], here=HERE, measured=measured)
    # The D1 cap from its final waits (release_qual's watchdog: plan + readiness allowance + 120 s load); the first
    # tightening pass ran with the 180 s pipeline allowance before retime set the 40 s live-toggle waits.
    from tools.live_batches import live_flow as flow
    from tools.live_batches.release_qual.make_final import watchdog
    path = HERE / "jobs" / "r2-d1-roof-em.json"
    j, plan, _ = flow.load_job(path)
    watchdog(j, plan)
    flow.write(path, j)
    print("Generated", ", ".join(j["name"] for _, j in made), f"- no queue staging; {added} new entries appended to "
          "lint_corpus_snapshot.json")


if __name__ == "__main__":
    main()
