"""hair_async: hx1-s8-hair-stable with rt_hair_groom_async=1 from the start. STAGED, not queued, not authorized.

    py -3 -m tools.live_batches_staged.hair_async.make_job

hair_stable's plan and gates (one Water Queens House launch, the RTX Hair On bundle) with the groom LOD decodes on the
worker (claude/hair-async puts the key into the bundle). Added gates: every decode of the moving windows ran on the
worker, and no groom was dropped to cards on a LOD switch ('[hair groom] frame' line). Reported: the groom-frames kept
on the built LOD while a decode is pending, and hx1's card runs of entering heads. predicted.json precedes the job.
"""
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))
from tools.live_batches_staged.hair_stable import make_job as hx1

NAME = "hx1a-s8-hair-async"
PLAN = f"tools/live_batches_staged/hair_async/plans/{NAME}.json"
DECODE = r"\[hair groom\] [0-9a-f]{16} LOD \d+ decoded \(v3"
GROOM = r"\[hair groom\] frame \d+: \d+ groom instances; last"
MOVING = hx1.ON_DOLLY + hx1.ON_ORBIT + hx1.NOKEEP + hx1.ON_AGAIN
BUNDLE = hx1.ON_DOLLY + hx1.ON_ORBIT + hx1.ON_AGAIN


def make_job(plan):
    j = hx1.make_job(plan)
    s = j["set"] if "rt_hair_groom_async=1" in j["set"] else j["set"] + ",rt_hair_groom_async=1"
    j.update(name=NAME, live_batch="hair-async", set=s,
             args=["-RealPresent", "-Move", "-MovePlan", PLAN])
    assert "rt_hair_groom_async=1" in j["set"]
    j["expect"] += [r"\[hair groom\] [0-9a-f]{16} LOD \d+ decoded \(v3, worker\)"]
    j["expect_every"] += [
        hx1.every("every groom decode of the moving windows ran on the worker", DECODE, r", worker\)", MOVING,
                  event=True, min=0),
        hx1.every("no groom dropped to cards on a LOD switch (bundle windows)", GROOM,
                  r"waiting for upload \d+ \(kept\) / 0 \(groom dropped\)", BUNDLE, **hx1.DOLLY),
        hx1.every("groom-frames kept on the built LOD while a decode is pending (report)", GROOM,
                  r"waiting for upload \d+ \(kept\)", MOVING, min=0, **hx1.DOLLY),
        hx1.every("groom decodes per window (report)", DECODE, r"decoded", MOVING, event=True, min=0),
    ]
    j["purpose"] = ("hair_async: hx1's RTX Hair On bundle with rt_hair_groom_async=1 from the start. hx1's gates, plus: "
                    "every decode of the moving windows on the worker, no groom dropped to cards on a LOD switch. "
                    "Reported: groom-frames kept while a decode is pending, card runs of entering heads. STAGED, "
                    "not authorized.")
    return j


def main():
    assert (HERE / "predicted.json").exists(), "predicted.json precedes the job"
    hx1.check_bundle()   # hx1.HAIR is the RTX Hair On bundle, with rt_hair_groom_async=1 since claude/hair-async
    plan = hx1.make_plan()
    for folder, value in (("plans", plan), ("jobs", make_job(plan))):
        (HERE / folder).mkdir(exist_ok=True)
        (HERE / folder / f"{NAME}.json").write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8", newline="\n")
    from tools.live_batches import live_flow as flow, lint_snapshot
    # Append-only, as perf_ab's retime: the readiness queries and expect/forbid patterns the frozen snapshot has not
    # seen (this job's, and hair_stable's whose bundle gained the key), then the frozen tightening.
    for folder, names in ((HERE, [NAME]), (hx1.HERE, [hx1.NAME])):   # the live corpus's readiness waits first
        flow.tighten_files(folder, job_names=names)
    lint_snapshot.extend_readiness(sorted(HERE.glob("jobs/*.json")) + sorted(hx1.HERE.glob("jobs/*.json")))
    data = flow.read(lint_snapshot.PATH)
    new = sorted({rx for path in [*HERE.glob("jobs/*.json"), *hx1.HERE.glob("jobs/*.json")] for key in ("forbid", "expect")
                  for rx in flow.read(path).get(key, [])} - data["matches"].keys())
    if new:
        data["matches"].update({rx: [0, None] for rx in new})
        flow.write(lint_snapshot.PATH, data)
    flow.tighten_files(HERE, job_names=[NAME], frozen=True)
    print(f"Generated {NAME}; no queue staging")


if __name__ == "__main__":
    main()
