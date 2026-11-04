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

# Retained fixture: rebuild from current sources so the cross-job reset gets
# the shared compositor's complete initial state and pipeline readiness waits.
FIXTURES = {"combined-Water-Queens-House-part3": [
    "water_flicker_wf9/jobs/wf9-s8-compose-shift.json",
    "post_temporal/jobs/pt-s8.json",
]}


if __name__ == "__main__":
    base = ROOT / "tools/live_batches"
    failures = 0
    for name, sources in (PARTITIONS | FIXTURES).items():
        failures |= main([str(base / p) for p in sources] + ["--out", str(Path(__file__).resolve().parent), "--name", name])
    sys.exit(failures)
