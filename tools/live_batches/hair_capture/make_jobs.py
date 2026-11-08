"""Writes tools/live_batches/hair_capture/{plans,jobs}/*.json: READY_FOR_LIVE, not queued (docs/RTX_HAIR_ROADMAP.md 2c).

One Test Dialogue1 launch (party close-ups, companions' hair at LOD0) with native hair capture and the strand grooms on.
Observables the runtime capture source needs and offline data cannot answer:
- the CHAR_Hair mask bound at draw time: format, size, mip count and layout ([hair capture] lines, layer/alphatest.cpp
  hair_mask_probe): is mip 0 resident at full size (compare with the pak DDS of that style) and copyable (usage);
- the capture stride 88 (soup with UV0 at +16 and post-wind world positions);
- how many hair draws have a host-mapped index buffer (the [hair groom] stats line: "not mapped" vs matched): the
  capture copies the index range by GPU when it is not.
No new key: rt_native_hair (startup) and rt_hair_groom are existing keys, both restored by the queue after the run.
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
B = ROOT / "tools" / "live_batches" / "hair_capture"
(B / "plans").mkdir(parents=True, exist_ok=True)
(B / "jobs").mkdir(parents=True, exist_ok=True)

PIPELINES_IDLE = "\\[watch\\].*\\| pipelines 0 \\(0 ms in driver\\)"


def phase(label, seconds, wait=None, fresh=None, timeout=None):
    p = {"label": label, "kind": "static", "speed": "static", "seconds": seconds, "repeat_ms": 0, "scan": 16,
         "capture_at": 0, "serial": 0, "motion_start": seconds, "motion_end": seconds, "pixels_per_second": 0,
         "direction": 0, "set": "rt_stats=0,qa_exposure_hold=0"}
    if wait:
        p["wait_log"] = wait
        p["wait_log_timeout"] = timeout or 120
    if fresh:
        p["wait_log_fresh"] = fresh
    return p


plan = [
    phase("warmup", 20, wait=["\\[auto\\] scene ready", "\\[cfg\\] settings read", PIPELINES_IDLE], fresh=[PIPELINES_IDLE],
          timeout=180),
    # Two 300-frame groom stats periods at ~60 fps: the "not mapped" counters of the party's hair draws.
    phase("observe", 12),
    phase("restore", 2),
]
(B / "plans" / "dialogue.json").write_text(json.dumps(plan, indent=1) + "\n", encoding="utf-8")

job = {
    "name": "hcap1-dialogue",
    "save": "Test Dialogue1",
    "session": False,
    "traces": 0,
    "repeat": 1,
    "hold": 120,
    "timeout": 480,
    "max_minutes": 8,
    "window_mode": "visible_unfocused_fg",
    "live_batch": "hair-capture",
    "tree": ".claude/worktrees/test-main",
    "fail_fast": True,
    "stop_on_dump": False,
    "set": "preset=ultra,fg=2,headless=0,crash_dump=1,detailed_logging=1,rt_stats=0,rt_debug=0,gi_debug=0,ls_debug=0,"
           "rf_debug=0,ls_stats=0,rf_stats=0,ls_check=0,dump_images=0,dump_raw=0,dump_shaders=0,pan_capture=0,"
           "pan_inputs=0,pan_prepare=0,rt_primary_check=0,replay_pack=0,qa_exposure_hold=0,rt_native_hair=1,"
           "rt_bvh_split=1,rt_hair_groom=1,rt_scene_wait_log_frames=120",
    "args": ["-RealPresent", "-Move", "-MovePlan", "tools/live_batches/hair_capture/plans/dialogue.json"],
    "patterns": ["\\[auto\\]", "\\[cfg\\]", "\\[hot\\]", "\\[watch\\]", "\\[crash\\]", "FAILED", "\\[geo\\] transform feedback",
                 "\\[hair capture\\]", "\\[hair groom\\]", "\\[native hair\\]", "VK_ERROR", "validation"],
    "expect": [
        "\\[auto\\] scene ready",
        "\\[geo\\] transform feedback enabled: .* \\(stride 88\\)",
        "\\[hair capture\\] fs [0-9a-f]{16} mask image \\d+: format \\d+, \\d+x\\d+, \\d+ mips \\(view base \\d+, \\d+ levels\\), layout \\d+, usage 0x[0-9a-f]+",
        "\\[hair groom\\] device with linear swept spheres",
        "\\[hair groom\\] frame \\d+: \\d+ groom instances; last \\d+ frames: .* unmatched hair draws \\d+ \\(not indexed \\d+, count unknown \\d+, not mapped \\d+, no file \\d+",
    ],
    "forbid": [
        "\\[crash\\] exception 0x",
        "VK_ERROR_DEVICE_LOST",
        "\\[hot\\] core (rejected|faulted)",
        "FAILED, keeping previous",
        "compile call failed",
        "\\[geo\\] rt_native_hair rejected",
        "\\[hair groom\\] device WITHOUT",
    ],
}
(B / "jobs" / "hcap1-dialogue.json").write_text(json.dumps(job, indent=1) + "\n", encoding="utf-8")
predicted = {
    "_rule": "Written and committed BEFORE any run.",
    "hcap1-dialogue": {
        "mask_mip0": {"predicted": "full size and all mips resident in a dialogue close-up (BC7 or BC3)",
                      "source": "NATIVE_HAIR.md: mask is a plain 2D view (set 1 binding 4), not VT", "written": "2026-11-08"},
        "index_buffer": {"predicted": "not mapped > 0 (the game's hair index buffers are device-local)",
                         "source": "hair_groom UV join exists for this case (RTX_HAIR_PRIMARY)", "written": "2026-11-08"},
    },
}
(B / "predicted.json").write_text(json.dumps(predicted, indent=1) + "\n", encoding="utf-8")
print("wrote", B / "jobs" / "hcap1-dialogue.json", "(READY_FOR_LIVE, not queued)")
sys.exit(0)
