"""Writes the staged hair_edge (he2, still pixels) job and its plan. Generate only: no inbox staging, no game launch.

    py -3 tools/live_batches_staged/hair_edge/make_job.py

rr_hair_strand_aa (docs/RTX_HAIR_PRIMARY.md 5n) on still pixels: one Test Dialogue1 launch with the RTX Hair bundle on
(today's 26 keys), the exposure held, eight 16-frame bursts with the RR inputs in ABBA ABBA order, each after a 90-frame settle
window in its own mode. Judged by tools/hair_groom/edge_burst.py --modes (still pixels, key-off repeat spread).
predicted.json is committed before the job.
"""
import importlib.util
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))
_spec = importlib.util.spec_from_file_location("hair_edge_jobs", ROOT / "tools" / "live_batches" / "hair_edge" / "make_job.py")
he = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(he)   # he.step, he.timed, he.BUNDLE, he.hv (stage_phase, visual_job)
from tools.live_batches_staged.perf_ab import make_jobs as pab   # noqa: E402  (bundle(): read from common/settings.h)

# Today's RTX Hair On bundle instead of he1's hand copy of 2026-11-09 (26 keys since 6k; rt_hair is the master switch:
# with 0 every bundle key reads 0). The three pins are he1's and are today's defaults.
he.BUNDLE = dict({"rt_hair": "1"}, **{k: "1" for k in pab.bundle()}, rt_hair_groom_tips="0", rt_hair_groom_coverage="0",
                 rt_hair_primary_passes="15")
# he1's capture pins predate the VRAM right-sizing defaults (2026-11-14): the buffers follow the demand now.
STALE_STARTUP = ("rt_xfb_mb", "rt_cascade_mb")

NAME = "he2-dialogue1-still"
SAVE = he.SAVE
# off aa aa off off aa aa off: a linear drift of the animated dialogue cancels in mean(aa) - mean(off).
ORDER = ["off", "aa", "aa", "off", "off", "aa", "aa", "off"]
MODE = {"off": 0, "aa": 2}
SERIAL0 = 99110
# History after a mode switch. The final image carries rr_post_temporal's accumulation (alpha 1/16, on in this bundle;
# docs/TRACK_A_RR.md), which needs ceil(ln(1/1024) / ln(15/16)) = 108 frames to forget the other mode to 10-bit
# precision (live_flow's own settle rule); RR's history is shorter. he1 armed its bursts 0.31-2.3 s (about 9-40 frames)
# after the switch. The settle window's periodic gates skip SETTLE_SKIP frames and then need one 60-frame stats period:
# 48 + 60 = 108. The analysis also reports each burst's half-burst drift.
SETTLE_FRAMES = 108
SETTLE_SKIP = SETTLE_FRAMES - 60
BURST_SECONDS = 14   # a 16-frame RR-input burst completes in ~7.5-10 s (he1 03:10, 04:05)


def held(p):
    # Exposure held for every measured window (docs/QA.md "Exposure hold"): he1 ran qa_exposure_hold=0, so bursts 15 s
    # apart saw different eye adaptation. pan_inputs stays on (input-layout tracking only) so the burst window changes
    # nothing but pan_capture.
    p["set"] = p["set"].replace("qa_exposure_hold=0", "qa_exposure_hold=1").replace(",pan_inputs=0", ",pan_inputs=1")
    return p


def make_plan():
    warm = he.timed(he.step("warm-all", 2, frames=0), 25)   # compiles rr_strand_aa.hlsl and every strand pipeline
    warm.update(wait_log=[r"\[shaders\] cache: prefetched ", r"gi: gi/[^ ]+ generation ", r"\[auto\] scene ready"],
                wait_log_timeout=180)
    warm_inputs = he.timed(he.step("warm-inputs", 0, frames=0), 1.1)
    warm_inputs["set"] = warm_inputs["set"].replace(",pan_inputs=0", ",pan_inputs=1")
    # The hold latches the exposure of the frame it is switched on (mode 0) and keeps it for the rest of the run.
    plan = [warm, warm_inputs, held(he.timed(he.step("warm-hold", 0, frames=0), 2))]
    for i, mode in enumerate(ORDER, 1):
        aa = MODE[mode]
        settle = held(he.step(f"settle-{i}-{mode}", aa, frames=SETTLE_FRAMES))   # sized again by tighten
        burst = held(he.timed(he.step(f"burst-{i}-{mode}", aa, frames=0), BURST_SECONDS))
        serial = SERIAL0 + i - 1
        burst.update(capture_at=.1, serial=serial)
        burst["set"] += f",pan_prepare=1,pan_edge=0,pan_capture={serial},pan_delay_ms=0"
        plan += [settle, burst]
    restore = he.timed(he.step("restore", 0, frames=0), 1.1)
    restore["set"] += ",dump_images=1,dump_png_width=960,dump_preview_f32=1"
    plan.append(restore)
    return plan


def make_job(plan):
    j = he.hv.visual_job(NAME, SAVE, plan)
    startup = dict(item.split("=", 1) for item in j["set"].split(","))
    startup.update(he.BUNDLE, rr_hair_strand_aa="2")   # the strand_resolve pipeline exists from the start
    for key in STALE_STARTUP:
        startup.pop(key, None)
    j["set"] = ",".join(f"{k}={v}" for k, v in startup.items())
    j["live_batch"] = "hair-edge-still"
    j["args"] = ["-RealPresent", "-Move", "-MovePlan", f"tools/live_batches_staged/hair_edge/plans/{NAME}.json"]
    j["purpose"] = ("rr_hair_strand_aa 0/2 on still pixels: eight RR-input K16 bursts (ABBA ABBA) in one Test Dialogue1 "
                    "run with RTX Hair on, exposure held, a 90-frame settle before each burst; edge_burst.py --modes. "
                    "READY_FOR_LIVE, not authorized.")
    settles = [p["label"] for p in plan if p["label"].startswith("settle-")]
    aa_settles = [l for l in settles if l.endswith("-aa")]
    gates = []
    for g in j["expect_every"]:
        # Periodic gates only in the settle windows: bursts stay timed windows (live_flow.tighten would make a gated
        # window a frame window, and auto-run ends a frame window on its count before the burst completes: he1 03:17).
        if g["name"].startswith(("zero refused", "zero orphan", "depth orphans", "swapped frames")):
            gates.append(dict(g, windows=settles, settle_frames=SETTLE_SKIP))
    gates.append(dict(name="render-res chain GPU ms in every settle window", select=r"\[rrpost\] gpu: render-res chain",
                      match=r"render-res chain [0-9.]+ ms/frame", min=1, period_frames=60, windows=settles,
                      settle_frames=SETTLE_SKIP))
    # The resolve acts in aa windows: a site with coverage pixels in every aa settle period (he1 04:02: ~82k px).
    gates.append(dict(name="strand coverage at the G-buffer end in every aa settle window",
                      select=r"\[rrpost\] strand snapshot site G-buffer end",
                      match=r"strand snapshot site G-buffer end \(frames \d+-\d+\): [1-9]\d* dispatches; per dispatch "
                            r"footprint \d+ px, strand [1-9]\d* px, coverage [1-9]\d* px",
                      min=1, period_frames=60, windows=aa_settles, settle_frames=SETTLE_SKIP))
    j["expect_every"] = gates
    j["expect"] = [e for e in j["expect"] if "shadow transmittance" not in e and "strand G-buffer draws GPU" not in e]
    j["expect"] += [rf"\[pan\] burst {SERIAL0 + i} complete: 16 frames, \d+ samples, transport ok" for i in range(len(ORDER))]
    j["expect"] += [r"\[rrpost\] exposure held \(qa_exposure_hold=1\) at EV [-0-9.]+",
                    r"\[rrpost\] rr_post\.hlsl:strand_resolve pipeline built",
                    r"\[rrpost\] rr_hair_strand_aa=0:", r"\[rrpost\] rr_hair_strand_aa=2:",
                    r"\[rrpost\] strand snapshots \(rr_hair_strand_aa=\d\): [1-9]\d* at the G-buffer end",
                    # 1-3 fallbacks per ~360 frames (he1 04:02, INVALID on '0 fell back'): recorded, not fatal; a 16-frame
                    # burst holds one with ~13% probability and the analysis' positive control catches an idle resolve.
                    r"\[rrpost\] strand resolve \(rr_hair_strand_aa=2\): [1-9]\d* frames, \d+ fell back"]
    j["patterns"] += [r"\[rrpost\]", r"hair pipeline stencil"]
    j["forbid"] = list(j.get("forbid", [])) + [r"\[rrpost\] strand resolve .*: 0 frames, [1-9]\d* without",
                                               r"\[rrpost\] rr_post\.hlsl:strand_resolve pipeline creation failed",
                                               r"\[shaders\] rr_strand_aa\.hlsl:strand_resolve compile call failed",
                                               r"\[rrpost\] rr_post\.hlsl:strand_snapshot pipeline creation failed",
                                               r"\[rrpost\] strand snapshots .*: 0 at the G-buffer end, [1-9]\d* refused",
                                               r"\[pan\] burst 991[01]\d complete: .*transport INVALID"]
    return j


def main():
    assert (HERE / "predicted.json").exists(), "predicted.json precedes the job"
    plan = make_plan()
    for folder, value in (("plans", plan), ("jobs", make_job(plan))):
        (HERE / folder).mkdir(exist_ok=True)
        (HERE / folder / f"{NAME}.json").write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    from tools.live_batches.live_flow import tighten_files
    tighten_files(HERE, job_names=[NAME])
    tight = json.loads((HERE / "plans" / f"{NAME}.json").read_text())
    labels = [p["label"] for p in tight]
    for p in tight:
        if p["label"].startswith("burst-"):
            assert "frames" not in p and not p.get("stats_reset") and p["seconds"] >= BURST_SECONDS, (p["label"], p)
            assert "qa_exposure_hold=1" in p["set"] and p["set"].rsplit("pan_capture=", 1)[1].split(",")[0] == str(p["serial"])
            settle = tight[labels.index(p["label"]) - 1]
            assert settle["label"] == p["label"].replace("burst-", "settle-") and settle.get("frames", 0) >= SETTLE_FRAMES, settle
            mode = "rr_hair_strand_aa=" + str(MODE[p["label"].rsplit("-", 1)[1]])
            assert settle["set"].rsplit("rr_hair_strand_aa=", 1)[1][0] == p["set"].rsplit("rr_hair_strand_aa=", 1)[1][0] == mode[-1]
    # Append-only (as perf_ab.retime): readiness queries and expect / forbid patterns the frozen lint snapshot has not
    # seen (count 0 until a VALID run supplies them); existing entries are never changed.
    from tools.live_batches import lint_snapshot, live_flow as flow
    added = lint_snapshot.extend_readiness(sorted((HERE / "jobs").glob("*.json")))
    data = flow.read(lint_snapshot.PATH)
    job = flow.read(HERE / "jobs" / f"{NAME}.json")
    new = sorted({rx for key in ("forbid", "expect") for rx in job.get(key, [])} - data["matches"].keys())
    if new:
        data["matches"].update({rx: [0, None] for rx in new})
        flow.write(lint_snapshot.PATH, data)
    print(f"Generated {NAME}; no queue staging; {added + len(new)} new entries appended to lint_corpus_snapshot.json")


if __name__ == "__main__":
    main()
