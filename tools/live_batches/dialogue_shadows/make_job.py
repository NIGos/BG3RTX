"""Writes the dialogue_shadows jobs and their shared plan (README.md). Rerun after editing the phases or gates:

    py -3 tools/live_batches/dialogue_shadows/make_job.py            # jobs/ and plans/ of this folder
    py -3 tools/live_batches/dialogue_shadows/make_job.py --no-hair --out <scratch dir>
        fallback without the native-hair startup (stride 64, the user's own layout) if a hair job fails to start
"""
import argparse
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
BATCH = "dialogue_shadows"
PLAN = "ds_dialogue.json"
SAVES = [("ds1-dialogue1-fg2", "Test Dialogue1"), ("ds2-dialogue2-fg2", "Test Dialogue2"),
         ("ds3-dialogue3-fg2", "Test Dialogue3")]

# The user's session of 2026-11-02 06:24 (bg3rtx_user.ini over preset=ultra, bg3rtx.log): stride 64, xfb 1024 MB.
USER = ("ls_box=2,rt_hit_material=1,rt_hit_material_instanced=1,rt_hit_material_wind_rgb=1,gi_hit_direct=1,gi_hit_sun=1,"
        "rf_hit_shade=1,rf_hit_gi=1,rt_alpha_spill=1")
# Startup latches for the hair A/B (NATIVE_HAIR.md): 88-byte vertices, physical-origin image. Buffers scaled 88/64 from
# the user's explicit 1024 / 427 MB (an explicit rt_xfb_mb is not scaled by the stride, common/rt_capture_budget.h:53;
# the user's opaque peak 812.7 MB x 88/64 = 1117 MB would not fit 1024).
HAIR_START = "rt_native_hair=1,rt_native_scalp=1,rt_hair_physical_origin=1,rt_origin=1,rt_xfb_mb=1408,rt_cascade_mb=587"
NOHAIR_START = "rt_native_hair=0,rt_native_scalp=0,rt_hair_physical_origin=0,rt_origin=1,rt_xfb_mb=1024,rt_cascade_mb=427"
BASE_SET = ("preset=ultra,fg=2,headless=0,fps_cap=0,rt=1,rr=1,rr_post=1,reflex=1,crash_dump=1,rt_timing_frames=60,"
            "rt_debug=0,ls_debug=0,gi_debug=0,rf_debug=0,rr_show=0,rf_stats=0,ls_stats=0,ls_check=0,dump_images=1,"
            "dump_png_width=768,dump_raw=0,dump_preview_f32=1,dump_shaders=0,pan_capture=0,pan_inputs=0,pan_prepare=0,rt_order_trace=0,"
            "rt_stability_log=0,rt_stats=0,rt_primary_check=0,rt_primary_epoch=0,fg_state_every=30,rt_omm=0,"
            "rt_sun_cascade_current=0,rt_native_tileset=0,ls_k=0,ls_samples=0,ls_shadowed=3,rt_bvh_split=1,rt_alpha=1,"
            "qa_exposure_hold=0,rt_scene_wait=1,rt_scene_wait_log_frames=60,rt_cascade_casters=1,rt_dyn_hold=4,"
            "rt_shadows=1,contact_shadows=1,rt_local_shadows=1,ls_owners=31,ls_noshadow=1,gi_ao=1.0")

# Every phase writes every toggled key, so a phase reads back on its own (no carried-over toggle).
BASE = {"rt_shadows": "1", "contact_shadows": "1", "rt_local_shadows": "1", "ls_box": "2", "ls_owners": "31",
        "ls_noshadow": "1", "rt_cascade_casters": "1", "rt_dyn_hold": "4", "gi_ao": "1.0", "rt_native_hair": "0",
        "rt_native_scalp": "0", "rt_hair_physical_origin": "0", "ls_debug": "0", "rt_debug": "0", "ls_stats": "0",
        "rt_stats": "0", "rt_primary_check": "0", "qa_exposure_hold": "1", "rt_occ_stats": "0", "rt_occ_stats_frames": "60", "rt_sun_roofs": "1", "rt_sun_replace": "1", "gi": "1", "ls_box_cascade_old": "0", "rt_cascade_static_only": "0"}
HAIR = {"rt_native_hair": "1", "rt_native_scalp": "1"}
ORIGIN = dict(HAIR, rt_hair_physical_origin="1")


def step(label, seconds, trace=False, **keys):
    s = dict(BASE, **keys)
    out = {"label": label, "kind": "static", "speed": "static", "seconds": seconds, "repeat_ms": 0, "scan": 16,
           "capture_at": 0, "serial": 0, "motion_start": seconds, "motion_end": seconds, "pixels_per_second": 0,
           "direction": 0, "set": ",".join(f"{k}={v}" for k, v in s.items())}
    if trace:
        out["trace_at"] = trace if isinstance(trace, list) else [5]
    return out


SHOT = 8   # one PNG trace at 5 s; 8 s holds 3-4 lines of 60 frames at the unfocused 30 fps floor
plan = [
    # Shader variants: warmed by the ds0 job (below), which every ds job requires.
    step("warmup", 20, qa_exposure_hold="0"),                        # dialogue settles
    step("settle-cam", 10, qa_exposure_hold="0"),                    # ds1 10:54: the dialogue camera still moved 41 s after load
    # The first held shot right after the load (user screens 7-9, port: the moving face band is in THIS shot; the later
    # diag-occ windows may sit in another cut). Its trace proves the framing; sun rays on character px with distances.
    step("diag-view-occ-first", 12, [6], rt_occ_stats="1", qa_exposure_hold="0"),
    # User 2026-11-03: sun between the buildings that should be static but moves: (f) and (g) first, in this shot.
    # (f) cascade copies re-fitted to the close-up camera; (g) the roof trust (rt_sun_roofs) and the whole replace rule.
    # Every phase is a 'static' motion step: the runner sends no input, so a moving band is from our side or the game's.
    step("diag-occ-frames-casc0", 6, rt_occ_stats="1", rt_occ_stats_frames="4", rt_cascade_casters="0", qa_exposure_hold="0"),
    step("diag-view-occ-roofs0", 6, [3], rt_occ_stats="1", rt_occ_stats_frames="4", rt_sun_roofs="0", qa_exposure_hold="0"),
    step("diag-view-occ-replace0", 6, [3], rt_occ_stats="1", rt_occ_stats_frames="4", rt_sun_replace="0", qa_exposure_hold="0"),
    # (e) a cinematic light rig: the game's mask (red) vs our traced sun (green) in the same first shot; an offset band
    # of the same shape = another light direction.
    step("diag-view-sunmask-first", 8, [5], rt_debug="2", qa_exposure_hold="0"),
    # Frame by frame (4-frame lines) in the same shot, rt_dyn_hold 4 then 0: a moving occluder shows as kind/distance
    # changing line to line; held/stale geometry as 'held' that vanishes with hold 0.
    step("diag-occ-frames-hold4", 6, rt_occ_stats="1", rt_occ_stats_frames="4", qa_exposure_hold="0"),
    step("diag-occ-frames-hold0", 6, rt_occ_stats="1", rt_occ_stats_frames="4", rt_dyn_hold="0", qa_exposure_hold="0"),
    step("shot-off-a", SHOT, True),
    step("shot-nosun-on", SHOT, True, rt_shadows="0", contact_shadows="0"),   # the game's own sun shadow map only
    step("shot-nolocal-on", SHOT, True, rt_local_shadows="0"),               # the game's own local-light shadows
    step("shot-off-b", SHOT, True),
    step("shot-nobox-on", SHOT, True, ls_box="0"),                   # box (projector) lights untraced
    step("shot-charlights-on", SHOT, True, ls_owners="15"),          # lights owned by characters untraced
    step("shot-noart-on", SHOT, True, ls_noshadow="2"),              # lights the art leaves unshadowed untraced
    step("shot-off-c", SHOT, True),
    step("shot-static-only-off", SHOT, True, rt_cascade_static_only="0"),
    step("shot-static-only-on", 12, [8], rt_cascade_static_only="1"),
    step("shot-nocascade-on", SHOT, True, rt_cascade_casters="0"),   # last frame's cascade soup out of the sun TLAS
    step("shot-boxold-on", SHOT, True, ls_box_cascade_old="1"),      # the old box passes: the cascade copy shadows local/box lights
    step("shot-nohold-on", SHOT, True, rt_dyn_hold="0"),            # no held (absent) draws while static
    step("shot-noao-on", SHOT, True, gi_ao="0"),                     # RT AO off
    step("shot-nogi-on", SHOT, True, gi="0"),                        # no GI at all: no RT AO and no RT bounce (port: bounce share)
    step("shot-off-d", SHOT, True),
    step("shot-hair-on", SHOT, True, **HAIR),                        # native Hair + Scalp coverage (else opaque cards)
    step("shot-hairorigin-on", SHOT, True, **ORIGIN),                # + physical secondary origins
    step("shot-off-e", SHOT, True),
    # H7 (moving shadows): three traces 6 s apart (the real gap is the frame numbers of the '[trace] frame N written'
    # lines), user config and the game's own sun map only. Moving with RT, still without = RT-only animated casters.
    step("motion-base-fix-off", 22, [6, 12, 18], rt_cascade_static_only="0"),
    step("motion-base", 22, [6, 12, 18], rt_cascade_static_only="1"),
    step("motion-nosun", 22, [6, 12, 18], rt_shadows="0", contact_shadows="0"),
    step("view-lsremoved", 12, [6], ls_debug="2"),                # local light removed x4 (where D-LS darkens)
    step("view-lsowners", 12, [6], ls_debug="9"),                 # evaluated lights by owner (magenta character)
    step("diag-view-sunmask", 12, rt_debug="2"),             # game sun mask (red) vs traced (green): direction/caster set
    step("diag-ls-stats", 10, ls_stats="1"),                         # light removed % of the lit luminance
    # rt_occ_stats (docs/DIALOGUE_SHADOWS.md "Occluder-kind diagnostic"): per source the character px darkened, the
    # mean darkening, the occluder kinds and distances; one line per 60 frames. Each window flips one key so the kind
    # it names must vanish (validation of the kind) and the darkening it causes is read off directly.
    step("diag-occ-base", 10, rt_occ_stats="1"),
    step("diag-occ-nohold", 10, rt_occ_stats="1", rt_dyn_hold="0"),         # 'held' share -> 0
    step("diag-occ-nocascade", 10, rt_occ_stats="1", rt_cascade_casters="0"),   # 'cascade' share -> 0
    step("diag-occ-hair", 10, rt_occ_stats="1", **ORIGIN),                    # hair/scalp kinds replace opaque cards
    step("diag-occ-boxold", 10, rt_occ_stats="1", ls_box_cascade_old="1"),    # before the box cascade-mask fix: local/box 'cascade' > 0
    step("diag-rt-hairoff-a", 10, rt_stats="1", rt_primary_check="1"),
    step("diag-rt-hairon", 10, rt_stats="1", rt_primary_check="1", **ORIGIN),
    step("diag-rt-hairoff-b", 10, rt_stats="1", rt_primary_check="1"),
    step("settle", 5),                                               # stats off: the cost windows start clean
    step("cost-off-a", 10),
    step("cost-hair", 10, **ORIGIN),
    step("cost-off-b", 10),
    step("restore", 5, qa_exposure_hold="0", rt_cascade_static_only="0"),
]
labels = [p["label"] for p in plan]
shots = [l for l in labels if l.startswith(("shot-", "motion-"))]
traced = [p["label"] for p in plan if p.get("trace_at")]
cost = [l for l in labels if l.startswith("cost-")]
occ = [l for l in labels if l.startswith(("diag-occ-", "diag-view-occ-"))]
judged = shots + [l for l in labels if l.startswith("view-")] + cost
seconds = sum(p["seconds"] for p in plan)
cascade_on = [p["label"] for p in plan if "rt_cascade_casters=1" in p["set"] and p["label"] in judged]

expect_every = [
    {"name": "candidate host: key-zero coverage counter in every cascade fix control window",
     "select": r"\[cascade\] casters: cascade \d+, \d+ frames;",
     "match": r"cascade casters with key 0 [0-9.]+ per frame", "min": 1,
     "windows": ["shot-static-only-off", "shot-static-only-on", "motion-base-fix-off", "motion-base"]},
    {"name": "scene wait: every judged window traces its own frame's scene (0 timeouts; stale shadows otherwise)",
     "select": r"\[rt\] scene wait: \d+ frames", "match": r"got this frame's scene, 0 timeouts", "min": 1,
     "windows": judged},
    {"name": "capacity: the RT capture drops no opaque draw (buffer full 0.00) in every judged window",
     "select": r"\[geo\] RT capture, last \d+ frames:", "match": r"opaque draws dropped \(buffer full\) 0\.00,",
     "min": 1, "windows": judged},
    {"name": "capacity: no alpha-tested draw made opaque (region full 0.00) in every judged window (hypothesis 6)",
     "select": r"\[geo\] RT capture, last \d+ frames:", "match": r"alpha draws made opaque \(region full\) 0\.00,",
     "min": 1, "windows": judged},
    {"name": "capacity: the cascade casters buffer is never full (buffer full 0.0) in every judged window with the casters on",
     "select": r"\[cascade\] casters: cascade \d+, \d+ frames;", "match": r", buffer full 0\.0, added ", "min": 1,
     "windows": cascade_on, "when_present": True},
    {"name": "occ stats: every diag-occ window has a complete line with the sun/AO pass and the D-LS occ variant running",
     "select": r"occ stats \(\d+ frames\): character px", "match": r"passes: sun/AO [1-9]\d*, D-LS [1-9]\d* frames",
     "min": 1, "windows": occ},
    {"name": "occ stats: the sun replica line is present in every diag-occ window (read: mismatch <= 1% of the sun rays, predicted.json)",
     "select": r"occ stats \(\d+ frames\): character px", "match": r"sun replica mismatch \d+ of \d+ rays",
     "min": 1, "windows": occ},
]
# Pair registration, rather than scene-build camera logs, decides whether shots are comparable.
# Run pair_registration.py before interpreting character energy; never warp the images.


def job(name, save, first, hair):
    start = HAIR_START if hair else NOHAIR_START
    expect = [
        r"\[auto\] scene ready",
        r"\[hot\] core generation \d+ loaded",
        r"\[ngx\] RR create .*: Success",
        r"\[sl\] DLSS-G frame \d+: status 0x0, presented per app frame 2, displayed/rendered over the last \d+ frames 2\.00",
        r"\[pace\] rendered frame time over 600 frames",
        r"rt passes: GPU cost [0-9.]+ ms/frame \(average of \d+ frames\); TLAS this frame [1-9]\d*, previous frame 0, none 0",
        r"\[cfg\] settings read",
        r"\[trace\] frame \d+ written",
        r"\[geo\] transform feedback enabled: .*\(stride %d\)" % (88 if hair else 64),
        r"ls box: active \(mode 2;",
        r"ls: local-light shadow pass GPU median .*box 2\)",
        r"ls stats \(\d+ frames\): [0-9.]+M px/frame",
        r"rt stats \(\d+ frames, alpha test on\): primary-ray check match",
        r"\[rrpost\] exposure held \(qa_exposure_hold=1\) at EV [-0-9.]+",
        r"\[rrpost\] exposure hold released after [1-9]\d* writes",
        r"occ stats: active \(sun traced 1, RT GI 1;",
        r"occ stats \(\d+ frames\): character px",
    ]
    if hair:
        expect += [r"\[hair origin\] startup auxiliary capability 1",
                   r"\[alpha hair\] native coverage records \d+; invalid/missing native state \d+"]
    forbid = [r"\[crash\] exception 0x", "VK_ERROR_DEVICE_LOST", r"\[hot\] core (rejected|faulted)",
              "FAILED, keeping previous", "compile call failed", "RR creation failed", "RR evaluate failed",
              r"\[geo\] rt_native_hair rejected", r"\[alpha hair\] rt_native_hair requires restart",
              r"\[hair origin\] enable rejected"]
    # Foreground (user 2026-11-02 10:xx: the PC is free). The ds1 stall was a shader compile, not focus.
    runner = [r"\[motion-plan\] pointer centred in owned foreground game"]
    runner += [rf"\[motion-trace\] phase={labels.index(l)} label={l} repeat=0" for l in traced]
    runner += [rf"\[motion-plan\] complete phases={len(plan)}\b", "^Run ended: done"]
    j = {
        "name": name, "save": save, "session": False, "traces": 0, "hold": 90, "timeout": seconds + 250, "repeat": 1,
        "live_batch": "dialogue-shadows",
        "set": f"{BASE_SET},{USER},{start}",
        "args": ["-RealPresent", "-Foreground", "-Move", "-MovePlan", f"captures/live_batches/{BATCH}/plans/{PLAN}"],
        "patterns": [r"\[hot\]", r"\[ngx\] RR create", r"\[sl\] DLSS-G frame", r"\[pace\]", r"\[watch\]",
                     "rt passes: GPU cost", "rt applied", r"\[rt\] scene wait", r"\[rt\] scene build GPU",
                     r"\[cpu\] capture hooks", r"\[trace\]", r"\[shaders\].*\?", r"\[crash\]", "FAILED", "ls box",
                     "ls stats", "ls: local-light shadow pass GPU", "rt stats", "sun stats", "sun check", "gi: GPU cost",
                     r"\[cascade\] casters", r"\[geo\]", r"\[alpha hair\]", r"\[alpha scalp\]", r"\[hair origin\]",
                     r"\[bvh\]", r"\[rrpost\] exposure (held|hold|probe)", "occ stats"],
        "expect": expect, "forbid": forbid, "expect_runner": runner, "stop_on_dump": True,
        "purpose": ("Dialogue character shadows (docs/DIALOGUE_SHADOWS.md 2026-11-02): attribution inside one run at a held "
                    "dialogue shot, user config (Ultra + bg3rtx_user.ini). PNG shots base / RT sun off / local off / box off / "
                    "character lights off / art-unshadowed off / cascade casters off / dyn hold off / AO off / native hair+scalp "
                    "/ + physical origin, H7 moving-shadow PNG triplets 6 s apart (RT on and game sun only), ls_debug 2 and 9 "
                    "and rt_debug 2 views, ls_stats and rt_stats windows, hair cost OFF/ON/OFF. "
                    "Static-only 0/1 and motion-base fix OFF/ON test the dynamic-cascade fallback; "
                    "pair_registration.py requires integer image registration (0,0) per ON/OFF neighbour. "
                    "predicted.json names which shot removes the shadow under each hypothesis."),
        "estimated_phase_seconds": seconds,
        "expect_every": expect_every,
    }
    if name != first:
        j["requires_success_of"] = [first]
    return j


# ---- ds0: shader warm-up job (no measurement) ----
# Every key set the ds plan uses is a separate variant family (the hot keys pick the defines: RT_HAIR_PHYSICAL_ORIGIN
# from rt_hair_physical_origin, RT_CASCADE_MASKS/RT_CASCADE_SUN from rt_cascade_casters, pan_primary from
# rt_primary_check, the box pass from ls_box, ...). Their first compile blocks the render thread for 0.6-20 s each
# (ds1 reruns 2026-11-02 10:03 and 10:19: 53 s and 2.5 min without a present). This job requests each family twice
# (a family requested while the render thread was blocked on another is caught by the second pass); afterwards the
# runtime cache (%LOCALAPPDATA%\BG3RTX\shader_cache) holds them for ds1-ds3 as long as test-main's shaders are unchanged.
WARM_PLAN = "ds_warm.json"
STATS = dict(rt_stats="1", rt_primary_check="1", qa_exposure_hold="0")
_warm_sets = [("base", {}), ("hair", ORIGIN), ("nocasc", dict(rt_cascade_casters="0", ls_box="0")),
              ("views", dict(rt_debug="2", ls_debug="2")), ("owners", dict(ls_debug="9", ls_box="0")),
              ("nosun", dict(rt_shadows="0", contact_shadows="0", rt_local_shadows="0")),
              ("occ", dict(rt_occ_stats="1")), ("occhair", dict(ORIGIN, rt_occ_stats="1")),
              ("occnocasc", dict(rt_occ_stats="1", rt_cascade_casters="0")),
              ("boxold", dict(rt_occ_stats="1", ls_box_cascade_old="1"))]
warm_plan = [step(f"diag-warm-{n}-{p}", 25, **dict(STATS, **k)) for p in ("a", "b") for n, k in _warm_sets]
warm_plan.append(step("restore", 5, qa_exposure_hold="0", rt_cascade_static_only="0"))
warm_seconds = sum(p["seconds"] for p in warm_plan)


def warm_job(save, hair):
    start = HAIR_START if hair else NOHAIR_START
    return {
        "name": WARM, "save": save, "session": False, "traces": 0, "hold": 90, "timeout": warm_seconds + 300,
        "repeat": 1, "live_batch": "dialogue-shadows", "set": f"{BASE_SET},{USER},{start}",
        "args": ["-RealPresent", "-Foreground", "-Move", "-MovePlan", f"captures/live_batches/{BATCH}/plans/{WARM_PLAN}"],
        "patterns": [r"\[hot\]", r"\[watch\]", r"\[shaders\]", r"\[core\] gi: ", r"\[crash\]", "FAILED", r"\[fault\] presents"],
        "expect": [r"\[auto\] scene ready", r"\[hot\] core generation \d+ loaded", r"\[cfg\] settings read",
                   r"\[shaders\] cache: prefetched \d+ requests",
                   r"\[geo\] transform feedback enabled: .*\(stride %d\)" % (88 if hair else 64)],
        "forbid": [r"\[crash\] exception 0x", "VK_ERROR_DEVICE_LOST", r"\[hot\] core (rejected|faulted)",
                   "FAILED, keeping previous", "compile call failed"],
        "expect_runner": [r"\[motion-plan\] pointer centred in owned foreground game",
                          rf"\[motion-plan\] complete phases={len(warm_plan)}\b", "^Run ended: done"],
        "stop_on_dump": True,
        "purpose": ("Shader warm-up for ds1-ds3 (no measurement): requests every variant family of the ds plan's key "
                    "sets twice so the measured runs never compile on the render thread."),
        "estimated_phase_seconds": warm_seconds,
    }


WARM = "ds0-warm-dialogue1-fg2"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-hair", action="store_true", help="stride-64 fallback (no native-hair startup)")
    ap.add_argument("--out", default=str(HERE))
    a = ap.parse_args()
    out = Path(a.out)
    (out / "jobs").mkdir(parents=True, exist_ok=True)
    (out / "plans").mkdir(parents=True, exist_ok=True)
    (out / "plans" / PLAN).write_text(json.dumps(plan, indent=1) + "\n", encoding="utf-8")
    (out / "plans" / WARM_PLAN).write_text(json.dumps(warm_plan, indent=1) + "\n", encoding="utf-8")
    (out / "jobs" / f"{WARM}.json").write_text(json.dumps(warm_job(SAVES[0][1], not a.no_hair), indent=1) + "\n",
                                               encoding="utf-8")
    for name, save in SAVES:
        j = job(name, save, SAVES[0][0], not a.no_hair)
        j["requires_success_of"] = [WARM]   # ds1 gate failures (not the run working) must not skip ds2-ds3
        if a.no_hair:   # the hair phases still toggle the keys; with the startup off they are inert (logged rejected)
            j["forbid"] = [f for f in j["forbid"] if "hair" not in f]
        (out / "jobs" / f"{name}.json").write_text(json.dumps(j, indent=1) + "\n", encoding="utf-8")
    print(f"{len(SAVES)} jobs, plan {PLAN}: {len(plan)} phases, {seconds} s, {sum(len(p.get('trace_at', [])) for p in plan)} PNG traces per job -> {out}")


if __name__ == "__main__":
    main()
