"""r2_layer: vram_rightsize's tables plus the BLAS chunk rows of the '[mem] ours' line and the refusal events.

    py -3 tools/live_batches_staged/r2_layer/analyze.py --job <job.json> --log <game.log> --runner <runner.txt> --out <md>

Reads a finished run's log; no game or queue access.
"""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.live_batches_staged.perf_ab import analyze as base
from tools.live_batches_staged.vram_rightsize import analyze as vr

M = r"\[mem\] ours \([^)]*\): .*?"
base.ROWS += [
    ("BLAS chunks reserved (ours)", "MB", M + r"BLAS chunks (\d+) MB reserved"),
    ("BLAS chunks live", "MB", M + r"reserved in \d+ chunks \((\d+) live"),
    ("BLAS chunks pending", "MB", M + r"(\d+) pending, "),
    ("free in partly used chunks", "MB", M + r"(\d+) free in partly used chunks"),
    ("of it in chunks over 64 MB", "MB", M + r"\((\d+) in chunks over a base chunk\)"),
    ("chunks over 64 MB", "count", M + r"(\d+) over a\s+base chunk\)"),
]
vr.EVENTS += [("cascade casters refused", r"\[vram\] guard: refused BLAS chunks.*cascade casters"),
              ("refusal class lines", r"\[bvh\] BLAS refusals in"),
              ("casters left out (lines with left out > 0)", r"\[bvh\] BLAS refusals in .*left out [1-9]")]

if __name__ == "__main__":
    sys.exit(vr.main())
