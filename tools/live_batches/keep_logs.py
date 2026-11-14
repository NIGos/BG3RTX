"""Prune captures/queue/logs without emptying the lint evidence.

Keeps the newest 3 VALID logs per save (the set live_flow.corpus() reads) with every
sibling file sharing the log's name or stem, and deletes the other files older than N days.
Usage: py -3 tools/live_batches/keep_logs.py <logs_dir> [--results results.jsonl] [--days 3]
"""
import argparse
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.live_batches import live_flow as flow


def keep_set(results, logs_dir, newest=3, inventory=None):
    """Names (in logs_dir) of the evidence logs and their siblings."""
    logs_dir = Path(logs_dir)
    with tempfile.TemporaryDirectory() as tmp:
        index = flow.corpus(results, Path(tmp) / "c.json", newest, inventory)
    stems = set()
    for entry in index["entries"]:
        p = Path(entry["path"])
        if p.parent.resolve() == logs_dir.resolve():
            stems.add((p.name, p.stem))
    keep = set()
    for f in logs_dir.iterdir():
        if f.is_file() and any(f.name.startswith(name) or f.name.startswith(stem + ".") for name, stem in stems):
            keep.add(f.name)
    return keep


def prune(logs_dir, results, days=3, newest=3, inventory=None, now=None):
    logs_dir = Path(logs_dir)
    keep = keep_set(results, logs_dir, newest, inventory) if Path(results).exists() else set()
    cutoff = (now or time.time()) - days * 86400
    removed = []
    for f in logs_dir.iterdir():
        if f.is_file() and f.name not in keep and f.stat().st_mtime < cutoff:
            try:
                f.unlink()
                removed.append(f.name)
            except OSError:
                pass
    return removed, keep


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("logs")
    ap.add_argument("--results", default=str(flow.default_results()))
    ap.add_argument("--days", type=float, default=3)
    a = ap.parse_args(argv)
    removed, keep = prune(a.logs, a.results, a.days)
    print(f"logs pruned: {len(removed)} removed, {len(keep)} kept as lint evidence")
    return 0


if __name__ == "__main__":
    sys.exit(main())
