"""handout_qual: qualify the scene handout (rt_scene_handout) in one live session. STAGED, not queued, not authorized.

    py -3 -m tools.live_batches_staged.handout_qual.make_jobs [job name ...]

Five launches (README.md): hq-s8-ultra and hq-inn-ultra toggle rt_scene_handout 0 / 2 / 0 in static, pan, RTX Hair,
energy and primary-check windows of one focused, uncapped FG 3x run each; hq-s8-high and hq-inn-high the static and
pan windows on High; hq-smoke-s8 is the release Ultra preset with the launcher knob's keys on from launch.
predicted.json precedes the jobs. Helpers are perf_ab's (one copy). Nothing here writes a watched queue inbox.
"""
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))
from tools.live_batches_staged.perf_ab import make_jobs as pab

BATCH = "tools/live_batches_staged/handout_qual"
PERIOD = pab.PERIOD
# Measured frames after the 60-frame settle: 3 periods in a static and in a pan window, so a window holds at least 2
# complete 120-frame periods wherever the period grid falls (the first run, 2026-11-14, sized pans at 2 periods: the
# first pan side after a toggle had none in 4 of 5 launches). A pan state pans Q, then E back. Check windows log every
# 60 frames: 4 periods. Sized for the inn at the lint model's fps inside the 15 minute cap.
FRAMES = {"static": 3 * PERIOD, "pan": 3 * PERIOD, "check": 2 * PERIOD}
HIT_FRAMES = PERIOD   # hq-s8-modes hit-<state> windows: 2 periods of the 60-frame rt_stats lines
ENERGY_TRACES = 8   # per state in hq-s8-energy (the first run had 3: S8 -0.72% against an OFF spread of 647.8-656.6)
S8, INN = pab.S8, pab.INN
# Startup: the update-after-bind layout (restart key) with the handout off; dumps only for the energy traces (PNG 1280).
START = (pab.USER.replace("dump_images=0", "dump_images=1,dump_png_width=1280") +
         ",rt_scene_handout_layout=1,rt_scene_handout=0")
WAIT, HANDOUT = "\\[rt\\] scene wait: \\d+ frames", "\\[rt\\] scene handout: \\d+ frames, reserved"
LAYOUT = ["\\[rt\\] \\+ scene handout layout: storage buffer, sampled image",
          "\\[rt\\] scene handout layout on \\(set 1 update-after-bind\\)", "\\[alpha\\] set 1 layout update-after-bind",
          "\\[rt\\] scene handout: dummy TLAS \\(0 instances", HANDOUT]
FORBID = pab.FORBID + ["\\[rt\\] scene handout layout off", "\\[rt\\] scene handout: dummy TLAS (allocation|creation) failed",
                       "\\[layer\\] optional scene handout update-after-bind request failed"]
PATTERNS = pab.PATTERNS + ["\\[core\\] scene handout", "\\[alpha\\] set 1 layout", "\\[geo\\] RT capture", "rt stats \\(",
                           "\\[trace\\]", "\\[rrpost\\] exposure", "\\[water\\] (?:reflections|compose)", "\\[omm"]
PAN = dict(kind="pan", speed="slow", repeat_ms=100, key_period_ms=400, key_down_ms=100)   # opt_profile's keyboard pan


def keys(handout, hair=0, check=0, hold=0):
    return (f"rt_scene_handout={handout},rt_hair={hair},rr_hair_strand_aa={2 * hair},rt_stats={check},"
            f"rt_primary_check={check},rt_stats_period_frames={60 if check else PERIOD},qa_exposure_hold={hold}")


def window(label, kind, settings):
    """One measured window; a moving one pans Q (label -q) or E back over the same path (label -e)."""
    p = pab.phase(label, 12, settings, frames=FRAMES[kind])
    if kind != "static":
        p.update(PAN, direction=1 if label.endswith("-e") else -1)
    return p


def group(name, kind, hair=0):
    """off-a / on / off-b of one window kind; a moving kind runs Q then E in every state (the same ground)."""
    sides = ("",) if kind == "static" else ("-q", "-e")
    return [window(f"{name}-{state}{side}", kind, keys(2 if state == "on" else 0, hair, int(kind == "check")))
            for state in ("off-a", "on", "off-b") for side in sides]


def energy(state, n=3):
    """Static, exposure held, n float traces (gi_1ray_qual's shot phase): the energy check of AGENTS.md."""
    p = pab.phase(f"energy-{state}", 14.2 + 5 * (n - 3), keys(2 if state == "on" else 0, hold=1), frames=60)
    p.update(trace_at=[1 + 5 * i for i in range(n)], motion_start=17 + 5 * (n - 3), motion_end=17 + 5 * (n - 3))
    return p


def every(*a, **kw):
    return pab.every(*a, **dict(kw, min=1))   # one complete period per window at least (a pan side is one period)


def gates(plan, hair_windows=()):
    measured = [p["label"] for p in plan if p.get("frames") and not p.get("trace_at")]
    on = [w for w in measured if "-on" in w]
    timed = [w for w in measured if not w.startswith("check")]
    out = [every(n + " in every window", s, s, timed) for n, s in pab.LINES] if timed else []
    out += [
        every("handout: every first RT query handed out in the ON windows", WAIT, "handed out [1-9]\\d*", on),
        every("handout: 0 fallbacks and 0 pairing mismatches in every ON window", HANDOUT,
                  "in order [1-9]\\d*, fallbacks 0 \\(.*\\), pairing mismatches 0,", on),
        every("handout: no hit-material fallback and no OMM downgrade in every ON window", HANDOUT,
                  "downgraded 0; .*\\(fallbacks: query 0, camera 0\\)", on),
        every("scene wait: 0 timeouts in every window", WAIT, "got this frame's scene, 0 timeouts", measured),
    ]
    if timed:
        out.append(every("capacity: no opaque draw dropped in every window", "\\[geo\\] RT capture, last \\d+ frames:",
                      "opaque draws dropped \\(buffer full\\) 0\\.00,", timed))
    check = [w for w in measured if w.startswith("check")]
    if not measured:
        return []   # the energy job has no timed window
    if check:
        out.append(every("primary-ray check in every check window (ON against the OFF pair: analyze.py)",
                             "rt stats \\(\\d+ frames, alpha test on\\): primary-ray check match",
                             "primary-ray check match [0-9.]+%", check, period_frames=60, sample_frames=60))
    if hair_windows:
        out.append(every("hair period line in every hair window", "\\[hair primary\\] frame \\d+: ",
                             "\\[hair primary\\] frame \\d+: ", list(hair_windows)))
    return out


def job(name, save, start, plan, purpose, hair_windows=()):
    j = pab.focused_job(name, save, start, plan, [], LAYOUT + ["\\[ngx\\] RR create .*: Success"], purpose, batch=BATCH,
                        live_batch="handout-qual")
    traces = sum(len(p.get("trace_at", [])) for p in plan)
    # fail_fast off: sh2 18:35 stopped on one OFF-window timeout; every window is measured and analyze.py judges.
    j.update(expect_every=gates(plan, hair_windows), forbid=FORBID, patterns=PATTERNS, timeout=840, max_minutes=14,
             fail_fast=False)
    j["live_flow"].update(startup_only=["rt_scene_handout_layout", "fg"], trace_stall_s=3 if traces else 0)
    if traces:
        j["expect"] += ["\\[trace\\] frame \\d+ written", "\\[rrpost\\] exposure held \\(qa_exposure_hold=1\\) at EV [-0-9.]+"]
    return plan, j


def check_only():
    """The primary-ray windows (about 80 ms/frame) in their own launch at S8: with them hq-s8-ultra passed the 15 minute
    cap at 3 periods per window; the match does not depend on the scene's cost."""
    warm = pab.phase
    plan = [warm("warmup", 20, keys(0)), warm("warm-on", 8, keys(2)), warm("warm-check", 4, keys(2, 0, 1)), warm("warm-off", 4, keys(0))]
    plan += group("check-pan", "check") + [warm("restore", 2, keys(0))]
    plan, j = job("hq-s8-check", S8, f"{START},{pab.HAIR_START},rt_hair=0,{keys(0)}", plan,
               "Scene handout stale-scene detector at S8, Ultra, FG 3x, focused: rt_scene_handout 0 / 2 / 0 in primary-ray "
               "check windows in motion (pan Q and E). ON match not lower than the OFF pair's by more than their difference.")
    j["timeout"] = 1500   # plan 576 s at the lint model's 3 fps plus the readiness allowance (800 s)
    return plan, j


def ultra(slug, save, check=True):
    """check: the primary-ray windows (about 80 ms/frame), now in hq-s8-check (off in every ultra job)."""
    warm = pab.phase
    plan = [warm("warmup", 20, keys(0)), warm("warm-on", 8, keys(2)), warm("warm-hair-on", 8, keys(2, 1)),
            warm("warm-hair-off", 4, keys(0, 1))] + ([warm("warm-check", 4, keys(2, 0, 1))] if check else []) + [warm("warm-off", 4, keys(0))]
    plan += group("static", "static") + group("pan", "pan")
    # RTX Hair in motion only (the harder case; the static handout result does not depend on the hair path).
    plan += [warm("warm-pre-hair", 4, keys(0, 1))] + group("hair-pan", "pan", 1)
    plan += [warm("warm-pre-energy", 4, keys(0, hold=1))] + [energy(s) for s in ("off-a", "on", "off-b")]
    plan += (group("check-pan", "check") if check else []) + [warm("restore", 2, keys(0))]
    hair = [p["label"] for p in plan if p["label"].startswith("hair-") and p.get("frames")]
    return job(f"hq-{slug}-ultra", save, f"{START},{pab.HAIR_START},{pab.hair('on')},{keys(0)}", plan,
               f"Scene handout at {save}, Ultra, FG 3x, uncapped, focused: rt_scene_handout 0 / 2 / 0 in static and pan "
               "windows with stock cards, pan windows with RTX Hair On, three float traces per state for the energy check" +
               (", and primary-ray check windows in motion" if check else "") + ". Per window: scene wait, handout fallbacks, rendered frame time "
               "median and p99, every GPU cost line.", hair)


def energy_repeat():
    """S8 only, no timed window: ENERGY_TRACES float traces per state (off-a / on / off-b), exposure held, the handout
    toggled live. Decides the open energy point of docs/SCENE_HANDOUT_DESIGN.md section 8 (S8 -0.72% with 3 traces)."""
    warm = pab.phase
    plan = [warm("warmup", 20, keys(0, hold=1)), warm("warm-on", 8, keys(2, hold=1)), warm("warm-off", 4, keys(0, hold=1))]
    plan += [energy(s, ENERGY_TRACES) for s in ("off-a", "on", "off-b")] + [warm("restore", 2, keys(0))]
    return job("hq-s8-energy", S8, f"{START},{pab.HAIR_START},rt_hair=0,{keys(0)}", plan,
               f"Scene handout energy repeat at S8, Ultra, FG 3x, focused: {ENERGY_TRACES} float traces per state (rt_scene_handout "
               "0 / 2 / 0, static camera, exposure held). The ON mean of the normalized scene colour must lie inside the OFF "
               "samples' range; the spread of the OFF samples is the noise.")


# hq-s8-modes: the states that separate the stamp-TLAS path (1) from the no-wait commit (2) and confirm the fix.
MODES = (("off-a", 0, 0), ("m1", 1, 0), ("m2", 2, 0), ("fix", 2, 1), ("off-b", 0, 0))


def mode_keys(handout, hit_wait, stats=0):
    k = keys(handout, hold=1) + f",rt_scene_handout_hit_wait={hit_wait}"
    if stats:   # rt_stats without the primary-ray check (60-frame lines)
        k = k.replace("rt_stats=0", "rt_stats=1").replace(f"rt_stats_period_frames={PERIOD}", "rt_stats_period_frames=60")
    return k


def modes():
    """S8 static, exposure held: per state ENERGY_TRACES float traces, then a short rt_stats window (hit-<state>: the
    native hit-material lines). Decides docs/SCENE_HANDOUT_DESIGN.md 8.6 (the -0.20% of rt_scene_handout=2)."""
    warm = pab.phase
    plan = [warm("warmup", 20, mode_keys(0, 0)), warm("warm-m1", 8, mode_keys(1, 0)), warm("warm-m2", 8, mode_keys(2, 0)),
            warm("warm-fix", 8, mode_keys(2, 1)), warm("warm-stats", 4, mode_keys(2, 1, 1)), warm("warm-off", 4, mode_keys(0, 0))]
    for state, handout, hit_wait in MODES:
        e = energy(state, ENERGY_TRACES)
        e["set"] = mode_keys(handout, hit_wait)
        plan += [e, pab.phase(f"hit-{state}", 12, mode_keys(handout, hit_wait, 1), frames=HIT_FRAMES)]
    plan += [warm("restore", 2, keys(0) + ",rt_scene_handout_hit_wait=0")]
    hit = [f"hit-{s}" for s, _, _ in MODES]
    plan, j = job("hq-s8-modes", S8, f"{START},{pab.HAIR_START},rt_hair=0,{mode_keys(0, 0)}", plan,
                  f"Scene handout energy by mode at S8, Ultra, FG 3x, focused, static, exposure held: rt_scene_handout 0 / 1 / 2 / "
                  f"2 with rt_scene_handout_hit_wait=1 / 0, {ENERGY_TRACES} float traces per state and a short rt_stats window "
                  "each. Mode 1 and the fix inside the OFF range; mode 2 reproduces the -0.20% (unpublished commit-time "
                  "hit-material query). Reports the scene wait and the GI trace GPU ms of every state.")
    # Runner gates: only "the measured lines exist" in every hit window (user rule 2026-11-14). The values (qualified
    # hits, timeouts, fallbacks, the fix taking the committed scene) are judged by analyze.py hit_checks.
    stats = dict(period_frames=60, sample_frames=60, min=1)
    far = "\\[core\\] GI far \\(\\d+ frames\\)"
    # State lines stay out of the any-line "expect": live_flow puts a producer-sanity phase for every periodic expect
    # line after the first warm-up, in the warm-up's state. GI far needs rt_stats=1 and the core handout line
    # rt_scene_handout=2, so the OFF warm-up could never print them (hq-s8-modes.1.204217 stopped there, phase 1 of 19).
    core = "\\[core\\] scene handout: .*hit material passes on the committed scene \\d+"
    j["expect_every"] = [pab.every("GI far line in every hit window", far, far, hit, **stats),
                         pab.every("scene wait line in every hit window", WAIT, WAIT, hit, **stats),
                         pab.every("handout line in the handout windows", HANDOUT, HANDOUT, ["hit-m2", "hit-fix"], **stats),
                         pab.every("core handout line in the handout windows", core, core, ["hit-m2", "hit-fix"], **stats)]
    j["patterns"] = j["patterns"] + ["GI far \\(", "GI hit direct", "\\[hit material (?:submit|query)\\]"]
    j["energy_expect"] = {"m2": "lower"}   # analyze.py: the predicted loss is reported, not judged
    return plan, j


def high(slug, save):
    warm = pab.phase
    plan = [warm("warmup", 20, keys(0)), warm("warm-on", 8, keys(2)), warm("warm-off", 4, keys(0))]
    plan += group("static", "static") + group("pan", "pan") + [warm("restore", 2, keys(0))]
    start = START.replace("preset=ultra", "preset=high") + ",rt_hair=0," + keys(0)
    return job(f"hq-{slug}-high", save, start, plan,
               f"Scene handout at {save}, High, FG 3x, uncapped, focused: rt_scene_handout 0 / 2 / 0 in static and pan "
               "windows (no native hit materials, no cascade casters).")


def smoke():
    """The launcher's state: release policy, the Ultra preset with its cap, the knob's two keys and the user's RTX Hair
    options from launch. One state for the whole run: every window is an ON window."""
    state = "rt_hair=1,rt_hair_pixel_lod=1,rt_hair_pixel_lod_small_keep=0.25,rr_hair_strand_aa=2"
    plan = [pab.phase("warmup", 20, state), window("static-on-a", "static", state), window("pan-on-q", "pan", state),
            window("pan-on-e", "pan", state), window("static-on-b", "static", state)]
    start = (START.replace("dev=1,", "dev=0,experimental_options=1,").replace(",fps_cap=0", "")
             .replace("rt_scene_handout=0", "rt_scene_handout=2").replace("dump_images=1,dump_png_width=1280", "dump_images=0") +
             f",{pab.HAIR_START},{state}")
    windows = [p["label"] for p in plan if p.get("frames")]
    plan, j = job("hq-smoke-s8", S8, start, plan,
                  "Smoke of the launcher's Low-latency scene On state at S8: release policy, preset Ultra with its 58 fps "
                  "cap, FG 3x, RTX Hair On with distant detail 1/4 and motion stability, the handout on from launch: "
                  "handed out with 0 fallbacks and 0 wait timeouts in static and pan windows.", windows)
    j["expect_every"].append(every("FG 3x presented in every window", "\\[sl\\] DLSS-G frame \\d+: ",
                                       "presented per app frame 3", windows))
    return plan, j


def measured(p):
    """Frames after the settle (perf_ab.retime); the energy phases keep their own clock (trace_at seconds)."""
    if p.get("trace_at"):
        return None
    if p["label"].startswith("hit-"):
        return HIT_FRAMES
    return FRAMES["check" if p["label"].startswith("check") else "pan" if p.get("kind") == "pan" else "static"]


def main():
    assert (HERE / "predicted.json").exists(), "predicted.json precedes the jobs"
    made = [ultra("s8", S8, check=False), ultra("inn", INN, check=False), high("s8", S8), high("inn", INN), smoke(), energy_repeat(), check_only(), modes()]
    if sys.argv[1:]:   # job names: regenerate only these (the others keep the files of their own runs)
        made = [m for m in made if m[1]["name"] in sys.argv[1:]]
    for plan, j in made:
        for folder, value in (("plans", plan), ("jobs", j)):
            (HERE / folder).mkdir(exist_ok=True)
            (HERE / folder / f"{j['name']}.json").write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8", newline="\n")
    added = pab.retime([j["name"] for _, j in made], here=HERE, measured=measured)
    for _, j in made:   # a handout toggle rebuilds every gi/ and water pipeline with constant 1051: generous warm waits
        path = HERE / "plans" / f"{j['name']}.json"
        plan = json.loads(path.read_text(encoding="utf-8"))
        for p in plan:
            if p.get("wait_log") and p["label"].startswith("warm"):
                p["wait_log_timeout"] = 180
        path.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8", newline="\n")
    print("Generated", ", ".join(j["name"] for _, j in made), f"- no queue staging; {added} new entries appended to "
          "lint_corpus_snapshot.json")


if __name__ == "__main__":
    main()
