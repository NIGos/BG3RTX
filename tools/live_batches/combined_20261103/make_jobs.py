"""Regenerate the explicitly requested next-series combinations. Never queues."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.live_batches.combine import main

# Keep the accepted source groups stable. Shorter sample windows change the
# estimate, not which independently qualified jobs share a launch.
PARTITIONS = {
    'combined-Campsite---27h-part1': ['cov_paks_compact/jobs/cp4-compact-camp-join.json', 'gpu_opt1/jobs/go1-camp.json'],
    'combined-Campsite---27h-part2': ['gpu_opt2/jobs/go2-camp-high.json'],
    'combined-Test-Dialogue3': ['post_temporal/jobs/pt-dialogue3.json', 'edge_flicker/jobs/ef1-dialogue3-attribution.json'],
    'combined-Water-Queens-House-part1': ['smoke_ultra/jobs/su1-ultra-s8-fg2.json', 'gpu_opt1/jobs/go2-s8.json'],
    'combined-Water-Queens-House-part2': ['gpu_opt2/jobs/go2-s8-medium.json', 'water_offscreen/jobs/wo1-s8-hit-shade.json', 'water_offscreen/jobs/wo1-s8-bit2.json'],
    'combined-Water-Queens-House-part3': ['water_offscreen/jobs/wo1-s8-bit4.json', 'water_flicker_wf9/jobs/wf9-s8-compose-shift.json', 'post_temporal/jobs/pt-s8.json'],
}

if __name__ == "__main__":
    base = ROOT / "tools/live_batches"
    failures = 0
    for name, sources in PARTITIONS.items():
        failures |= main([str(base / p) for p in sources] + ["--out", str(Path(__file__).resolve().parent), "--name", name])
    sys.exit(failures)
