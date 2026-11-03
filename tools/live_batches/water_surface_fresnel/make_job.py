"""Refresh the canonical Fresnel job with shared FPS/cadence bounds. Never queues."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from tools.live_batches.live_flow import tighten_files


if __name__ == "__main__":
    tighten_files(Path(__file__).resolve().parent)
