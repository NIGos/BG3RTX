"""Writes the hair_groom jobs and their plan (README.md). Rerun after editing the phases or gates:

    py -3 tools/live_batches/hair_groom/make_job.py            # jobs/ and plans/ of this folder

rt_hair_groom (docs/RTX_HAIR_INTEGRATION.md) A/B in one run per save: native hair on throughout (the hair_qual
startup latches), the groom on at startup (device feature), then groom off/on/off/on/off shots and cost windows,
and a no-hair cost window for the baseline. Reuses hair_qual's Ultra keys and window gates.
"""
import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "hair_qual"))
import make_job as hq  # noqa: E402  (ULTRA, HAIR_START, the window gate shapes)

BATCH = "hair_groom"
LIVE = "hair-groom"
# Shadowheart's hair is the one converted groom (data/assetdb/hair_grooms, LOD0 + LOD1). The 11:03 rerun saw her
# LOD1 draw (6009 indices) only in Test Dialogue1; the camp save has no Shadowheart hair draw (camp job dropped).
# A run without a groom instance is INVALID (gated).
SAVES = [("hg2-dialogue1-fg2", "Test Dialogue1")]
GROOM_START = "rt_hair_groom=1"

BASE = {"rt_native_hair": "1", "rt_native_scalp": "1", "rt_hair_physical_origin": "1", "rt_stats": "0",
        "rt_primary_check": "0", "qa_exposure_hold": "1", "rt_hair_gi_cull": "0", "rt_hair_groom": "0"}
GROOM = {"rt_hair_groom": "1"}
NO_HAIR = {"rt_native_hair": "0", "rt_native_scalp": "0", "rt_hair_physical_origin": "0"}


def step(label, seconds, trace=False, **keys):
    s = dict(BASE, **keys)
    out = {"label": label, "kind": "static", "speed": "static", "seconds": seconds, "repeat_ms": 0, "scan": 16,
           "capture_at": 0, "serial": 0, "motion_start": seconds, "motion_end": seconds, "pixels_per_second": 0,
           "direction": 0, "set": ",".join(f"{k}={v}" for k, v in s.items())}
    if trace:
        out["trace_at"] = [5]
    return out


SHOT = 8
plan = [
    step("warmup", 25, qa_exposure_hold="0", **GROOM),     # startup: hair + groom variants, groom upload and builds
    step("settle-cam", 10, qa_exposure_hold="0", **GROOM),
    step("toggle-off-a", 20, qa_exposure_hold="0"),         # first toggle: constant 1020 = 0 pipelines build here
    step("toggle-off-b", 10, qa_exposure_hold="0"),
    # 2026-11-03 10:40 run: the no-hair pipelines built inside cost-nohair. Warm them (and the way back) here.
    step("warm-nohair", 25, qa_exposure_hold="0", **NO_HAIR),
    step("warm-back", 15, qa_exposure_hold="0"),
    step("shot-off-a", SHOT, True),
    step("shot-groom-a", SHOT, True, **GROOM),
    step("shot-off-b", SHOT, True),
    step("shot-groom-b", SHOT, True, **GROOM),
    step("shot-off-c", SHOT, True),
    step("settle", 5),
    step("cost-off-a", 25),                                 # 25 s: one complete 300-frame rt passes / gi line
    step("cost-groom", 25, **GROOM),
    step("cost-off-b", 25),
    step("cost-nohair", 25, **NO_HAIR),                     # the no-hair baseline of the native hair cost
    step("restore", 5, qa_exposure_hold="0"),
]
labels = [p["label"] for p in plan]
shots = [l for l in labels if l.startswith("shot-")]
cost = [l for l in labels if l.startswith("cost-")]
groom_on = ["shot-groom-a", "shot-groom-b", "cost-groom"]
judged = ["toggle-off-b"] + shots + cost
hair_on = [l for l in judged if l != "cost-nohair"]
traced = [p["label"] for p in plan if p.get("trace_at")]
seconds = sum(p["seconds"] for p in plan)


def expect_every():
    keep = {"scene wait", "capacity: the RT capture", "opaque cutouts", "capacity: the cascade",
            "no background build", "no 'building in the background'", "no freeze"}
    g = [dict(x) for x in hq.expect_every("dlg") if any(x["name"].startswith(k) for k in keep)]
    for x in g:
        if "windows" in x:
            x["windows"] = judged
    g += [
        {"name": "first enable (startup): no build pending in settle-cam",
         "select": r"gi: background builds \(\d+ frames\):", "match": r", 0 pending, 0 started", "min": 1,
         "windows": ["settle-cam"]},
        {"name": "hair: invalid/missing native state 0 and retired bounds-invalid 0 in every hair-on window",
         "select": r"\[alpha hair\] native coverage records \d+;",
         "match": r"; invalid/missing native state 0; unsupported native interfaces total \d+; retired bounds-invalid primitives 0;",
         "min": 1, "windows": hair_on},
        # Participation: without a matched groom instance the A/B measures nothing (INVALID, not a pass).
        # The window maximum, not the instant of the line: 11:38 run, Shadowheart left the view between lines.
        {"name": "groom participation: at least one groom instance in every groom-on window",
         "select": r"\[hair groom\] frame \d+: \d+ groom instances; last 300 frames: at most",
         "match": r"last 300 frames: at most [1-9]\d* groom instances", "min": 1, "windows": groom_on},
    ]
    for prefix in ["rt passes", "gi"]:
        metric = (rf"{prefix}: GPU cost [0-9.]+ ms/frame \(average of 300 frames\)" +
                  (r"; TLAS this frame [1-9]\d*, previous frame \d+, none 0" if prefix == "rt passes" else ""))
        g.append({"name": f"{prefix} GPU cost: a complete diagnostic-free sample in every cost window",
                  "select": rf"{prefix}: GPU cost", "match": metric, "min": 1, "windows": cost})
    g.append({"name": "scene build GPU: a sample in every cost window (groom update + refit live there)",
              "select": r"\[rt\] scene build GPU", "match": r"\[rt\] scene build GPU", "min": 1, "windows": cost})
    return g


def job(name, save):
    expect = [
        r"\[auto\] scene ready",
        r"\[hot\] core generation \d+ loaded",
        r"\[ngx\] RR create .*: Success",
        r"\[cfg\] settings read",
        r"\[trace\] frame \d+ written",
        r"\[geo\] transform feedback enabled: .*\(stride 88\)",
        r"\[hair groom\] \+ VK_NV_ray_tracing_linear_swept_spheres",
        r"\[hair groom\] device with linear swept spheres",
        r"\[hair groom\] loaded .*\.hgrm: index id [0-9a-f]{16}",
        r"\[hair groom\] [0-9a-f]{16} uploaded: \d+ LODs",
        r"\[hair groom\] frame \d+: \d+ groom instances; last 300 frames: at most [1-9]\d* groom instances",
        r"gi: background builds done: [1-9]\d* builds in \d+ ms",
        r"rt passes: GPU cost [0-9.]+ ms/frame \(average of 300 frames\)",
        r"gi: GPU cost [0-9.]+ ms/frame \(average of 300 frames\)",
        r"\[rrpost\] exposure held \(qa_exposure_hold=1\) at EV [-0-9.]+",
    ]
    forbid = [r"\[crash\] exception 0x", "VK_ERROR_DEVICE_LOST", r"\[hot\] core (rejected|faulted)",
              "FAILED, keeping previous", "compile call failed", "RR creation failed", "RR evaluate failed",
              "skipped: no variant for", "pipeline creation failed", "shader module creation failed",
              r"\[geo\] rt_native_hair rejected", r"\[alpha hair\] rt_native_hair requires restart",
              r"\[hair groom\] .*malformed", r"\[hair groom\] device WITHOUT",
              r"linear swept spheres device request failed", "set 1 [1-9]\\d* out-of-range reads skipped",
              r"\[bvh\] mesh pool full at", r"\[cascade\] could not allocate"]
    runner = [r"\[motion-plan\] pointer centred in owned foreground game"]
    runner += [rf"\[motion-trace\] phase={labels.index(l)} label={l} repeat=0" for l in traced]
    runner += [rf"\[motion-plan\] complete phases={len(plan)}\b", "^Run ended: done"]
    return {
        "name": name, "save": save, "session": False, "traces": 0, "hold": 90, "timeout": seconds + 300, "repeat": 1,
        "live_batch": LIVE, "set": f"{hq.ULTRA},{hq.HAIR_START},rt_hair_gi_cull=0,{GROOM_START}",
        "args": ["-RealPresent", "-Foreground", "-Move", "-MovePlan", f"captures/live_batches/{BATCH}/plans/hg_plan.json"],
        "patterns": [r"\[hot\]", r"\[ngx\] RR create", r"\[sl\] DLSS-G frame", r"\[pace\]", r"\[watch\]",
                     "rt passes: GPU cost", "gi: GPU cost", r"\[rt\] scene wait", r"\[rt\] scene build GPU",
                     r"\[trace\]", r"\[shaders\].*\?", r"\[crash\]", "FAILED", "failed", r"\[cascade\] casters",
                     r"\[geo\]", r"\[alpha hair\]", r"\[alpha scalp\]", r"\[hair groom\]", "gi: background builds",
                     "building in the background", "skipped: no variant for", r"\[bvh\]",
                     r"\[rrpost\] exposure (held|hold|probe)", r"\[core\] guard:"],
        "expect": expect, "forbid": forbid, "expect_runner": runner, "stop_on_dump": True,
        "purpose": ("rt_hair_groom A/B (docs/RTX_HAIR_INTEGRATION.md): native hair on throughout, the strand groom on at "
                    "startup, then groom off/on/off/on/off PNG shots (character float energy, char_energy.py), cost "
                    "off/groom/off and a no-hair baseline (rt passes, gi, scene build GPU ms). Gated: groom instances > 0 "
                    "in every groom-on window, no background build in judged windows, hair invalid 0."),
        "estimated_phase_seconds": seconds,
        "expect_every": expect_every(),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(HERE))
    a = ap.parse_args()
    out = Path(a.out)
    (out / "jobs").mkdir(parents=True, exist_ok=True)
    (out / "plans").mkdir(parents=True, exist_ok=True)
    (out / "plans" / "hg_plan.json").write_text(json.dumps(plan, indent=1) + "\n", encoding="utf-8")
    for name, save in SAVES:
        (out / "jobs" / f"{name}.json").write_text(json.dumps(job(name, save), indent=1) + "\n", encoding="utf-8")
    print(f"{len(SAVES)} jobs; hg_plan {len(plan)} phases/{seconds} s -> {out}")


if __name__ == "__main__":
    main()
