"""Writes the hair_qual jobs and their plans (README.md). Rerun after editing the phases or gates:

    py -3 tools/live_batches/hair_qual/make_job.py            # jobs/ and plans/ of this folder
"""
import argparse
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
BATCH = "hair_qual"
LIVE = "hair-qual"
# S8 first: its water hair variants (rt/water_*.hlsl ?RT_SPEC_NATIVE=1) were never compiled at run time, so its
# startup burst is the cold case; the dialogue jobs read the runtime cache for them.
SAVES = [("hq1-s8-water-fg2", "Water Queens House", "s8"), ("hq2-dialogue1-fg2", "Test Dialogue1", "dlg"),
         ("hq3-dialogue2-fg2", "Test Dialogue2", "dlg"), ("hq4-dialogue3-fg2", "Test Dialogue3", "dlg")]

# smoke_ultra's launch keys (tools/live_batches/smoke_ultra/jobs/su1-ultra-s8-fg2.json: preset ultra, every qualified
# experimental key, the water keys from the preset) with the native-hair startup latches instead of hair off:
# 88-byte vertices and the origin image; rt_xfb_mb / rt_cascade_mb scaled 88/64 from smoke's explicit 1024 / 427
# (an explicit value is not scaled by the stride, docs/NATIVE_HAIR.md "Dialogue validation plan").
ULTRA = ("preset=ultra,fg=2,headless=0,fps_cap=0,rt=1,rr=1,rr_post=1,reflex=1,crash_dump=1,rt_timing_frames=60,rt_debug=0,"
         "ls_debug=0,gi_debug=0,rf_debug=0,rr_show=0,rf_stats=0,ls_stats=0,ls_check=0,dump_images=1,dump_png_width=1024,"
         "dump_raw=0,dump_preview_f32=1,dump_shaders=0,pan_capture=0,pan_inputs=0,pan_prepare=0,rt_order_trace=0,"
         "rt_stability_log=0,rt_stats=0,rt_primary_check=0,rt_primary_epoch=0,fg_state_every=30,rt_omm=0,"
         "rt_sun_cascade_current=0,rt_native_tileset=0,rt_alpha_spill=0,ls_box=2,ls_k=0,rt_hit_reference=0,sharc=0,"
         "rt_bvh_split=1,rt_alpha=1,qa_exposure_hold=0,sharc_device=1,rt_scene_wait_log_frames=60,rt_hit_material=1,"
         "rt_hit_material_instanced=1,gi_hit_direct=1,gi_hit_sun=1,rt_emissive=1,rt_hit_emission=1,rt_cascade_casters=1,"
         "rt_scene_wait=1,rt_hit_material_recycle=1,fg_letterbox=1")
HAIR_START = "rt_native_hair=1,rt_native_scalp=1,rt_hair_physical_origin=1,rt_origin=1,rt_xfb_mb=1408,rt_cascade_mb=587"

# Every phase writes every toggled key. Hair "on" is the product set: hair + scalp + physical origin.
BASE = {"rt_native_hair": "0", "rt_native_scalp": "0", "rt_hair_physical_origin": "0", "rt_stats": "0",
        "rt_primary_check": "0", "qa_exposure_hold": "1", "rt_hair_gi_cull": "0"}
ON = {"rt_native_hair": "1", "rt_native_scalp": "1", "rt_hair_physical_origin": "1"}
STATS = {"rt_stats": "1", "rt_primary_check": "1"}


def step(label, seconds, trace=False, **keys):
    s = dict(BASE, **keys)
    out = {"label": label, "kind": "static", "speed": "static", "seconds": seconds, "repeat_ms": 0, "scan": 16,
           "capture_at": 0, "serial": 0, "motion_start": seconds, "motion_end": seconds, "pixels_per_second": 0,
           "direction": 0, "set": ",".join(f"{k}={v}" for k, v in s.items())}
    if trace:
        out["trace_at"] = [5]
    return out


SHOT = 8
# Enabling native hair is a restart with the keys on (startup latches: 88-byte vertices, origin image), and the
# layer's scalp capability word is sticky from then on (layer/alphatest.cpp alpha_begin_capture): every gi/ and
# water request is a hair variant from the first lighting frame. So the user's first enable IS the startup: its
# burst (ds1 10:03:51-10:04:52, 60 s frozen before the fix) runs while warmup holds hair on. Live, only
# rt_hair_physical_origin selects other pipeline specializations (the alternate entries, core/gi.cpp refresh): its first toggle off
# in toggle-off-a is the runtime first-toggle case. The coverage A/B after it toggles all three keys.
plan = [
    step("warmup", 25, qa_exposure_hold="0", **ON),            # the startup burst: hair on as the user starts it
    step("settle-cam", 10, qa_exposure_hold="0", **ON),        # every startup build done (gated); the camera settles
    step("toggle-off-a", 25, qa_exposure_hold="0"),            # first toggle: the non-origin variants build here
    step("toggle-off-b", 15, qa_exposure_hold="0"),
    step("shot-off-a", SHOT, True),
    step("shot-hair-a", SHOT, True, **ON),
    step("shot-off-b", SHOT, True),
    step("shot-hair-b", SHOT, True, **ON),
    step("shot-off-c", SHOT, True),
    # rt_stats + rt_primary_check select other variants (pan_primary): built in the warm windows, judged after.
    step("diag-rt-warm-off", 10, **STATS),
    step("diag-rt-warm-on", 10, **dict(STATS, **ON)),
    step("diag-rt-hairoff-a", 10, **STATS),
    step("diag-rt-hairon", 10, **dict(STATS, **ON)),
    step("diag-rt-hairoff-b", 10, **STATS),
    step("settle", 5),                                         # stats off: the cost windows start clean
    step("cost-off-a", 25),                                    # 25 s: one complete 300-frame rt passes line at >= 24 fps
    step("cost-hair", 25, **ON),
    step("cost-off-b", 25),
    step("restore", 5, qa_exposure_hold="0"),
]
labels = [p["label"] for p in plan]
shots = [l for l in labels if l.startswith("shot-")]
hair_on = ["shot-hair-a", "shot-hair-b", "diag-rt-hairon", "cost-hair"]
diag = ["diag-rt-hairoff-a", "diag-rt-hairon", "diag-rt-hairoff-b"]
cost = [l for l in labels if l.startswith("cost-")]
measured = shots + diag + cost
judged = ["toggle-off-b"] + measured   # toggle-off-b: the first-toggle builds done within toggle-off-a (25 s)
traced = [p["label"] for p in plan if p.get("trace_at")]
seconds = sum(p["seconds"] for p in plan)

# The smallest old render-thread stall of a traced hair variant in ds1 (2026-11-02 10:04:00-10:04:01):
# rt_reflections 747 ms DXC + 581 ms pipeline creation = 1.33 s. Every present interval of the first-enable burst
# must stay below it; 1000 ms is under that with margin for a normal frame (<= 70 ms at the 14 fps floor seen in ds3).
LONGEST_MS = 1000


def expect_every(kind):
    g = [
        {"name": "scene wait: every judged window traces its own frame's scene (0 timeouts)",
         "select": r"\[rt\] scene wait: \d+ frames", "match": r"got this frame's scene, 0 timeouts", "min": 1,
         "windows": [w for w in judged if not w.startswith("diag-")]},
        {"name": "capacity: the RT capture drops no opaque draw (buffer full 0.00) in every judged window",
         "select": r"\[geo\] RT capture, last \d+ frames:", "match": r"opaque draws dropped \(buffer full\) 0\.00,",
         "min": 1, "windows": judged},
        {"name": "opaque cutouts 0: no alpha-tested draw made opaque (region full 0.00) in every judged window",
         "select": r"\[geo\] RT capture, last \d+ frames:", "match": r"alpha draws made opaque \(region full\) 0\.00,",
         "min": 1, "windows": judged},
        {"name": "capacity: the cascade casters buffer is never full in every judged window",
         "select": r"\[cascade\] casters: cascade \d+, \d+ frames;", "match": r", buffer full 0\.0, added ", "min": 1,
         "windows": judged, "when_present": True},
        # A pass waiting for its background build is skipped: energy and cost would be faked.
        {"name": "no background build in any measured window (0 waiting, 0 pending, 0 started in every status line)",
         "select": r"gi: background builds \(\d+ frames\):", "match": r": 0 frames waiting, 0 pending, 0 started",
         "min": 1, "windows": judged},
        {"name": "no 'building in the background' line in any measured window (a pass skipped there)",
         "select": r"building in the background", "match": r"(?!)", "min": 0, "event": True, "windows": judged},
        # The freeze fix. Startup (the user's first enable): every build done by settle-cam.
        {"name": "first enable (startup): no build pending in settle-cam (all passes appeared within load + warmup 25 s)",
         "select": r"gi: background builds \(\d+ frames\):", "match": r", 0 pending, 0 started", "min": 1,
         "windows": ["settle-cam"]},
        # First runtime toggle: the origin key's other variants start building in toggle-off-a.
        {"name": "first toggle: the non-origin variants build in the background in toggle-off-a",
         "select": r"building in the background", "match": r"building in the background", "min": 1, "event": True,
         "windows": ["toggle-off-a"]},
        # Every burst of the run (startup included): no present interval as long as one old render-thread build.
        {"name": f"no freeze: every burst's longest present interval < {LONGEST_MS} ms (startup and first toggle)",
         "select": r"gi: background builds done: ", "match": r"longest present interval \d{1,3} ms", "min": 2},
        # Invalid / bounds 0 in every judged window (hair on or off).
        {"name": "hair: invalid/missing native state 0 and retired bounds-invalid 0 in every judged window",
         "select": r"\[alpha hair\] native coverage records \d+;",
         "match": r"; invalid/missing native state 0; unsupported native interfaces total \d+; retired bounds-invalid primitives 0;",
         "min": 1, "windows": judged},
        {"name": "scalp: invalid/missing native state 0 in every judged window",
         "select": r"\[alpha scalp\] native hard records \d+;", "match": r"; invalid/missing native state 0;",
         "min": 1, "windows": judged},
    ]
    if kind == "dlg":   # participation: a dialogue without native hair records is no evidence (INVALID, not a pass)
        g += [{"name": "hair participation: native coverage records > 0 in every hair-on window",
               "select": r"\[alpha hair\] native coverage records \d+;",
               "match": r"native coverage records [1-9]\d*;", "min": 1, "windows": hair_on},
              {"name": "scalp participation: hard or camera-dither records > 0 in every hair-on window",
               "select": r"\[alpha scalp\] native hard records \d+;",
               "match": r"native hard records (?:[1-9]\d*; native camera dither records \d+|\d+; native camera dither records [1-9]\d*);",
               "min": 1, "windows": hair_on}]
    if kind == "s8":   # water reflections drawn with hair on (and off): the routed scalp/hair water variants
        g += [{"name": "water reflections traced > 0 in every judged window (hair on and off)",
               "select": r"\[water\] reflections \(\d+ frames, ", "match": r"\(\d+ frames, [1-9]\d* traced\)", "min": 1,
               "windows": judged}]
    return g


def job(name, save, kind, first):
    expect = [
        r"\[auto\] scene ready",
        r"\[hot\] core generation \d+ loaded",
        r"\[ngx\] RR create .*: Success",
        r"\[sl\] DLSS-G frame \d+: status 0x0, presented per app frame 2, displayed/rendered over the last \d+ frames 2\.00",
        r"rt passes: GPU cost [0-9.]+ ms/frame \(average of \d+ frames\); TLAS this frame [1-9]\d*, previous frame \d+, none 0",
        r"\[cfg\] settings read",
        r"\[trace\] frame \d+ written",
        r"\[geo\] transform feedback enabled: .*\(stride 88\)",
        r"\[hair origin\] startup auxiliary capability 1",
        r"\[alpha hair\] native coverage records \d+; invalid/missing native state \d+",
        r"gi: background builds done: [1-9]\d* builds in \d+ ms",
        r"gi: background builds \(\d+ frames\):",
        r"rt stats \(\d+ frames, alpha test on\): primary-ray check match",
        r"\[rrpost\] exposure held \(qa_exposure_hold=1\) at EV [-0-9.]+",
        r"\[rrpost\] exposure hold released after [1-9]\d* writes",
    ]
    if kind == "s8":
        expect += [r"\[water\] water geometry capture on \(rt_water_geometry=1",
                   r"\[water\] reflections \(\d+ frames, [1-9]\d* traced\)",
                   r"\[water\] rt/water_reflect\.hlsl\?\S*RT_SPEC_NATIVE=1\S* water_reflect: built \(spec hair=1 edge=\d+\)"]
    forbid = [r"\[crash\] exception 0x", "VK_ERROR_DEVICE_LOST", r"\[hot\] core (rejected|faulted)",
              "FAILED, keeping previous", "compile call failed", "RR creation failed", "RR evaluate failed",
              "skipped: no variant for", r"\[water\] reflections unavailable", "pipeline creation failed",
              "shader module creation failed", r"\[geo\] rt_native_hair rejected", r"\[alpha hair\] rt_native_hair requires restart",
              r"\[hair origin\] enable rejected", "sun: WARNING \\d+ roof volumes within", "over kSunMaxVolumes on [1-9]",
              "set 1 [1-9]\\d* out-of-range reads skipped", r"\[bvh\] mesh pool full at", r"\[cascade\] could not allocate"]
    runner = [r"\[motion-plan\] pointer centred in owned foreground game"]
    runner += [rf"\[motion-trace\] phase={labels.index(l)} label={l} repeat=0" for l in traced]
    runner += [rf"\[motion-plan\] complete phases={len(plan)}\b", "^Run ended: done"]
    j = {
        "name": name, "save": save, "session": False, "traces": 0, "hold": 90, "timeout": seconds + 300, "repeat": 1,
        "live_batch": LIVE, "set": f"{ULTRA},{HAIR_START}",
        "args": ["-RealPresent", "-Foreground", "-Move", "-MovePlan", f"captures/live_batches/{BATCH}/plans/hq_plan.json"],
        "patterns": [r"\[hot\]", r"\[ngx\] RR create", r"\[sl\] DLSS-G frame", r"\[pace\]", r"\[watch\]",
                     "rt passes: GPU cost", "gi: GPU cost", r"\[rt\] scene wait", r"\[rt\] scene build GPU",
                     r"\[trace\]", r"\[shaders\].*\?", r"\[crash\]", "FAILED", "failed", "rt stats", r"\[cascade\] casters",
                     r"\[geo\]", r"\[alpha hair\]", r"\[alpha scalp\]", r"\[hair origin\]", "gi: background builds",
                     "building in the background", r"\[water\]", "skipped: no variant for", r"\[bvh\]",
                     r"\[rrpost\] exposure (held|hold|probe)", r"\[core\] guard:", "sun: WARNING"],
        "expect": expect, "forbid": forbid, "expect_runner": runner, "stop_on_dump": True,
        "purpose": ("Native hair qualification (docs/progress/worktree-agent-a78f48005e3b350bd.md checklist) at Ultra "
                    "(smoke_ultra keys) with the hair startup latches: first enable built in the background (no present "
                    f"gap >= {LONGEST_MS} ms, all done by settle-cam; first origin toggle built in toggle-off-a), PNG shots off/hair/off/hair/off for the "
                    "character float energy (char_energy.py), rt_stats windows (primary check, hair origin), cost "
                    "OFF/ON/OFF; every judged window: no background build, hair/scalp invalid 0, bounds 0, opaque "
                    "cutouts 0" + ("; water reflections traced with hair on." if kind == "s8" else "; hair and scalp records > 0 with hair on.")),
        "estimated_phase_seconds": seconds,
        "expect_every": expect_every(kind),
    }
    return j


# Cost2: full native hair is the reference, GI cull is deliberately disabled.
COST_KEYS = ("rt_hair_pool_trim", "rt_hair_cheap", "rt_hair_gi_mean", "rt_hair_visibility")
BASE.update({key: "0" for key in COST_KEYS})


def cost2_job(name, save, kind):
    states = [("pool", {"rt_hair_pool_trim": "1"}),
              ("cheap", {"rt_hair_cheap": "1"}),
              ("visibility", {"rt_hair_visibility": "1"}),
              ("mean", {"rt_hair_gi_mean": "1"}),
              ("combined", {key: "1" for key in COST_KEYS})]
    phases = [step("warmup", 25, qa_exposure_hold="0", **ON)]
    phases += [step("warm-" + label, 25, **dict(ON, **keys)) for label, keys in states if "rt_hair_gi_mean" not in keys]
    # hq1 13:32: cards/origin-off first selected during shot-cards. Prebuild
    # that exact state, then warm full hair again before any judged shot.
    phases += [step("warm-cards", 25), step("warm-full", 25, **ON),
               step("settle-cam", 10, **ON), step("shot-cards", SHOT, True),
               step("cost-cards", 25), step("shot-full-0", SHOT, True, **ON),
               step("cost-full-0", 25, **ON)]
    # Shared full-hair repeats bracket each individual ON for both observables.
    for i, (label, keys) in enumerate(states, 1):
        # Mean can lose the device: finish every unrelated observable first.
        if "rt_hair_gi_mean" in keys:
            phases += [step("warm-" + label, 25, **dict(ON, **keys))]
        phases += [step("shot-" + label, SHOT, True, **dict(ON, **keys)),
                   step("cost-" + label, 25, **dict(ON, **keys)),
                   step("shot-full-" + str(i), SHOT, True, **ON),
                   step("cost-full-" + str(i), 25, **ON)]
    phases += [step("restore", 5, qa_exposure_hold="0")]
    # Dialogue3 filled the pool before warm-pool. Keep trimming on from load,
    # including full-hair references; this scene no longer measures trim A/B.
    if name.startswith("hq4-"):
        for phase in phases:
            phase["set"] = phase["set"].replace("rt_hair_pool_trim=0", "rt_hair_pool_trim=1")
    j = job(name, save, kind, False)
    j["args"][-1] = f"captures/live_batches/{BATCH}/plans/hq_cost2_plan.json"
    j["set"] += "," + ",".join(key + "=0" for key in COST_KEYS) + ",rt_hair_gi_cull=0"
    if name.startswith("hq4-"):
        j["set"] = j["set"].replace("rt_hair_pool_trim=0", "rt_hair_pool_trim=1")
    j["estimated_phase_seconds"] = sum(p["seconds"] for p in phases)
    j["timeout"] = j["estimated_phase_seconds"] + 300
    windows = [p["label"] for p in phases if p["label"].startswith(("shot-", "cost-"))]
    native_windows = [w for w in windows if not w.endswith("cards")]
    costs = [w for w in windows if w.startswith("cost-")]
    gates = []
    for gate in j["expect_every"]:
        text = gate["name"]
        if text.startswith(("first toggle:", "no freeze:")):
            continue
        if gate.get("windows"):
            gate["windows"] = native_windows if "participation:" in text else (["settle-cam"] if text.startswith("first enable") else windows)
        gates.append(gate)
    for prefix in ("rt passes", "gi"):
        metric = rf"{prefix}: GPU cost [0-9.]+ ms/frame \(average of 300 frames\)"
        if prefix == "rt passes":
            metric += r"; TLAS this frame [1-9]\d*, previous frame \d+, none 0"
        gates.append({"name": prefix + " GPU cost in every cost2 cost window", "select": prefix + ": GPU cost",
                      "match": metric, "min": 1, "windows": costs})
    gates.append({"name": "scene build GPU includes mean precompute in every cost2 cost window",
                  "select": r"\[rt\] scene build GPU", "match": r"; total median [0-9.]+ ms, p99 [0-9.]+ ms",
                  "min": 1, "windows": costs})
    gates.append({"name": "compact holds participate in every pool and combined ON window",
                  "select": r"\[bvh\] last .*hold pack",
                  "match": r"; hold pack [1-9]\d* jobs \([1-9]\d* bytes\), copy \d+ jobs \(\d+ bytes\), unavailable 0 builds",
                  "min": 1, "windows": ["shot-pool", "cost-pool", "shot-combined", "cost-combined"]})
    gates.append({"name": "visibility selector evaluates hair draws in every visibility and combined ON window",
                  "select": r"\[alpha hair\] visibility evaluated",
                  "match": r"visibility evaluated [1-9]\d*; eligible \d+; selected \d+; cards \d+; unknown \d+",
                  "min": 1, "windows": ["shot-visibility", "cost-visibility", "shot-combined", "cost-combined"]})
    j["expect_every"] = gates
    j["expect"] = [x for x in j["expect"] if "primary-ray check match" not in x]
    j["expect"].append(r"gi: GPU cost [0-9.]+ ms/frame \(average of 300 frames\)")
    j["expect"].append(r"\[hair mean\] rt_hair_gi_mean ignored: disabled pending GPU device-loss root-cause repair")
    j["expect_runner"] = [r"\[motion-plan\] pointer centred in owned foreground game"]
    j["expect_runner"] += [rf"\[motion-trace\] phase={i} label={p['label']} repeat=0"
                           for i, p in enumerate(phases) if p.get("trace_at")]
    j["expect_runner"] += [rf"\[motion-plan\] complete phases={len(phases)}\b", "^Run ended: done"]
    if name.startswith("hq4-"):
        j["args"][-1] = f"captures/live_batches/{BATCH}/plans/hq_cost3_trim_plan.json"
    j["purpose"] = ("Mean requests are quarantined/ignored; cannot qualify mean cost or energy. hair-cost4: hair-on full/pool/full/cheap/full/visibility/full/mean/full/combined/full, independent "
                    "rt passes and GI GPU cost with character float energy. Same-run cards baseline. "
                    "Zero pool-full or scene skips, including Dialogue3. No GI cull. "
                    "Visibility is the certified mapped static-draw subset; unknowns stay native. Zero eligible/cards is no qualification.")
    return j, phases


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(HERE))
    a = ap.parse_args()
    out = Path(a.out)
    (out / "jobs").mkdir(parents=True, exist_ok=True)
    (out / "plans").mkdir(parents=True, exist_ok=True)
    for name, save, kind in SAVES:
        j, phases = cost2_job(name, save, kind)
        (out / "plans" / ("hq_cost3_trim_plan.json" if name.startswith("hq4-") else "hq_cost2_plan.json")).write_text(json.dumps(phases, indent=1) + "\n", encoding="utf-8")
        (out / "jobs" / f"{name}.json").write_text(json.dumps(j, indent=1) + "\n", encoding="utf-8")
    print(f"{len(SAVES)} cost2 jobs; {len(phases)} phases/{sum(p['seconds'] for p in phases)} s per scene -> {out}")



if __name__ == "__main__":
    main()
