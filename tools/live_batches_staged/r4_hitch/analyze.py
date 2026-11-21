"""r4_hitch: vram_rightsize's tables plus the groom decode events (in the build or on the worker) per window.

    py -3 tools/live_batches_staged/r4_hitch/analyze.py --job <job.json> --log <game.log> --runner <runner.txt> --out <md>

Reads a finished run's log; no game or queue access.
"""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.live_batches_staged.vram_rightsize import analyze as vr

vr.EVENTS += [("groom decodes in the build", r"\[hair groom\] [0-9a-f]{16} LOD \d+ decoded \(v3\)"),
              ("groom decodes on the worker", r"\[hair groom\] [0-9a-f]{16} LOD \d+ decoded \(v3, worker\)"),
              ("Streamline frame over 100 ms", r"\[sl\] warn .*Frame rate over 100")]

if __name__ == "__main__":
    sys.exit(vr.main())
