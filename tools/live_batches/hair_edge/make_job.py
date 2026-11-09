"""Writes the hair_edge job and its plan (README.md). Generate only: no inbox staging, no game launch.

    py -3 tools/live_batches/hair_edge/make_job.py

rr_hair_strand_aa (docs/RTX_HAIR_PRIMARY.md 5n) in one Test Dialogue1 launch with the whole RTX Hair bundle on:
key 0 / 2 / 0 / 2 windows with a PNG trace each, a key-1 window, then a K8 pan burst with the key 0 and with it 2.
predicted.json is committed before the job.
"""
import importlib.util
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))
_spec = importlib.util.spec_from_file_location("hair_validation_jobs", HERE.parent / "hair_validation" / "make_job.py")
hv = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(hv)   # hv.hp (hair_primary), hv.stage_phase, hv.visual_job

NAME = "he1-dialogue1-edge"
SAVE = "Test Dialogue1"
# The user-tested RTX Hair set (common/settings.h exp_hair_groom_primary "on", docs 5l), as live keys.
BUNDLE = dict(rt_hair_groom_primary="1", rt_hair_groom_primary_order="1", rt_hair_groom_primary_current="1",
              rt_hair_primary_frame_tag="1", rt_hair_groom_lod_keep="1", rt_hair_primary_hold="1",
              rt_hair_tangent_world="1", rt_hair_groom="1", rt_hair_groom_shadow_t="1", rt_hair_groom_ao_t="1",
              rt_hair_groom_cascade_drop="1", rt_hair_fibre="1", rt_hair_fibre_gi="1", rt_hair_fibre_spec="1",
              rt_hair_fibre_mapped="1", rr_hair_guides="1", rt_hair_physical_origin="1", rt_hair_capture="1",
              rt_hair_groom_tips="0", rt_hair_groom_coverage="0", rt_hair_primary_passes="15")
SERIALS = {"burst-off": 99100, "burst-aa": 99101}


def step(label, aa, trace=False, frames=60):
    p = hv.stage_phase(label, 7, dict(hv.hp.ON, **BUNDLE, rr_hair_strand_aa=str(aa), qa_exposure_hold="0",
                                      pan_delay_ms="0"), trace=trace)
    if frames:
        p.update(frames=frames, settle_frames=0, stats_reset=True, motion_start=7, motion_end=7)
    p["set"] += ",pan_inputs=0"
    return p


def timed(p, seconds):
    for key in ("frames", "settle_frames", "stats_reset"):
        p.pop(key, None)
    p.update(seconds=seconds, motion_start=seconds, motion_end=seconds)
    return p


def make_plan():
    warm = timed(step("warm-all", 2, frames=0), 25)   # compiles rr_strand_aa.hlsl and every strand pipeline
    warm.update(wait_log=[r"\[shaders\] cache: prefetched ", r"gi: gi/[^ ]+ generation ", r"\[auto\] scene ready"],
                wait_log_timeout=180)
    warm_inputs = timed(step("warm-inputs", 2, frames=0), 1.1)   # pan_inputs=1 once before the measured windows
    warm_inputs["set"] = warm_inputs["set"].replace(",pan_inputs=0", ",pan_inputs=1")
    # f-debug (mode 3) is captured already (he1 2026-11-09 03:01, trace 20261109-030300); its readiness timed out on the
    # 03:04 rerun before the bursts, so this job measures the static pairs and the two bursts only.
    plan = [warm, warm_inputs, timed(step("warm-untag", 1, frames=0), 1.1),
            timed(step("warm-base", 0, frames=0), 1.1),
            step("a-off", 0, True), step("b-aa", 2, True), step("c-off", 0, True), step("d-aa", 2, True),
            step("e-untag", 1, True)]
    # K8 bursts on a still camera (edge flicker without registration), in timed windows that outlast the capture: the
    # 2026-11-09 he1 burst-off window closed with its 60-frame stats period (2 s at 30 fps) while the burst armed at
    # 01:41:57.998 was still recording, so its completion line fell outside (fail-fast 01:42:00.025). Like
    # edge_flicker's rendered bursts (5 s static windows, capture at 0.1 s), sized here for the K8 frames plus the
    # measured capture-stall p95 (2.7 s) and decode.
    for label, aa in (("burst-off", 0), ("burst-aa", 2)):
        p = timed(step(label, aa, frames=0), 14)   # 16-frame burst with RR inputs completes in ~7.5 s (03:10 he1)
        serial = SERIALS[label]
        p.update(capture_at=.1, serial=serial)
        # Full bursts with the RR inputs (rr_color, rr_mvec, material every frame): temporal_burst.py needs them; the
        # 02:04 edge-only bursts (pan_edge=1) held one material frame and no RR input. About 0.55 GB per burst.
        p["set"] = p["set"].replace(",pan_inputs=0", "") + f",pan_prepare=1,pan_edge=0,pan_inputs=1,pan_capture={serial},pan_delay_ms=0"
        plan.append(p)
    restore = timed(step("restore", 0, frames=0), 1.1)
    restore["set"] += ",dump_images=1,dump_png_width=960,dump_preview_f32=1"
    plan.append(restore)
    return plan


def make_job(plan):
    j = hv.visual_job(NAME, SAVE, plan)
    startup = dict(item.split("=", 1) for item in j["set"].split(","))
    startup.update(BUNDLE, rr_hair_strand_aa="2")   # the strand_resolve pipeline exists from the start
    j["set"] = ",".join(f"{k}={v}" for k, v in startup.items())
    j["live_batch"] = "hair-edge"
    j["args"] = ["-RealPresent", "-Move", "-MovePlan", f"tools/live_batches/hair_edge/plans/{NAME}.json"]
    j["purpose"] = ("rr_hair_strand_aa 0/2/0/2 (+1) in one Test Dialogue1 run with RTX Hair on: PNG pairs and K8 "
                    "pan bursts for strand edge flicker (tools/hair_groom/temporal_burst.py), energy and the "
                    "render-res chain GPU ms. READY_FOR_LIVE, not authorized.")
    labels = [p["label"] for p in plan]
    windows = [l for l in labels if l in ("a-off", "b-aa", "c-off", "d-aa", "e-untag", "f-debug")]
    strands = windows + list(SERIALS)
    gates = []
    for g in j["expect_every"]:
        if g["name"].startswith(("zero refused", "zero orphan", "depth orphans", "swapped frames")):
            gates.append(dict(g, windows=strands))
        elif g["name"].startswith("K8 "):
            # Per-window burst gates fail fast about 2 s into the window (03:14:04 he1: burst 99101 armed 03:14:02.469,
            # gate failed 03:14:04.573, the burst takes ~7.5 s), so each burst's completion is a plain expect below.
            pass
    j["expect"] = j["expect"] + [rf"\[pan\] burst {s} complete: \d+ frames, \d+ samples, transport ok"
                                 for s in SERIALS.values()]
    # The mode-switch line is logged when the setting is applied, before the window's motion-ready line (live
    # 2026-11-09 01:38: =2 at 01:38:13.4, b-aa ready at 01:38:16.1), so a per-window event gate can never see it.
    # Each mode's switch line is a plain expect instead; the plan order proves which window ran which mode.
    gates.append(dict(name="render-res chain GPU ms in every window", select=r"\[rrpost\] gpu: render-res chain",
                      match=r"render-res chain [0-9.]+ ms/frame", min=1, period_frames=60, windows=windows))
    j["expect_every"] = gates
    j["expect"] = [e for e in j["expect"] if "shadow transmittance" not in e and "strand G-buffer draws GPU" not in e]
    j["expect"] += [r"\[hair primary\] (?:prepass|G-buffer) hair pipeline stencil: test \d, write mask 0x[0-9a-f]{2}",
                    r"\[rrpost\] rr_post\.hlsl:strand_resolve pipeline built",
                    r"\[rrpost\] rr_hair_strand_aa=0:", r"\[rrpost\] rr_hair_strand_aa=1:", r"\[rrpost\] rr_hair_strand_aa=2:",
                    r"\[rrpost\] strand snapshots \(rr_hair_strand_aa=\d\): [1-9]\d* at the lighting pass"]
    j["patterns"] += [r"\[rrpost\]", r"hair pipeline stencil"]
    # The edge resolve must build: a pipeline creation or DXC call failure (layer/rrpost.cpp compile_pipeline, which
    # names every entry under rr_post.hlsl; layer/shaders.cpp) and a period without a strand resolve frame are fatal.
    j["forbid"] = list(j.get("forbid", [])) + [r"\[rrpost\] strand resolve .*: 0 frames, [1-9]\d* without",
                                               r"\[rrpost\] rr_post\.hlsl:strand_resolve pipeline creation failed",
                                               r"\[shaders\] rr_strand_aa\.hlsl:strand_resolve compile call failed",
                                               r"\[rrpost\] rr_post\.hlsl:strand_snapshot pipeline creation failed",
                                               r"\[rrpost\] strand snapshots .*: 0 at the lighting pass, [1-9]\d* refused"]
    return j


def main():
    assert (HERE / "predicted.json").exists(), "predicted.json precedes the job"
    plan = make_plan()
    for folder, value in (("plans", plan), ("jobs", make_job(plan))):
        (HERE / folder).mkdir(exist_ok=True)
        (HERE / folder / f"{NAME}.json").write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    from tools.live_batches.live_flow import tighten_files
    tighten_files(HERE, job_names=[NAME])
    print(f"Generated {NAME}; no queue staging")


if __name__ == "__main__":
    main()
