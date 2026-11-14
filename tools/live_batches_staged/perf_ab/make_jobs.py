"""perf_ab: where does the user's configuration lose its frames? STAGED, not queued, not authorized.

    py -3 -m tools.live_batches_staged.perf_ab.make_jobs

Five log-only launches (README.md): perf-ab-s8 and perf-ab-inn toggle the RTX Hair parts inside one focused, uncapped
Ultra / FG 3x run each; perf-ab-wide-split-0/-1 are the opt-profile-s8 state with rt_capture_wide_split 0 and 1;
perf-ab-dlss5-s8 toggles the DLSS 5 NR live switch. predicted.json precedes the jobs. Nothing here writes a watched
captures/queue inbox or launches the game.
"""
from copy import deepcopy
import json
from pathlib import Path
import re
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))
from tools.live_batches import live_flow as flow, lint_snapshot

BATCH = "tools/live_batches_staged/perf_ab"
PERIOD = 120
FRAMES = 3 * PERIOD   # as opt_profile: 2-3 complete producer periods after GPU readback latency
SETTLE = PERIOD // 2  # frames in the new state before the stats reset (after the readiness wait): builds, publishes, warm-up
S8, INN = "Water Queens House", "The Blushing Mermaid - 96h 39m"
RUNTIME = r"E:\Storage\New folder\DLSSNR Package\nvngx_dlssnr.dll"


def bundle():
    """The keys RTX Hair On writes (common/settings.h exp_hair_groom_primary "on"): read, never copied by hand."""
    text = (ROOT / "common/settings.h").read_text(encoding="utf-8")
    block = text[text.index('{"exp_hair_groom_primary", "RTX Hair"'):]
    block = block[block.index('{"on", "On",'):block.index('{{"exp_hair_groom", 1}}')]
    keys = re.findall(r'\{"(\w+)", "1"\}', block)
    assert len(keys) >= 20 and "rt_hair_groom" in keys and "rt_hair_physical_origin" in keys, keys
    return keys


# Observation only: Detailed logging, CPU timers, 120-frame periods, no dumps, no diagnostic passes.
OBSERVE = ("headless=0,crash_dump=1,detailed_logging=1,cpu_timing=1,rt_stats_period_frames=120,rt_timing_frames=120,"
           "rt_scene_wait_log_frames=120,fg_state_every=120,dump_images=0,dump_raw=0,dump_preview_f32=0,dump_shaders=0,"
           "pan_capture=0,pan_inputs=0,pan_prepare=0,rt_debug=0,ls_debug=0,gi_debug=0,rf_debug=0,rr_show=0,rt_stats=0,"
           "rt_primary_check=0,rt_occ_stats=0,ls_stats=0,ls_check=0,rf_stats=0,qa_exposure_hold=0")
# The user's configuration: Ultra, FG 3x, no rendered-frame cap (the preset's is 58; the user's too: README). dev=1 as in
# the test tree's bg3rtx.ini: the release policy would undo a part toggled out of the RTX Hair bundle.
USER = "preset=ultra,dev=1,fg=3,fps_cap=0,reflex=1,rt_capture_wide_split=1," + OBSERVE
# Startup state of the hair jobs: the whole bundle, rt_hair_mv_card=2 (specialization 1049) and the nearest trace are
# in the pipelines from the level load, the physical-origin reservation exists: every toggle below is then live.
HAIR_START = ("rt_hair=1," + ",".join(k + "=1" for k in bundle()) +
              ",rt_hair_mv_card=2,rt_hair_pixel_lod_k=1.0")
# (rt_hair, strands on screen, shadow and AO transmittance, pixel LOD, strand AA): the live keys of each state.
STATES = {
    "off":     (0, 1, 1, 0, 0),   # stock cards: rt_hair=0 makes every bundle key read 0 (the hotkey's path)
    "on":      (1, 1, 1, 0, 2),   # the RTX Hair On bundle with the user's strand AA
    "lod":     (1, 1, 1, 1, 2),   # + RTX Hair detail at distance 1/4
    "not":     (1, 1, 0, 0, 2),   # bundle without strand shadow and AO transmittance
    "groomrt": (1, 0, 1, 0, 2),   # strands in ray-traced lighting only (cards on screen)
}
WINDOWS = (("off-a", "off"), ("on", "on"), ("lod", "lod"), ("not", "not"), ("groomrt", "groomrt"), ("off-b", "off"))
FORBID = ["\\[crash\\] exception 0x", "VK_ERROR_DEVICE_LOST", "\\[hot\\] core (rejected|faulted)",
          "FAILED, keeping previous", "compile call failed", "RR creation failed", "\\[log\\] reached \\d+ MB"]
# (name, select): one complete line per 120-frame period in every measured window; presence only.
LINES = [("rt passes GPU ms", "rt passes: GPU cost"), ("GI GPU ms", "gi: GPU cost"),
         ("scene build GPU ms", "\\[rt\\] scene build GPU"), ("scene wait", "\\[rt\\] scene wait: \\d+ frames"),
         ("rendered frame time", "\\[pace\\] rendered frame time over \\d+ frames"),
         ("CPU submit", "\\[watch\\] presents"), ("VRAM used and budget", "\\[mem\\] device-local VRAM"),
         ("RR GPU ms", "\\[ngx\\] gpu RR:")]
PATTERNS = ["\\[auto\\]", "\\[cfg\\]", "\\[hot\\]", "\\[stats\\]", "\\[watch\\]", "\\[pace\\]", "\\[mem\\]", "\\[vram\\]",
            "\\[cpu\\]", "\\[bvh\\] last", "\\[rt\\] scene", "rt passes: GPU cost", "gi: GPU cost", "gi: trace pass",
            "ls: local-light shadow pass GPU median", "rf: reflection pass GPU median", "\\[ngx\\] gpu RR:",
            "\\[rrpost\\] gpu", "\\[sl\\] gpu FG input copy:", "\\[sl\\] DLSS-G frame", "\\[water\\] \\w+ GPU",
            "\\[hair primary\\] (?:frame|live|strand G-buffer)", "\\[hair groom\\] (?:frame|pixel LOD)",
            "\\[hair fibre\\]", "\\[dlss5\\]", "\\[crash\\]", "VK_ERROR", "\\[log\\]"]


def hair(state):
    on, primary, t, lod, aa = STATES[state]
    return (f"rt_hair={on},rt_hair_groom_primary={primary},rt_hair_groom_shadow_t={t},rt_hair_groom_ao_t={t},"
            f"rt_hair_pixel_lod={lod},rt_hair_pixel_lod_small_keep=0.25,rr_hair_strand_aa={aa}")


def phase(label, seconds, settings, frames=None):
    p = {"label": label, "kind": "static", "speed": "static", "seconds": seconds, "repeat_ms": 0, "scan": 16,
         "capture_at": 0, "serial": 0, "motion_start": seconds, "motion_end": seconds, "pixels_per_second": 0,
         "direction": 0, "set": settings}
    if frames:   # a frame window keeps its length through live_flow's tightening (camera-plan: needs stats_reset)
        p.update(frames=frames, settle_frames=0, stats_reset=True, wait_stats=[], wait_log_timeout=30)
    return p


def every(name, select, match, windows, **kw):
    return {"name": name, "select": select, "match": match, "windows": windows, "period_frames": PERIOD,
            "sample_frames": PERIOD, "min": 2, **kw}


def toggled(states, order, settings):
    """The startup state through the scene settle, every other state once (live_flow: a value measured later runs in
    an earlier phase), ending in the first window's state; then the windows (their length is set in retime)."""
    first = order[0][1]
    plan = [phase("warmup", 20, settings(states[0]))]
    plan += [phase(f"warm-{s}", 4, settings(s)) for s in states[1:] if s != first] + [phase(f"warm-{first}", 4, settings(first))]
    return plan + [phase(label, 12, settings(state), frames=FRAMES) for label, state in order]


def retime(names, here=HERE, measured=lambda p: FRAMES):
    """live_flow's readiness waits, then the windows' own length: SETTLE frames in the state, an acknowledged stats
    reset, FRAMES measured frames (opt_profile's rule; the tightening alone keeps one period). Other batches reuse it
    with their folder (`here`) and their own frames per window (`measured(phase)`; None keeps the phase as it is)."""
    HERE = here
    flow.tighten_files(HERE, job_names=names)
    lint_snapshot.load()   # the frozen FPS model per save and diagnostic state (the live corpus may be empty)
    for name in names:
        job, plan, plan_path = flow.load_job(HERE / "jobs" / f"{name}.json")
        for p, state in zip(plan, flow.phase_states(job, plan)):
            if p.get("frames") and measured(p):
                p.update(frames=SETTLE + measured(p), settle_frames=SETTLE, stats_reset=True)
                p["seconds"] = round((SETTLE + measured(p)) / flow.fps_windows.expected(job, state)["fps"] + .1, 3)
                p["motion_start"] = p["motion_end"] = p["seconds"]
            # A cold start may compile; a live toggle waits for a fresh quiet [watch] period (120 frames).
            if p.get("wait_log"):
                p["wait_log_timeout"] = 120 if p["label"] == "warmup" else 40
        job["estimated_phase_seconds"] = round(sum(p["seconds"] for p in plan), 3)
        flow.write(plan_path, plan)
        flow.write(HERE / "jobs" / f"{name}.json", job)
    # Append-only: readiness queries the frozen lint snapshot has not seen (unproven until a VALID run supplies them).
    added = lint_snapshot.extend_readiness(sorted((HERE / "jobs").glob("*.json")))
    # Likewise the expect and forbid patterns no frozen log was matched against (count 0, as 2460945e did by hand):
    # scripts/merge_scope.py's tooling check needs an entry for each.
    data = flow.read(lint_snapshot.PATH)
    new = sorted({rx for path in (HERE / "jobs").glob("*.json") for key in ("forbid", "expect")
                  for rx in flow.read(path).get(key, [])} - data["matches"].keys())
    if new:
        data["matches"].update({rx: [0, None] for rx in new})
        flow.write(lint_snapshot.PATH, data)
    return added + len(new)


def focused_job(name, save, start, plan, gates, expect, purpose, batch=BATCH, live_batch="perf-ab"):
    windows = [p["label"] for p in plan if p.get("frames")]
    return {
        "name": name, "save": save, "session": False, "traces": 0, "hold": 120, "timeout": 360, "max_minutes": 6,
        "repeat": 1, "live_batch": live_batch, "fail_fast": True, "stop_on_dump": False,
        "set": start,
        # Focused (no window_mode): the game takes the focus, so no unfocused 30 fps cap and [pace] / FG are valid.
        "args": ["-RealPresent", "-Foreground", "-Move", "-MovePlan", f"{batch}/plans/{name}.json"],
        "patterns": PATTERNS,
        "expect": ["\\[auto\\] scene ready", "\\[hot\\] core generation \\d+ loaded", "\\[cfg\\] settings read"] + expect,
        "expect_every": [every(n + " in every window", s, s, windows) for n, s in LINES] + gates,
        "forbid": FORBID,
        "expect_runner": [f"\\[motion-plan\\] complete phases={len(plan)}\\b", "^Run ended: done"],
        "live_flow": {"expected_fps": 60, "settings_latency_s": 1, "trace_stall_s": 0},
        "estimated_phase_seconds": sum(p["seconds"] for p in plan),
        "post": (f"python {batch}/analyze.py --job {batch}/jobs/{name}.json "
                 '--log "{log}" --runner "{runner}" --out "{log}.perf.md"'),
        "post_gate": True,
        "purpose": purpose + " STAGED, not authorized.",
    }


def hair_job(slug, save):
    plan = toggled(["on", "lod", "not", "groomrt", "off"], WINDOWS, hair)
    # Presence only (fail_fast is for "the measured lines exist"): the layer prints this line every period in both
    # states; whether strands were drawn is a row of the analyzer's table (grooms published, strand draws), not a gate:
    # a scene without converted grooms must still deliver its other windows.
    lines = (("hair period line", "\\[hair primary\\] frame \\d+: "), ("FG state line", "\\[sl\\] DLSS-G frame \\d+: "))
    gates = [every(n + " in every window", s, s, [w for w, _ in WINDOWS]) for n, s in lines]
    return plan, focused_job(
        f"perf-ab-{slug}", save, f"{USER},{HAIR_START},{hair('on')}", plan, gates, ["\\[ngx\\] RR create .*: Success"],
        f"User configuration at {save} (Ultra, FG 3x, uncapped, focused, static camera): stock cards / RTX Hair On / "
        "+ distant detail 1/4 / without shadow and AO transmittance / strands in lighting only / stock cards again; "
        "per window every GPU cost line, scene wait, rendered frame time, CPU submit and VRAM.")


def dlss5_job():
    def nr(state):
        return f"dlss5_nr_bypass={int(state == 'nr-off')}"
    order = (("nr-off-a", "nr-off"), ("nr-on", "nr-on"), ("nr-off-b", "nr-off"))
    plan = toggled(["nr-on", "nr-off"], order, nr)
    gates = [every("NR GPU ms in the on window", "\\[dlss5\\] gpu NR:", "gpu NR: [0-9.]+ ms/frame", ["nr-on"]),
             {"name": "no skipped or failed NR in the on window", "select": "\\[dlss5\\] NR (?:skipped|failed|disabled)",
              "match": "(?!)", "event": True, "min": 0, "windows": ["nr-on"]}]
    start = (f"{USER},experimental_options=1,rr=1,rr_post=1,rt_hair=0,rr_hair_strand_aa=0,dlss5_nr=1,"
             f"dlss5_nr_runtime={RUNTIME},dlss5_nr_bypass=1")
    plan, job = plan, focused_job(
        "perf-ab-dlss5-s8", S8, start, plan, gates,
        ["\\[ngx\\] RR create .*: Success", "\\[dlss5\\] NR created", "\\[dlss5\\] NR evaluate result=0x00000001"],
        "DLSS 5 NR cost at S8 (Ultra, FG 3x, uncapped, focused, stock cards): NR provisioned at start (dlss5_nr=1), the "
        "live switch dlss5_nr_bypass 1 / 0 / 1; [dlss5] gpu NR and rendered frame time. Needs the runtime staged by "
        "the launcher (README).")
    job["live_flow"]["startup_only"] = ["dlss5_nr", "dlss5_nr_runtime", "fg"]
    return plan, job


def wide_split_job(value):
    """The checked-in opt-profile-s8 job (the state run 131416 used) without its pans; one startup key differs."""
    source, plan, _ = flow.load_job(ROOT / "tools/live_batches/opt_profile/jobs/opt-profile-s8.json")
    job, name = deepcopy(source), f"perf-ab-wide-split-{value}"
    key = f"rt_capture_wide_split={value}"
    plan = [deepcopy(p) for p in plan if not p["label"].endswith("pan-q")]
    for p in plan:
        if p["label"].endswith("warmup"):
            p["set"] += "," + key
    windows = [p["label"] for p in plan if p.get("frames")]
    for g in job["expect_every"]:
        g["windows"] = [w for w in g["windows"] if w in windows]
    job["expect_every"] += [
        every("[bvh] hold pack and copy jobs in every window", "\\[bvh\\] last \\d+ builds", "hold pack \\d+ jobs", windows),
        every("VRAM and mesh pool in every window", "\\[mem\\] device-local VRAM", "\\[mem\\] device-local VRAM", windows)]
    for g in job["expect_every"][-2:]:
        g["sample_frames"] = PERIOD
    job.update(name=name, live_batch="perf-ab", set=job["set"] + "," + key, max_minutes=5,
               args=["-RealPresent", "-Move", "-MovePlan", f"{BATCH}/plans/{name}.json"],
               patterns=list(dict.fromkeys(job["patterns"] + ["\\[bvh\\] last", "\\[mem\\]", "\\[vram\\]", "\\[pace\\]",
                                                               "\\[rt\\] scene", "\\[geo\\] rt_capture_wide_split",
                                                               "\\[cascade\\] wide-stride"])),
               expect_runner=["\\[window-mode\\].*visible_unfocused", "^Run ended: done",
                              f"\\[motion-plan\\] complete phases={len(plan)}\\b"],
               estimated_phase_seconds=round(sum(p["seconds"] for p in plan), 3),
               post=(f"python {BATCH}/analyze.py --job {BATCH}/jobs/{name}.json "
                     '--log "{log}" --runner "{runner}" --out "{log}.perf.md"'),
               purpose=f"opt-profile-s8's Ultra and High static windows with rt_capture_wide_split={value} (startup "
                       "key): scene build GPU, [bvh] hold pack / copy jobs, mesh pool MB. Compare the two launches' "
                       "log medians. STAGED, not authorized.")
    # The startup line "[geo] rt_capture_wide_split on" proves the 1 launch; its absence in the 0 launch is a line of
    # the analyzer's report (a forbid would be refused by the lint once VALID logs with the default 1 exist).
    if value:
        job["expect"] = job["expect"] + ["\\[geo\\] rt_capture_wide_split on"]
    job["live_flow"]["startup_only"] = ["rt_capture_wide_split"]
    job["live_flow"]["segments"] = [dict(s, offset=2 * n, count=2, set=s["set"] + "," + key)
                                    for n, s in enumerate(job["live_flow"]["segments"])]
    job["live_flow"].pop("fps_windows", None)
    return plan, job


def main():
    assert (HERE / "predicted.json").exists(), "predicted.json precedes the jobs"
    made = [hair_job("s8", S8), hair_job("inn", INN), wide_split_job(0), wide_split_job(1), dlss5_job()]
    for plan, job in made:
        for folder, value in (("plans", plan), ("jobs", job)):
            (HERE / folder).mkdir(exist_ok=True)
            (HERE / folder / f"{job['name']}.json").write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8",
                                                               newline="\n")
    # The focused jobs take live_flow's timing (readiness waits, frame-window seconds); the wide-split jobs keep
    # opt_profile's own timing, which their source already carries.
    added = retime([job["name"] for _, job in made if "wide-split" not in job["name"]])
    print("Generated", ", ".join(job["name"] for _, job in made), f"- no queue staging; {added} new entries "
          "appended to lint_corpus_snapshot.json")


if __name__ == "__main__":
    main()
