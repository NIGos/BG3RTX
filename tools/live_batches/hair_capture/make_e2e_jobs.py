"""Writes tools/live_batches/hair_capture/jobs/hcap2-*.json: capture -> conversion -> strands (READY_FOR_LIVE, not
queued; docs/RTX_HAIR_ROADMAP.md 2d).

Two launches of Test Dialogue1, run back to back, because the observable spans a game start: a style seen without a
groom is captured in launch 1 (rt_hair_capture is a startup key), converted between the launches by the launcher's
converter (bin/bg3rtx_hairconv, the post step: stage.py convert) into a scratch assetdb copy in the job's tree, and
must show strands from the first frame of launch 2 (the groom index is built at startup). One launch is not possible
without an in-run save reload (the plan runner has none; the layer's level-load re-scan is the in-game path and is
covered by the same groom index code). Post of launch 2 removes the copy (stage.py cleanup).
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


plan = [phase("warmup", 20, wait=["\\[auto\\] scene ready", "\\[cfg\\] settings read", PIPELINES_IDLE],
              fresh=[PIPELINES_IDLE], timeout=180),
        phase("observe", 12), phase("restore", 2)]
(B / "plans" / "e2e.json").write_text(json.dumps(plan, indent=1) + "\n", encoding="utf-8")

STRANDS = ("rt_hair_groom_primary=1,rt_hair_groom_primary_order=1,rt_hair_groom_primary_current=1,rt_hair_primary_frame_tag=1,"
           "rt_hair_groom_lod_keep=1,rt_hair_primary_hold=1,rt_hair_tangent_world=1,rt_hair_groom=1,rt_hair_groom_shadow_t=1,"
           "rt_hair_groom_ao_t=1,rt_hair_groom_cascade_drop=1,rt_hair_fibre=1,rt_hair_fibre_gi=1,rt_hair_fibre_spec=1,"
           "rt_hair_fibre_mapped=1,rr_hair_guides=1,rt_hair_physical_origin=1,rt_native_hair=1,rt_bvh_split=1")
BASE = ("preset=ultra,fg=2,headless=0,crash_dump=1,detailed_logging=1,rt_stats=0,rt_debug=0,gi_debug=0,ls_debug=0,rf_debug=0,"
        "ls_stats=0,rf_stats=0,ls_check=0,dump_images=0,dump_raw=0,dump_shaders=0,pan_capture=0,pan_inputs=0,pan_prepare=0,"
        "rt_primary_check=0,replay_pack=0,qa_exposure_hold=0,rt_scene_wait_log_frames=120," + STRANDS)
COMMON_FORBID = ["\\[crash\\] exception 0x", "VK_ERROR_DEVICE_LOST", "\\[hot\\] core (rejected|faulted)",
                 "FAILED, keeping previous", "compile call failed", "\\[hair groom\\] device WITHOUT"]
PATTERNS = ["\\[auto\\]", "\\[cfg\\]", "\\[hot\\]", "\\[watch\\]", "\\[crash\\]", "FAILED", "\\[hair capture\\]",
            "\\[hair groom\\]", "\\[hair primary\\]", "VK_ERROR", "validation", "stage:", "\\[hairconv\\]"]


def job(name, set_extra, expect, forbid, post):
    return {"name": name, "save": "Test Dialogue1", "session": False, "traces": 0, "repeat": 1, "hold": 120,
            "timeout": 480, "max_minutes": 8, "window_mode": "visible_unfocused_fg", "live_batch": "hair-capture-e2e",
            "tree": ".claude/worktrees/test-main", "fail_fast": True, "stop_on_dump": False, "set": BASE + set_extra,
            "args": ["-RealPresent", "-Move", "-MovePlan", "tools/live_batches/hair_capture/plans/e2e.json"],
            "patterns": PATTERNS, "expect": expect, "forbid": COMMON_FORBID + forbid, "post": post, "post_gate": True}


capture = job("hcap2-capture", ",rt_hair_capture=1", [
    "\\[auto\\] scene ready",
    "\\[hair groom\\] device with linear swept spheres",
    "\\[hair capture\\] [0-9a-f_A-Zcount]+: \\d+ indices, \\d+ vertices, mask \\d+x\\d+ fmt \\d+ mips \\d+, stretch [0-9.]+/[0-9.]+/[0-9.]+, \\d+ KB, written",
    "\\[hair capture\\] \\d+ frames: [1-9]\\d* captured",
], ["\\[hair capture\\] .* capturing stopped"], "python tools/live_batches/hair_capture/stage.py convert")
strands = job("hcap2-strands", ",rt_hair_capture=0", [
    "\\[auto\\] scene ready",
    "\\[hair groom\\] indexed .*\\.hgrm: index id [0-9a-f]{16}",
    "\\[hair groom\\] [0-9a-f]{16} LOD \\d decoded \\(v3\\): \\d+ points in \\d+ ms",
    "\\[hair groom\\] frame \\d+: [1-9]\\d* groom instances",
    "\\[hair primary\\] strand G-buffer draws GPU",
], ["\\[hair groom\\] .*file unreadable or changed since startup"], "python tools/live_batches/hair_capture/stage.py cleanup")
for j in (capture, strands):
    (B / "jobs" / f"{j['name']}.json").write_text(json.dumps(j, indent=1) + "\n", encoding="utf-8")
pred = json.loads((B / "predicted.json").read_text(encoding="utf-8")) if (B / "predicted.json").exists() else {}
pred["hcap2-capture"] = {"captures": {"predicted": ">= 1 written, mask 2048x2048 BC3 (fmt 137), 12 mips, stretch near 1",
                                      "source": "hcap1-dialogue: 2 full masks, 424-600 unmatched count-unknown draws", "written": "2026-11-08"}}
pred["hcap2-strands"] = {"strands": {"predicted": "the converted style's groom indexed and drawn (groom instances >= 1)",
                                     "source": "offline: hairconv grooms decode in the layer reader (hairconv_test)", "written": "2026-11-08"}}
(B / "predicted.json").write_text(json.dumps(pred, indent=1) + "\n", encoding="utf-8")
print("wrote hcap2-capture and hcap2-strands (READY_FOR_LIVE, not queued; run in this order)")
sys.exit(0)
