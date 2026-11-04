"""Writes the hair_l2 jobs and their plan (README.md). Rerun after editing the phases or gates:

    py -3 tools/live_batches/hair_l2/make_job.py            # jobs/ and plans/ of this folder

Live L2 of docs/RTX_HAIR_COMPLETE_PLAN.md (M2 shadows + M3 image): strands on screen throughout, then strand shadow
transmittance (rt_hair_groom_shadow_t, C2) and dithered strand tips (rt_hair_groom_tips, C3a) toggled live, each
against the strands-only baseline, in one launch per save. Built on the hair_crowd job (its keys, gates, forbids,
window mode). Two saves, so two launches: the queue shares one game only between jobs of the same save.
"""
import argparse
import importlib.util
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location("hair_crowd_make_job", HERE.parent / "hair_crowd" / "make_job.py")
hc = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(hc)
hp, hg = hc.hp, hc.hg

BATCH = "hair_l2"
LIVE = "hair-l2"
SAVES = [("l2a-dialogue1-fg2", "Test Dialogue1"), ("l2b-campsite-fg2", "Campsite - 27h")]
# rt_hair_groom_primary is latched on at startup (hair_primary make_job PRIMARY_START); strands stay on screen in
# every phase, the A/B keys are the two candidates.
BASE = dict(hc.SCREEN, rt_hair_groom_shadow_t="0", rt_hair_groom_tips="0")
SHADOW = dict(BASE, rt_hair_groom_shadow_t="1")
TIPS = dict(BASE, rt_hair_groom_tips="1")
BOTH = dict(BASE, rt_hair_groom_shadow_t="1", rt_hair_groom_tips="1")


def step(label, seconds, trace=False, **keys):
    return hg.step(label, seconds, trace, **keys)


SHOT = 4
plan = [
    step("warmup", 15, qa_exposure_hold="0", **BASE),     # startup: hair + groom, strand variants
    step("warm-both", 5, qa_exposure_hold="0", **BOTH),   # warm both independent candidates together
    step("shot-base-a", SHOT, True, **BASE),
    step("shot-shadow-a", SHOT, True, **SHADOW),
    step("shot-base-b", SHOT, True, **BASE),
    step("shot-tips-a", SHOT, True, **TIPS),
    step("settle", 5, **BASE),
    step("cost-base-a", 12, **BASE),
    step("cost-shadow", 12, **SHADOW),
    step("cost-tips", 12, **TIPS),
    step("cost-base-b", 12, **BASE),
    step("restore", 5, qa_exposure_hold="0", **BASE),
]
# hair_primary's job and gates read these module globals: this batch's plan.
hp.BATCH, hp.LIVE, hp.plan = BATCH, LIVE, plan
hp.labels = [p["label"] for p in plan]
hp.shots = [l for l in hp.labels if l.startswith("shot-")]
hp.cost = [l for l in hp.labels if l.startswith("cost-")]
hp.judged = hp.shots + hp.cost   # warm-ups are never judged
hp.traced = [p["label"] for p in plan if p.get("trace_at")]
hp.seconds = sum(p["seconds"] for p in plan)
hc.BATCH, hc.LIVE, hc.plan = BATCH, LIVE, plan
SHADOW_ON = ["cost-shadow"]
TRANSMIT = (r"\[hair primary\] shadow transmittance: [1-9]\d* draws measured, a_f mean [0-9.]+ [0-9.]+ [0-9.]+ "
            r"\(per draw [0-9.]+\.\.[0-9.]+\); last 300 frames: [1-9]\d* groom rows with a_f")


# The initial warmup waits for cold prefetch, GI creation and scene readiness.
# The candidate warmup waits for its measured a_f producer instead of a second
# cold-start allowance. The shared retimer derives this 300-frame wait budget.
plan[1].update(wait_log=[TRANSMIT], wait_log_timeout=1)


def job(name, save):
    j = hc.job(name, save)
    j["args"][-1] = f"captures/live_batches/{BATCH}/plans/" + ("l2_plan.json" if name == SAVES[0][0] else name + ".json")
    j["expect"] = [e for e in j["expect"] if "crowd plan" not in e]
    j["expect"].append(TRANSMIT)
    g = []
    for x in j["expect_every"]:
        if x["name"].startswith("crowd plan"):
            continue
        if x["name"].startswith(("strand participation", "strand cost", "groom participation")):
            x = dict(x, windows=hp.cost)   # strands are on in every cost window
        g.append(x)
    g += [
        # Participation: a_f measured for at least one draw and carried by groom rows (else INVALID: the shadow rays
        # saw no transmittance and the A/B compares nothing).
        {"name": "shadow transmittance: draws measured and groom rows with a_f in the transmittance cost window",
         "select": r"\[hair primary\] shadow transmittance:", "match": TRANSMIT, "min": 1, "windows": SHADOW_ON,
         "period_frames": 300},
    ]
    j["expect_every"] = g
    j["purpose"] = ("Live L2 (docs/RTX_HAIR_COMPLETE_PLAN.md M2 + M3): strands on screen throughout; strand shadow "
                    "transmittance (rt_hair_groom_shadow_t) and dithered strand tips (rt_hair_groom_tips) toggled live "
                    "against the strands-only baseline. Full-resolution shots base/shadow/base/tips for the "
                    "user (skin and neck under hair, tip fade), float energy of the pre-post scene colour, cost per mode "
                    "(rt passes, gi, scene build GPU, strand G-buffer draws). Gated: a_f measured while on.")
    return j


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(HERE))
    a = ap.parse_args()
    out = Path(a.out)
    (out / "jobs").mkdir(parents=True, exist_ok=True)
    (out / "plans").mkdir(parents=True, exist_ok=True)
    for name, save in SAVES:
        filename = "l2_plan.json" if name == SAVES[0][0] else name + ".json"
        (out / "plans" / filename).write_text(json.dumps(plan, indent=1) + "\n", encoding="utf-8")
        (out / "jobs" / f"{name}.json").write_text(json.dumps(job(name, save), indent=1) + "\n", encoding="utf-8")
    from tools.live_batches.live_flow import tighten_files
    tighten_files(out)
    print(f"{len(SAVES)} jobs; l2_plan {len(plan)} phases/{hp.seconds} s -> {out}")


if __name__ == "__main__":
    main()
