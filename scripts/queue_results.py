"""Summaries of queued test runs, for agents and the coordinator (reads captures/queue).

    py -3 scripts/queue_results.py em12            # every run whose job name starts with em12
    py -3 scripts/queue_results.py em12 --lines    # also the matched log lines
    py -3 scripts/queue_results.py --since 18:00   # runs started at or after a time today

One line per run: job, run, outcome, validity, device-lost count, dumps, and the path of its
kept log slice (captures/queue/logs/), which holds the run's whole log.
"""
import json
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
jsonl = root / "captures" / "queue" / "results.jsonl"


def main():
    args = sys.argv[1:]
    lines = "--lines" in args
    since = None
    if "--since" in args:
        since = args[args.index("--since") + 1]
        args = [a for a in args if a not in ("--since", since)]
    prefixes = [a for a in args if not a.startswith("--")]
    if not jsonl.exists():
        sys.exit(f"{jsonl} not found")
    for raw in jsonl.read_text(encoding="utf-8", errors="replace").splitlines():
        try:
            r = json.loads(raw)
        except ValueError:
            continue
        if prefixes and not any(r.get("job", "").startswith(p) for p in prefixes):
            continue
        if since and r.get("start", "")[11:16] < since:
            continue
        valid = "" if r.get("valid", True) else " INVALID"
        dumps = f" dumps={','.join(r['dumps'])}" if r.get("dumps") else ""
        print(f"{r.get('start', '')[11:19]} {r['job']} #{r['run']}: {r.get('outcome')}{valid} "
              f"nv153={r.get('nv153')}{dumps} log={r.get('log')}")
        if lines:
            for l in r.get("lines") or []:
                print(f"    {l}")


if __name__ == "__main__":
    main()
