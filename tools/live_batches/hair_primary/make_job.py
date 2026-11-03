"""Writes the hair_primary job and its plan (README.md). Rerun after editing the phases or gates:

    py -3 tools/live_batches/hair_primary/make_job.py            # jobs/ and plans/ of this folder

rt_hair_groom_primary (docs/RTX_HAIR_PRIMARY.md) A/B in one run: native hair and the strand groom in secondary rays on
throughout (the hair_groom job's startup), then visible strands off/on/off/on/off full-resolution shots and cost
windows cards/strands/cards. Reuses hair_groom's keys, step() and window gates.
"""
import argparse
import importlib.util
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
# hair_groom's make_job under its own name (it imports hair_qual's as "make_job").
_spec = importlib.util.spec_from_file_location("hair_groom_make_job", HERE.parent / "hair_groom" / "make_job.py")
hg = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(hg)   # BASE, GROOM, step, FULL_RES, GROOM_START, the hair_qual Ultra keys as hg.hq

BATCH = "hair_primary"
LIVE = "hair-primary"
SAVES = [("hp1-dialogue1-fg2", "Test Dialogue1")]   # Shadowheart's matched LOD1 hair (hg-20261103e: 5 instances)
# rt_hair_groom_primary is latched at device creation (main 8df7978d: an INI reload cannot enable strands for users),
# so it is on from startup in every phase; the live cards/strands A/B is rt_hair_groom (off: no groom anywhere, the
# native cards; on: strands in RT and on screen). There is no live "strands in RT only" mode any more.
PRIMARY_START = "rt_hair_groom_primary=1"
ON = dict(hg.GROOM, rt_hair_groom_primary="1", rt_hair_fibre="0")
OFF = dict(hg.GROOM, rt_hair_groom="0", rt_hair_groom_primary="1", rt_hair_fibre="0")
FIBRE = dict(ON, rt_hair_fibre="1")   # rt_hair_fibre: the strands lit by the fibre BSDF (3b-2b)


def step(label, seconds, trace=False, **keys):
    return hg.step(label, seconds, trace, **keys)


SHOT = 8
plan = [
    step("warmup", 25, qa_exposure_hold="0", **OFF),           # startup: hair + groom, variants created with the pipelines
    step("settle-cam", 10, qa_exposure_hold="0", **OFF),
    step("warm-strands", 20, qa_exposure_hold="0", **ON),       # first on: points ranges with UVs and previous points
    step("warm-back", 10, qa_exposure_hold="0", **OFF),
    step("shot-cards-a", SHOT, True, **OFF),
    step("shot-strands-a", SHOT, True, **ON),
    step("shot-cards-b", SHOT, True, **OFF),
    step("shot-strands-b", SHOT, True, **ON),
    step("shot-cards-c", SHOT, True, **OFF),
    step("shot-fibre-a", SHOT, True, **FIBRE),
    step("shot-strands-c", SHOT, True, **ON),
    step("settle", 5, **OFF),
    step("cost-cards-a", 25, **OFF),
    step("cost-strands", 25, **ON),
    step("cost-fibre", 25, **FIBRE),
    step("cost-cards-b", 25, **OFF),
    step("restore", 5, qa_exposure_hold="0", **OFF),
]
labels = [p["label"] for p in plan]
shots = [l for l in labels if l.startswith("shot-")]
cost = [l for l in labels if l.startswith("cost-")]
judged = shots + cost   # warm-ups are never judged (hc1 2026-11-03: a build gate on warm-back stopped the run)
traced = [p["label"] for p in plan if p.get("trace_at")]
seconds = sum(p["seconds"] for p in plan)
STRANDS = r"\[hair primary\] frame \d+: [1-9]\d* grooms published; last 300 frames: \d+ publishes, \d+ prepass, [1-9]\d* G-buffer and [1-9]\d* velocity strand draws, 0 pipelines refused"


def expect_every():
    keep = {"scene wait", "capacity: the RT capture", "opaque cutouts", "capacity: the cascade",
            "no background build", "no 'building in the background'", "no freeze"}
    g = [dict(x) for x in hg.hq.expect_every("dlg") if any(x["name"].startswith(k) for k in keep)]
    for x in g:
        if "windows" in x:
            x["windows"] = judged
    freeze = [x for x in g if x["name"].startswith("no freeze")]
    for x in freeze:
        g.remove(x)
        g.append(dict(x, name="no freeze: every build burst inside the plan < 1000 ms", event=True, min=0, windows=labels))
    g += [
        {"name": "hair: invalid/missing native state 0 and retired bounds-invalid 0 in every judged window",
         "select": r"\[alpha hair\] native coverage records \d+;",
         "match": r"; invalid/missing native state 0; unsupported native interfaces total \d+; retired bounds-invalid primitives 0;",
         "min": 1, "windows": judged},
        # Participation: strands drawn in the G-buffer and velocity passes while on (else INVALID: nothing replaced).
        {"name": "strand participation: G-buffer and velocity strand draws > 0 in the strands-on cost window",
         "select": r"\[hair primary\] frame \d+: \d+ grooms published", "match": STRANDS, "min": 1,
         "windows": ["cost-strands", "cost-fibre"]},
        {"name": "strand cost: a G-buffer strand draw GPU sample in the strands-on cost window",
         "select": r"\[hair primary\] strand G-buffer draws GPU", "match": r"GPU [0-9.]+ ms/frame \(average of \d+ frames",
         "min": 1, "windows": ["cost-strands"]},
        {"name": "fibre pass: dispatched in the fibre cost window",
         "select": r"\[hair fibre\] strand pixels lit", "match": r"\[hair fibre\] strand pixels lit", "event": True,
         "min": 1, "windows": ["shot-fibre-a", "cost-fibre"]},
        {"name": "groom participation in every cost window",
         "select": r"\[hair groom\] frame \d+: \d+ groom instances; last 300 frames: at most",
         "match": r"last 300 frames: at most [1-9]\d* groom instances", "min": 1, "windows": cost},
    ]
    for prefix in ["rt passes", "gi"]:
        metric = (rf"{prefix}: GPU cost [0-9.]+ ms/frame \(average of 300 frames\)" +
                  (r"; TLAS this frame [1-9]\d*, previous frame \d+, none 0" if prefix == "rt passes" else ""))
        g.append({"name": f"{prefix} GPU cost: a complete diagnostic-free sample in every cost window",
                  "select": rf"{prefix}: GPU cost", "match": metric, "min": 1, "windows": cost})
    g.append({"name": "scene build GPU: a sample in every cost window",
              "select": r"\[rt\] scene build GPU", "match": r"\[rt\] scene build GPU", "min": 1, "windows": cost})
    return g


def job(name, save):
    expect = [
        r"\[auto\] scene ready",
        r"\[hot\] core generation \d+ loaded",
        r"\[ngx\] RR create .*: Success",
        r"\[cfg\] settings read",
        r"\[trace\] frame \d+ written",
        r"\[hair groom\] device with linear swept spheres",
        r"\[hair groom\] [0-9a-f]{16} LOD \d uploaded: [0-9.]+ MB in the mesh pool",
        r"\[hair primary\] header at 0x[0-9a-f]+: static hair pipelines get strand variants",
        r"\[hair primary\] G-buffer hair pipeline: native depth test \d write \d compare \d+",
        r"\[hair primary\] velocity hair pipeline: native depth test \d write \d compare \d+",
        r"\[hair primary\] pixel record \d+x\d+: [0-9.]+ MB",
        STRANDS,
        r"\[hair primary\] strand G-buffer draws GPU [0-9.]+ ms/frame",
        r"\[hair fibre\] strand pixels lit by the fibre BSDF after D-LS",
        r"rt passes: GPU cost [0-9.]+ ms/frame \(average of 300 frames\)",
        r"gi: GPU cost [0-9.]+ ms/frame \(average of 300 frames\)",
        r"\[rrpost\] exposure held \(qa_exposure_hold=1\) at EV [-0-9.]+",
    ]
    forbid = [r"\[crash\] exception 0x", "VK_ERROR_DEVICE_LOST", r"\[hot\] core (rejected|faulted)",
              "FAILED, keeping previous", "compile call failed", "RR creation failed", "RR evaluate failed",
              "skipped: no variant for", "pipeline creation failed", "shader module creation failed",
              r"\[hair primary\] UNAVAILABLE", r"\[hair primary\] .*refused", r"\[hair primary\] .*could not be allocated",
              r"\[hair primary\] strand fragment shader unavailable", r"\[hair groom\] device WITHOUT",
              r"\[hair groom\] .*malformed", r"\[geo\] rt_native_hair rejected", r"\[bvh\] mesh pool full at",
              r"\[cascade\] could not allocate", "set 1 [1-9]\\d* out-of-range reads skipped"]
    runner = [r"\[motion-plan\] pointer centred in owned foreground game"]
    runner += [rf"\[motion-trace\] phase={labels.index(l)} label={l} repeat=0" for l in traced]
    runner += [rf"\[motion-plan\] complete phases={len(plan)}\b", "^Run ended: done"]
    return {
        "name": name, "save": save, "session": False, "traces": 0, "hold": 90, "timeout": seconds + 300, "repeat": 1,
        "live_batch": LIVE,
        "set": f"{hg.hq.ULTRA.replace('dump_png_width=1024', hg.FULL_RES)},{hg.hq.HAIR_START},{hg.GROOM_START},"
               f"{PRIMARY_START}",
        "live_flow": {"startup_only": ["rt_hair_groom_primary"]},
        "args": ["-RealPresent", "-Foreground", "-Move", "-MovePlan", f"captures/live_batches/{BATCH}/plans/hp_plan.json"],
        "patterns": [r"\[hot\]", r"\[ngx\] RR create", r"\[sl\] DLSS-G frame", r"\[pace\]", r"\[watch\]",
                     "rt passes: GPU cost", "gi: GPU cost", r"\[rt\] scene wait", r"\[rt\] scene build GPU",
                     r"\[trace\]", r"\[shaders\].*\?", r"\[crash\]", "FAILED", "failed", r"\[cascade\] casters",
                     r"\[geo\]", r"\[alpha hair\]", r"\[alpha scalp\]", r"\[hair groom\]", r"\[hair primary\]", r"\[hair fibre\]",
                     "gi: background builds", "building in the background", "skipped: no variant for", r"\[bvh\]",
                     r"\[rrpost\] exposure (held|hold|probe)", r"\[core\] guard:"],
        "expect": expect, "forbid": forbid, "expect_runner": runner, "stop_on_dump": True,
        "purpose": ("Strand hair A/B (docs/RTX_HAIR_PRIMARY.md): rt_hair_groom_primary latched on at startup, rt_hair_groom "
                    "toggled live (off: native cards everywhere; on: strands in RT and on screen); "
                    "off/on/off/on/off full-resolution PNG shots for the user and "
                    "the character float energy, cost cards/strands/cards (rt passes, gi, scene build GPU ms, strand "
                    "G-buffer draw ms). Gated: strand G-buffer and velocity draws > 0 while on."),
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
    (out / "plans" / "hp_plan.json").write_text(json.dumps(plan, indent=1) + "\n", encoding="utf-8")
    for name, save in SAVES:
        (out / "jobs" / f"{name}.json").write_text(json.dumps(job(name, save), indent=1) + "\n", encoding="utf-8")
    print(f"{len(SAVES)} jobs; hp_plan {len(plan)} phases/{seconds} s -> {out}")


if __name__ == "__main__":
    main()
