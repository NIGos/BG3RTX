"""r2_tools/em_pack: one gi_hit_sun_em replay pack at the inn for the offline 1054 A/B against the PT. STAGED, not queued.

    py -3 -m tools.live_batches_staged.r2_tools.em_pack.make_jobs

r2-inn-em-pack: The Blushing Mermaid, Ultra (gi_far, gi_hit_sun, rt_hit_emission on) with OMM off at startup (the pack
contract is the non-OMM request, as the LS pack's), FG 3x, visible_unfocused_fg, static camera, gi_far_roof_emission=0.
After the warm-up and the emission variant running (em-ready), replay_pack_pass=2 + replay_pack=1 take one pack of the emission + sun hit GI (tools/frame_replay.md
"Emission packs"); the diag-pack phase waits for the writer's line (the r2-inn-roof-em run closed the game while the
writer still copied), then the keys are restored. No measured window: capture only. predicted.json precedes the job;
the offline steps are its "offline" list. Helpers are perf_ab's (one copy). Nothing here writes a watched queue inbox.
"""
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(ROOT))
from tools.live_batches import live_flow as flow
from tools.live_batches_staged.perf_ab import make_jobs as pab

BATCH = "tools/live_batches_staged/r2_tools/em_pack"
OMM_OFF = "rt_omm=0,rt_omm_dyn=0,rt_alpha_fine_lod=0"   # common/settings.h exp_omm "off" (restart keys)
START = pab.USER + f",{OMM_OFF},sharc=0,gi_far_roof_emission=0,replay_pack=0,replay_pack_pass=2,replay_pack_cap_mb=2048"
DONE = "\\[replay\\] pack 1 (?:written|failed|over budget)"
PACK = ("\\[replay\\] pack 1 written: [0-9]+\\.[0-9] MB \\(\\d+ images omitted, \\d+ read by shader\\), frame \\d+, "
        "pass gi_hit_sun_em, instances [1-9]\\d* -> .+")
EMISSIVE = "\\[replay\\] pack 1 emissive scene \\(set 4\\): \\d+ texture and \\d+ cache elements bound"
# The emission variant is the one running (logged on the path change; the raygen build took ~80 s after load at the inn,
# r2-inn-em-pack 065644: 06:57:46 compute without the scene, 06:59:10 this line). The one-shot "first hit-GI dispatch"
# line is logged before the first D-EM build and cannot prove it.
EMVAR = "gi: gi_rt_pipeline: the hit-GI trace runs as a ray tracing pipeline, emission variant"
# Periodic (rt_timing_frames): the D-EM scene has emissive draws.
EMBUILD = "\\[em\\] last \\d+ builds, .*; emissive (?:[1-9]\\d*\\.\\d|0\\.[1-9])"
EM_READY_S = 180
WRITE_S = 90   # a 2 GB pack: copies in one frame, then the writer thread (tools/frame_replay.md "Live writer")


def em_pack_job():
    plan = [pab.phase("warmup", 20, "replay_pack=0"), pab.phase("em-ready", 2, "replay_pack=0"),
            pab.phase("diag-pack", 3, "replay_pack=1"), pab.phase("restore", 4, "replay_pack=0")]
    plan[1].update(wait_log=[EMVAR, EMBUILD], wait_log_timeout=EM_READY_S)   # the pack phase starts on the emission variant
    plan[2].update(wait_log=[DONE], wait_log_timeout=WRITE_S)
    name = "r2-inn-em-pack"
    j = pab.focused_job(name, pab.INN, START, plan, [], ["\\[ngx\\] RR create .*: Success", EMVAR, EMBUILD, EMISSIVE, PACK],
                        "One gi_hit_sun_em replay pack at The Blushing Mermaid (Ultra, OMM off, FG 3x, unfocused, static "
                        "camera, gi_far_roof_emission=0) for the offline A/B of constant 1054 against the PT reference "
                        "with emission (predicted.json).", batch=BATCH, live_batch="r2-tools")
    j.update(window_mode="visible_unfocused_fg", args=[a for a in j["args"] if a != "-Foreground"],
             timeout=480, max_minutes=8, fail_fast=True, expect_every=[])
    j["forbid"] = j["forbid"] + ["\\[replay\\] pack 1 (?:failed|over budget)", "\\[replay\\] the image read shader"]
    j["patterns"] = j["patterns"] + ["\\[replay\\]", "hit emission: first", "\\[em\\] emissive capture", "\\[em\\] last", "gi_rt_pipeline"]
    j["live_flow"].update(startup_only=["fg", "replay_pack_cap_mb", "rt_omm", "rt_omm_dyn", "rt_alpha_fine_lod"])
    j.pop("post", None)
    j["post_gate"] = False   # the deciding numbers are offline (predicted.json "offline")
    return plan, j


def main():
    assert (HERE / "predicted.json").exists(), "predicted.json precedes the jobs"
    made = [em_pack_job()]
    pab.write_made(HERE, made)
    added = pab.retime([j["name"] for _, j in made], here=HERE, measured=lambda p: None)
    # retime gives every non-warm-up wait 40 s: the writer's line needs the pack's write time.
    job, plan, plan_path = flow.load_job(HERE / "jobs" / "r2-inn-em-pack.json")
    for p in plan:
        if p["label"] == "diag-pack":
            p["wait_log_timeout"] = WRITE_S
        if p["label"] == "em-ready":
            p["wait_log_timeout"] = EM_READY_S
    flow.write(plan_path, plan)
    print("Generated", ", ".join(j["name"] for _, j in made), f"- no queue staging; {added} new entries appended to "
          "lint_corpus_snapshot.json")


if __name__ == "__main__":
    main()
