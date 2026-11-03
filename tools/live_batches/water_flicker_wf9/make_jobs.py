"""Refresh duration bounds on canonical prepared jobs. Never queues or launches."""
from pathlib import Path
from tools.live_batches.live_flow import tighten_files

if __name__ == "__main__":
    tighten_files(Path(__file__).resolve().parent)
