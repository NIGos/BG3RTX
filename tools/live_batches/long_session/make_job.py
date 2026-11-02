"""Writes the long_session job and plan (README.md). Rerun after editing CYCLES or the gates:

    py -3 tools/live_batches/long_session/make_job.py
"""
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
CYCLES = 22            # 22 x 110 s + 20 s warmup = 41 min of plan
BASE, TAIL = (2, 3, 4), (CYCLES - 2, CYCLES - 1, CYCLES)   # cycle 1 fills the caches; compare 2-4 with the last 3
# Process VRAM also holds the BLAS chunks' high water (64 MB chunks, never freed): the pan / rotate windows' transient
# BLAS (AS 0.25 GB static, 0.8-1.4 GB moving) raised it from 58 to 66 chunks until cycle 14 of ls1 2026-11-02 0845,
# then flat for 8 cycles. A leak keeps growing in the second half: VRAM compares the middle cycles with the last 3.
MID = (CYCLES // 2 + 1, CYCLES // 2 + 2, CYCLES // 2 + 3)
OFF = "rt_debug=0,rt_stats=0,qa_exposure_hold=0"
DIAG = "rt_debug=0,rt_stats=1,qa_exposure_hold=0"


def step(label, kind, speed, seconds, direction=0, pps=0, set_=OFF, repeat_ms=0):
    moving = kind != "static"
    return {"label": label, "kind": kind, "speed": speed, "seconds": seconds, "repeat_ms": repeat_ms, "scan": 16,
            "capture_at": 0, "serial": 0, "motion_start": 1 if moving else seconds,
            "motion_end": seconds - 1 if moving else seconds, "pixels_per_second": pps, "direction": direction, "set": set_}


plan = [step("warmup", "static", "static", 20)]
cost, diag = {}, {}
for c in range(1, CYCLES + 1):
    p = f"c{c:02d}"
    cyc = [step(f"cost-{p}-static", "static", "static", 20),
           step(f"cost-{p}-pan-q", "pan", "fast", 15, -1, repeat_ms=33),
           step(f"cost-{p}-pan-e", "pan", "fast", 15, 1, repeat_ms=33),
           step(f"cost-{p}-rot-r", "rotate", "slow", 20, 1, 60),
           step(f"cost-{p}-rot-l", "rotate", "slow", 20, -1, 60),
           step(f"diag-{p}-pool", "static", "static", 20, set_=DIAG)]   # >= 120 frames at ~10 fps with rt_stats
    plan += cyc
    cost[c] = [s["label"] for s in cyc[:5]]
    diag[c] = [cyc[5]["label"]]
all_cost = [l for c in cost.values() for l in c]


def side(d, cycles):
    return [l for c in cycles for l in d[c]]


def trend(name, select, value, abs_, src=cost, base=BASE):
    return {"name": f"trend: {name} does not grow from cycles {base[0]}-{base[-1]} to {TAIL[0]}-{TAIL[-1]}", "trend": True,
            "select": select, "value": value, "baseline": side(src, base), "tail": side(src, TAIL), "abs": abs_, "min": 3}


expect_every = [
    {"name": "scene wait: every cost window traces its own frame's scene (0 timeouts)",
     "select": r"\[rt\] scene wait: \d+ frames", "match": r"got this frame's scene, 0 timeouts", "min": 1, "windows": all_cost},
    {"name": "capacity: the RT capture drops no opaque draw (buffer full 0.00) in every cost window",
     "select": r"\[geo\] RT capture, last \d+ frames:", "match": r"opaque draws dropped \(buffer full\) 0\.00,", "min": 1,
     "windows": all_cost},
    {"name": "capacity: the cascade casters buffer is never full (buffer full 0.0) in every cost window",
     "select": r"\[cascade\] casters: cascade \d+, \d+ frames;", "match": r", buffer full 0\.0, added ", "min": 1,
     "windows": all_cost, "when_present": True},
    # Print precision is the allowance (%.0f MB, %.1f MB, %.2f ms); the noise is the baseline's own spread.
    trend("process VRAM (device-local heaps)", r"\[mem\] device-local VRAM of this process:", r"used ([0-9.]+) MB", 1, base=MID),
    trend("mesh pool use", r"\[bvh\] mesh cache:", r"pool ([0-9.]+) / ", 0.1),
    trend("acceleration structure memory", r"\[bvh\] mesh cache:", r"AS ([0-9.]+) MB", 0.1),
    trend("rendered frame time (median)", r"\[pace\] rendered frame time over 600 frames:", r"median ([0-9.]+) ms", 0.01),
    trend("rendered frame time (p99)", r"\[pace\] rendered frame time over 600 frames:", r"p99 ([0-9.]+) ms", 0.01),
    trend("hit-material pool records", r"\[hit material pool\] frame \d+ pool draws", r"records (\d+) stored", 0, diag),
]
forbid = [
    r"\[crash\] exception 0x", "VK_ERROR_DEVICE_LOST", r"\[hot\] core (rejected|faulted)", "FAILED, keeping previous",
    "compile call failed", "RR creation failed", "RR evaluate failed", r"\[cascade\] could not allocate",
    "skipped: no variant for", r"sun: WARNING \d+ roof volumes within", "over kSunMaxVolumes on [1-9]",
    r"set 1 [1-9]\d* out-of-range reads skipped", r"\[hit material pool basis\] over kBasisCapBytes",
    r"\[bvh\] mesh pool full at", r"\[log\] reached \d+ MB", r"\[water\] compose UNAVAILABLE", r"\[sl\] .*eWarnOutOfVRAM",
]
plan_s = sum(s["seconds"] for s in plan)
job = {
    "name": "ls1-long-session-camp-fg2",
    "save": "Campsite - 27h",
    "session": False, "traces": 0, "hold": 120, "timeout": int(plan_s + 420), "repeat": 1,
    "live_batch": "long-session",
    "set": ("preset=ultra,fg=2,headless=0,fps_cap=0,rt=1,rr=1,reflex=1,crash_dump=1,rt_timing_frames=120,"
            "rt_scene_wait_log_frames=120,rt_scene_wait=1,rt_debug=0,ls_debug=0,gi_debug=0,rf_debug=0,rr_show=0,rf_stats=0,"
            "ls_stats=0,ls_check=0,dump_images=0,dump_raw=0,dump_shaders=0,pan_capture=0,pan_inputs=0,pan_prepare=0,"
            "rt_order_trace=0,rt_stability_log=0,rt_stats=0,rt_primary_check=0,rt_primary_epoch=0,fg_state_every=60,"
            "ls_box=0,ls_k=0,rt_hit_reference=0,sharc=0,qa_exposure_hold=0"),
    "args": ["-RealPresent", "-Foreground", "-Move", "-MovePlan", "captures/live_batches/long_session/plans/ls1_camp.json"],
    "patterns": [r"\[hot\]", r"\[ngx\] RR create", r"\[sl\] DLSS-G frame", r"\[pace\]", r"\[mem\]", r"\[watch\]",
                 r"rt passes: GPU cost", "gi: GPU cost", r"\[rt\] scene wait", r"\[geo\] RT capture", r"\[cascade\] casters",
                 r"\[bvh\] mesh cache", r"\[hit material pool\] frame", r"\[water\]", r"\[crash\]", "FAILED", r"\[log\]",
                 r"\[core\] guard:"],
    "expect": [
        r"\[auto\] scene ready", r"\[hot\] core generation \d+ loaded", r"\[ngx\] RR create .*: Success",
        r"\[sl\] DLSS-G frame \d+: status 0x0, presented per app frame 2",
        r"\[cfg\] settings read",
        r"\[pace\] rendered frame time over 600 frames: median [0-9.]+ ms",
        r"\[mem\] device-local VRAM of this process: used [1-9]\d* MB, budget [1-9]\d* MB",
        r"\[bvh\] mesh cache: \d+ meshes",
        r"\[hit material pool\] frame \d+ pool draws \d+",
        r"\[rt\] scene wait: 120 frames, .*got this frame's scene, 0 timeouts",
    ],
    "expect_every": expect_every,
    "forbid": forbid,
    "expect_runner": [r"\[motion-plan\] pointer centred in owned foreground game",
                      rf"\[motion-plan\] complete phases={len(plan)}\b", "^Run ended: done"],
    "stop_on_dump": True,
    "purpose": (f"Long session (stage 6): Ultra + FG 2 at the camp for {plan_s / 60:.0f} min in one launch, {CYCLES} cycles of "
                "static / pan Q / pan E / rotate R / rotate L cost windows and a 20 s rt_stats pool window. Gates: scene wait 0 "
                "timeouts and capacity 0 drops in every cost window; trend gates (queue_gates.py) that VRAM, mesh pool, AS memory, "
                f"frame time median/p99 and pool records of cycles {TAIL[0]}-{TAIL[-1]} stay within cycles {BASE[0]}-{BASE[-1]} "
                "plus their own spread; no log rotation, no device lost, no pool-full lines."),
    "estimated_phase_seconds": plan_s,
}
(HERE / "plans").mkdir(exist_ok=True)
(HERE / "jobs").mkdir(exist_ok=True)
(HERE / "plans" / "ls1_camp.json").write_text(json.dumps(plan, indent=1) + "\n", encoding="utf-8")
(HERE / "jobs" / "ls1-long-session-camp-fg2.json").write_text(json.dumps(job, indent=1) + "\n", encoding="utf-8")
print(f"{len(plan)} phases, {plan_s} s ({plan_s / 60:.1f} min), {len(expect_every)} expect_every entries")
