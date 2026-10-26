"""Drives live setting changes during one auto-run (bg3rtx.ini is re-read on change).

    py -3 scripts/live_phases.py <phases.json>

phases.json: {"start": "<log regex that starts the clock>",
              "phases": [{"at": seconds, "set": {"key": "value", ...}, "name": "..."}, ...],
              "after_trace": [{"set": {...}, "name": "..."}, ...]}
`at` is relative to the first log line matching `start` (searched only in lines
written after this script starts). Each `after_trace` entry is applied when the
next "[trace] frame ... written" line appears, so consecutive auto traces can
capture an A/B of the same view. Every applied phase is logged to stdout and
to bg3rtx.log readers as a "[phase]" marker line in captures/phases.log.
"""
import json
import re
import sys
import time
from pathlib import Path

root = Path(__file__).resolve().parent.parent
ini = root / "captures" / "auto.ini"
log = root / "bg3rtx.log"
spec = json.loads(Path(sys.argv[1]).read_text())
marks = root / "captures" / "phases.log"


def apply(settings, name):
    lines = ini.read_text().splitlines()
    keys = set(settings)
    lines = [l for l in lines if l.split("=", 1)[0].strip() not in keys]
    lines += [f"{k}={v}" for k, v in settings.items()]
    ini.write_text("\n".join(lines) + "\n")
    msg = f"{time.strftime('%H:%M:%S')} [phase] {name}: {settings}"
    print(msg, flush=True)
    with marks.open("a") as f:
        f.write(msg + "\n")


t_launch = time.time()
start_re = re.compile(spec["start"])
pos = 0
t0 = None
phases = list(spec.get("phases", []))
after = list(spec.get("after_trace", []))
deadline = time.time() + spec.get("timeout", 600)
while time.time() < deadline and (phases or after):
    time.sleep(0.5)
    if not log.exists() or log.stat().st_mtime < t_launch:
        continue
    if log.stat().st_size < pos:   # the game recreated the log
        pos = 0
    with log.open("r", errors="replace") as f:
        f.seek(pos)
        new = f.read()
        pos = f.tell()
    for line in new.splitlines():
        if t0 is None and start_re.search(line):
            t0 = time.time()
            print(f"clock started: {line.strip()}", flush=True)
        if after and re.search(r"\[trace\] frame \d+ written", line):
            p = after.pop(0)
            apply(p["set"], p.get("name", "after trace"))
    while t0 is not None and phases and time.time() - t0 >= phases[0]["at"]:
        p = phases.pop(0)
        apply(p["set"], p.get("name", ""))
