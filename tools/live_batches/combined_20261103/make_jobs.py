"""Regenerate the explicitly requested next-series combinations. Never queues."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.live_batches.combine import main

# Keep the accepted source groups stable. Shorter sample windows change the
# estimate, not which independently qualified jobs share a launch.
# Retired scheduling: rejected GPU, pak and water-hit candidates are historical fixtures.
# Keep generated jobs/manifests because the frozen lint snapshot and tests read them.
PARTITIONS = {}


if __name__ == "__main__":
    base = ROOT / "tools/live_batches"
    failures = 0
    for name, sources in PARTITIONS.items():
        failures |= main([str(base / p) for p in sources] + ["--out", str(Path(__file__).resolve().parent), "--name", name])
    sys.exit(failures)
