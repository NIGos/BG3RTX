"""Regenerate the explicitly requested next-series combinations. Never queues."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.live_batches.combine import main

SOURCES = [
    "smoke_ultra/jobs/su1-ultra-s8-fg2.json",
    "gpu_opt1/jobs/go2-s8.json",
    "gpu_opt2/jobs/go2-s8-medium.json",
    "water_offscreen/jobs/wo1-s8-hit-shade.json",
    "water_flicker_wf9/jobs/wf9-s8-compose-shift.json",
    "cov_paks_compact/jobs/cp4-compact-camp-join.json",
    "gpu_opt1/jobs/go1-camp.json",
    "gpu_opt2/jobs/go2-camp-high.json",
    "post_temporal/jobs/pt-dialogue3.json",
    "post_temporal/jobs/pt-s8.json",
    "edge_flicker/jobs/ef1-dialogue3-attribution.json",
]

if __name__ == "__main__":
    base = ROOT / "tools/live_batches"
    sys.exit(main([str(base / p) for p in SOURCES] + ["--out", str(Path(__file__).resolve().parent)]))
