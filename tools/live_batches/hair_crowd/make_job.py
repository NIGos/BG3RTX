"""Writes the hair_crowd jobs and their plan (README.md). Rerun after editing the phases or gates:

    py -3 tools/live_batches/hair_crowd/make_job.py            # jobs/ and plans/ of this folder

Every converted hairstyle (tools.hair_groom.batch) and the crowd budget (rt_hair_groom_crowd_ksegments,
docs/RTX_HAIR_PRIMARY.md increment 5) in one launch per save: cards / strands in RT / strands on screen / strands
on screen with the crowd budget, shots and cost windows. Built on the hair_primary job (its keys, gates, forbids).
Two saves, so two launches: the queue shares one game only between jobs of the same save.
"""
import argparse
import importlib.util
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location("hair_primary_make_job", HERE.parent / "hair_primary" / "make_job.py")
hp = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(hp)
hg = hp.hg

BATCH = "hair_crowd"
LIVE = "hair-crowd"
SAVES = [("hc1-dialogue1-fg2", "Test Dialogue1"), ("hc2-campsite-fg2", "Campsite - 27h")]
# rt_hair_groom_primary is latched on at startup (hair_primary make_job PRIMARY_START); the live A/B is rt_hair_groom.
CARDS = dict(rt_hair_groom="0", rt_hair_groom_primary="1", rt_hair_fibre="0", rt_hair_groom_crowd_ksegments="0")
SCREEN = dict(CARDS, rt_hair_groom="1")
CROWD = dict(SCREEN, rt_hair_groom_crowd_ksegments="1800")   # the GUI's "2 close characters"


def step(label, seconds, trace=False, **keys):
    return hg.step(label, seconds, trace, **keys)


SHOT = 8
plan = [
    step("warmup", 25, qa_exposure_hold="0", **SCREEN),    # startup: hair + groom (every converted style), variants
    step("settle-cam", 10, qa_exposure_hold="0", **CARDS),
    step("warm-strands", 20, qa_exposure_hold="0", **SCREEN),
    step("warm-crowd", 15, qa_exposure_hold="0", **CROWD),
    step("warm-cards", 10, qa_exposure_hold="0", **CARDS),   # every judged key value runs once before warm-back
    step("warm-back", 10, qa_exposure_hold="0", **CARDS),
    step("shot-cards", SHOT, True, **CARDS),
    step("shot-strands", SHOT, True, **SCREEN),
    step("shot-crowd", SHOT, True, **CROWD),
    step("shot-cards-b", SHOT, True, **CARDS),
    step("settle", 5, **CARDS),
    step("cost-cards", 25, **CARDS),
    step("cost-strands", 25, **SCREEN),
    step("cost-crowd", 25, **CROWD),
    step("cost-cards-b", 25, **CARDS),
    step("restore", 5, qa_exposure_hold="0", **CARDS),
]
# hair_primary's job and gates read these module globals: this batch's plan.
hp.BATCH, hp.LIVE, hp.plan = BATCH, LIVE, plan
hp.labels = [p["label"] for p in plan]
hp.shots = [l for l in hp.labels if l.startswith("shot-")]
hp.cost = [l for l in hp.labels if l.startswith("cost-")]
hp.judged = hp.shots + hp.cost   # warm-ups are never judged
hp.traced = [p["label"] for p in plan if p.get("trace_at")]
hp.seconds = sum(p["seconds"] for p in plan)
GROOM_ON = ["cost-strands", "cost-crowd"]


def job(name, save):
    j = hp.job(name, save)
    j["args"][-1] = f"captures/live_batches/{BATCH}/plans/hc_plan.json"
    # The user may be at the PC: the game never takes focus (AGENTS.md). A full pool is a measured outcome here
    # (the crowd budget's point), not a failure; refusals are counted by the strand gates.
    j["window_mode"] = "visible_unfocused_fg"
    j["args"] = [a for a in j["args"] if a != "-Foreground"]
    j["expect_runner"] = [r"window-mode.*visible_unfocused" if "pointer centred" in e else e for e in j["expect_runner"]]
    j["forbid"] = [p for p in j["forbid"] if p not in (r"\[hair primary\] .*refused", r"\[bvh\] mesh pool full at")]
    j["forbid"] += [r"\[hair primary\].*\b[1-9]\d* pipelines refused", r"(?i)device (?:was )?lost"]
    j["patterns"] += ["pool full"]
    j["expect"] = [e for e in j["expect"] if "hair fibre" not in e]
    j["expect"].append(r"\[hair groom\] crowd plan: \d+ instances, budget 1800 k segments")
    g = []
    for x in j["expect_every"]:
        if "fibre" in x["name"]:
            continue
        if x["name"].startswith("strand participation") or x["name"].startswith("strand cost"):
            x = dict(x, windows=["cost-strands", "cost-crowd"])
        if x["name"].startswith("groom participation"):
            x = dict(x, windows=GROOM_ON)
        if x.get("windows") and "warmup" in x["windows"]:   # startup bursts are not judged
            x = dict(x, windows=hp.judged)
        g.append(x)
    g += [
        {"name": "crowd plan: logged in the crowd cost window (budget 1800 k segments)",
         "select": r"\[hair groom\] crowd plan:", "match": r"budget 1800 k segments: LOD \d+/\d+/\d+, \d+ keep their cards",
         "min": 1, "windows": ["cost-crowd"]},
    ]
    for i, x in enumerate(g):   # the strand, groom and crowd lines log every 300 frames (layer/hair_primary.cpp, bvhsplit)
        if x["name"].startswith(("strand ", "groom ", "crowd ")):
            g[i] = dict(x, period_frames=300)
    j["expect_every"] = g
    j["live_flow"] = dict(settings_latency_s=1, trace_stall_s=1, startup_only=["rt_hair_groom_primary"])
    j["purpose"] = ("Every converted hairstyle and the crowd budget (docs/RTX_HAIR_PRIMARY.md increment 5): cards / strands "
                    "in RT / strands on screen / strands with rt_hair_groom_crowd_ksegments=1800 in one launch; groom "
                    "instances and matched styles per scene, cost per mode (rt passes, gi, scene build GPU, strand G-buffer "
                    "draws), the crowd plan's LOD mix, full-resolution shots for the user.")
    return j


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(HERE))
    a = ap.parse_args()
    out = Path(a.out)
    (out / "jobs").mkdir(parents=True, exist_ok=True)
    (out / "plans").mkdir(parents=True, exist_ok=True)
    (out / "plans" / "hc_plan.json").write_text(json.dumps(plan, indent=1) + "\n", encoding="utf-8")
    for name, save in SAVES:
        (out / "jobs" / f"{name}.json").write_text(json.dumps(job(name, save), indent=1) + "\n", encoding="utf-8")
    print(f"{len(SAVES)} jobs; hc_plan {len(plan)} phases/{hp.seconds} s -> {out}")


if __name__ == "__main__":
    main()
