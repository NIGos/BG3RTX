"""level_vote: assetdb_level_vote 0 / 1 / 0 at S8 and in Dialogue1, Ultra. STAGED, READY_FOR_LIVE, not queued.

    py -3 -m tools.live_batches_staged.level_vote.make_jobs

The key is live both ways (layer/assetdb.cpp assetdb_on_present): turned on, the vote arms at once and sets the level a
few presents later; turned off, the level it found is forgotten. So one launch per scene holds the whole A/B:
lv-s8-ultra: Water Queens House, Ultra with OMM off at startup (the gi_hit_sun_em pack contract), visible_unfocused_fg,
static camera: off / on / off cost windows (3 periods each) with an energy window per state, rt_stats diag windows off
and on (the GI far line), and one replay pack per state for the offline PT verdict (predicted.json "offline").
lv-d1-ultra: Test Dialogue1 (camp), the user's Ultra (OMM on): the same windows without the packs.
predicted.json precedes the jobs. Helpers are perf_ab's (one copy). Nothing here writes a watched queue inbox.
"""
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))
from tools.live_batches import live_flow as flow
from tools.live_batches_staged.perf_ab import make_jobs as pab

BATCH = "tools/live_batches_staged/level_vote"
D1 = "Test Dialogue1"
KEY = "assetdb_level_vote"
OMM_OFF = "rt_omm=0,rt_omm_dyn=0,rt_alpha_fine_lod=0"   # common/settings.h exp_omm "off" (restart keys)
LEVEL_ON = "\\[assetdb\\] level {level} \\(from lights\\)"
CLEARED = "\\[assetdb\\] level cleared \\(assetdb_level_vote=0\\)"
# claude/fix-level-clear: a level load re-runs the vote 30 presents later with the after-load clear rule.
LOAD = "\\[assetdb\\] level load at present \\d+: the level vote re-runs in \\d+ presents"
AFTER_LOAD = "\\[assetdb\\] level vote #\\d+ frame \\d+: .*, after a level load: (?:kept|switched)"
VOTE = "\\[assetdb\\] level vote #\\d+ frame \\d+: \\d+ voters"
LS_DB = "ls: per-light data: \\d+ runtime lights .*: bridge \\d+, database [1-9]\\d*;"
SUN_ON = "sun stats \\(\\d+ frames, replace 1, [1-9]\\d* roof volumes"
SUN_OFF = "sun stats \\(\\d+ frames, replace 0, 0 roof volumes \\(level unknown"
FAR_ON = "GI far \\(\\d+ frames\\): rays \\d+; beyond the base radius: screen hits \\d+, qualified sun-lit [1-9]\\d*"
FAR_OFF = "GI far \\(\\d+ frames\\): rays \\d+; beyond the base radius: screen hits \\d+, qualified sun-lit 0 "
EMVAR = "gi: gi_rt_pipeline: the hit-GI trace runs as a ray tracing pipeline, emission variant"
EMBUILD = "\\[em\\] last \\d+ builds, .*; emissive (?:[1-9]\\d*\\.\\d|0\\.[1-9])"
WRITE_S = 90
# Every cost line of the release profile's consumers of the level (per 120-frame period, presence only).
LINES = pab.LINES + [("GI trace GPU ms", "gi: trace pass \\(ray-traced"), ("sun trace GPU ms", "gi: sun trace pass GPU cost"),
                     ("LS GPU ms", "ls: local-light shadow pass GPU median")]


def state(on, extra=""):
    return f"{KEY}={on},qa_exposure_hold=1" + (f",{extra}" if extra else "")


def plan_of(level, packs, reload=False):
    plan = [pab.phase("warmup", 20, state(0, "qa_exposure_hold=0")), pab.phase("warm-on", 4, state(1)),
            pab.phase("warm-off", 4, state(0))]
    plan[1].update(wait_log=[LEVEL_ON.format(level=level)], wait_log_timeout=40)
    plan[2].update(wait_log=[CLEARED], wait_log_timeout=40)
    for label, on in (("off-a", 0), ("on", 1), ("off-b", 0)):
        plan.append(pab.phase(label, 12, state(on), frames=pab.FRAMES))
        plan.append(pab.energy_phase(f"energy-{label}", state(on), traces=2))
    # Expensive diagnostics only in short dedicated windows after every cost window (AGENTS.md).
    for label, on in (("diag-off", 0), ("diag-on", 1)):
        p = pab.phase(label, 6, state(on, "rt_stats=1"), frames=2 * pab.PERIOD)
        plan.append(p)
    if reload:
        # A level load with the vote on (hunt2_lifecycle 2): F8 reloads this run's save (auto-run.ps1 reload_selected).
        # The layer sees a new G-buffer depth image, re-runs the vote 30 presents later with the after-load rule, and
        # must keep (or re-find) the level with its roof volumes: never an old level with 0 volumes.
        p = pab.phase("reload-on", 5, state(1, "rt_stats=0"))
        p.update(reload_selected=True, wait_log=[LOAD, AFTER_LOAD], wait_log_timeout=180)
        plan += [p, pab.phase("after-reload", 12, state(1, "rt_stats=0"), frames=2 * pab.PERIOD)]
    if packs:
        for serial, (label, on) in enumerate((("pack-off", 0), ("pack-on", 1)), 1):
            p = pab.phase(label, 3, state(on, f"rt_stats=0,replay_pack={serial}"))
            p.update(wait_log=[f"\\[replay\\] pack {serial} (?:written|failed|over budget)"], wait_log_timeout=WRITE_S)
            plan.append(p)
    plan.append(pab.phase("restore", 4, f"{KEY}=0,rt_stats=0,replay_pack=0,qa_exposure_hold=0"))
    return plan


def job(name, save, level, start, packs, purpose, reload=False):
    plan = plan_of(level, packs, reload)
    cost = ["off-a", "on", "off-b"]
    gates = [pab.every(n + " in every cost window", s, s, cost) for n, s in LINES[len(pab.LINES):]]
    gates += [pab.every("sun stats with roof volumes (on)", "sun stats \\(", SUN_ON, ["on"]),
              pab.every("sun stats without a level (off)", "sun stats \\(", SUN_OFF, ["off-a", "off-b"]),
              pab.every("GI far with qualified far hits (diag-on)", "GI far \\(", FAR_ON, ["diag-on"], min=1),
              pab.every("GI far without them (diag-off)", "GI far \\(", FAR_OFF, ["diag-off"], min=1)]
    if reload:
        gates.append(pab.every("sun stats with roof volumes after the reload", "sun stats \\(", SUN_ON, ["after-reload"]))
    expect = ["\\[ngx\\] RR create .*: Success", LEVEL_ON.format(level=level), CLEARED, VOTE, LS_DB]
    if reload:
        expect += [LOAD, AFTER_LOAD]
    if packs:
        expect += [EMVAR, EMBUILD] + [f"\\[replay\\] pack {s} written: [0-9]+\\.[0-9] MB .*pass gi_hit_sun_em" for s in (1, 2)]
    j = pab.focused_job(name, save, start, plan, gates, expect, purpose, batch=BATCH, live_batch="level-vote")
    j["expect_every"] = [g for g in j["expect_every"] if g["windows"] != [p["label"] for p in plan if p.get("frames")]]
    j["expect_every"] = [pab.every(n + " in every cost window", s, s, cost) for n, s in pab.LINES] + j["expect_every"]
    j.update(window_mode="visible_unfocused_fg", args=[a for a in j["args"] if a != "-Foreground"], hold=90,
             timeout=600, max_minutes=10, fail_fast=False)
    j["forbid"] = j["forbid"] + [flow.NONFINITE_EVENT, "\\[replay\\] pack \\d+ (?:failed|over budget)"]
    j["patterns"] = j["patterns"] + ["\\[assetdb\\]", "sun stats", "sun check", "GI far", "GI hit sun", "ls: per-light data",
                                     "gi: sun trace pass", "\\[replay\\]", "\\[trace\\]", "gi_rt_pipeline"]
    j["expect_runner"] += [r"\[window-mode\].*visible_unfocused"]
    j["live_flow"].update(startup_only=["fg", "replay_pack_cap_mb", "rt_omm", "rt_omm_dyn", "rt_alpha_fine_lod"])
    j.pop("post", None)
    j["post_gate"] = False   # sane_gates by hand on the cost lines and dumps.json energy; the PT verdict offline
    return plan, j


def measured(p):
    """Frames measured after the settle: 3 periods per cost window, 2 per diag window; energy phases keep theirs."""
    if p.get("trace_at") or not p.get("frames"):
        return None
    return 2 * pab.PERIOD if p["label"].startswith("diag-") else pab.FRAMES


def main():
    assert (HERE / "predicted.json").exists(), "predicted.json precedes the jobs"
    base = f"{pab.USER},{KEY}=0,replay_pack=0,replay_pack_pass=2,replay_pack_cap_mb=2048"
    made = [
        job("lv-s8-ultra", pab.S8, "CTY_Main_A", f"{base},{OMM_OFF},sharc=0", True, reload=True, purpose=
            "assetdb_level_vote 0 / 1 / 0 at Water Queens House, Ultra with OMM off at startup (pack contract), FG 3x, "
            "unfocused, static camera: the level lines, sun stats with roof volumes, the D-LS database lights, GI far "
            "(rt_stats diag windows), GPU ms and float energy per state, one F8 reload of the save with the key on (the "
            "level kept with its roof volumes after the load), and one gi_hit_sun_em pack per state for the offline PT "
            "verdict (predicted.json)."),
        job("lv-d1-ultra", D1, "WLD_Main_A", base, False,
            "assetdb_level_vote 0 / 1 / 0 in Test Dialogue1 (camp), the user's Ultra (OMM on), FG 3x, unfocused, static "
            "camera: the level lines, sun stats with roof volumes, the D-LS database lights, GI far (rt_stats diag "
            "windows), GPU ms and float energy per state."),
    ]
    pab.write_made(HERE, made)
    added = pab.retime([j["name"] for _, j in made], here=HERE, measured=measured)
    for name in ("lv-s8-ultra", "lv-d1-ultra"):   # retime gives every non-warm-up wait 40 s: the pack writer needs more
        j, plan, plan_path = flow.load_job(HERE / "jobs" / f"{name}.json")
        for p in plan:
            if p["label"].startswith("pack-"):
                p["wait_log_timeout"] = WRITE_S
            if p["label"] == "reload-on":
                p["wait_log_timeout"] = 180
        flow.write(plan_path, plan)
    print("Generated", ", ".join(j["name"] for _, j in made), f"- no queue staging; {added} new entries appended to "
          "lint_corpus_snapshot.json")


if __name__ == "__main__":
    main()
