"""Drives live setting changes during one auto-run (bg3rtx.ini is re-read on change).

    py -3 scripts/live_phases.py <phases.json> [--offset <bytes>]

phases.json: {"start": "<log regex that starts the clock>", "tag": "<auto-run -Tag, optional>",
              "phases": [{"at": seconds, "set": {"key": "value", ...}, "name": "..."}, ...],
              "after_trace": [{"set": {...}, "name": "..."}, ...]}
`at` is relative to the first log line matching `start` (searched only in lines
written after this script starts). Each `after_trace` entry is applied when the
next "[trace] frame ... written" line appears, so consecutive auto traces can
capture an A/B of the same view. Every applied phase is logged to stdout and
to bg3rtx.log readers as a "[phase]" marker line in captures/phases.log.

--offset: read the log from this byte offset (a test-queue.ps1 session job: the game
is already running, and only the lines written during this job count).
"""
import json
import os
import re
import sys
import time
from pathlib import Path

root = Path(__file__).resolve().parent.parent
spec = json.loads(Path(sys.argv[1]).read_text())
offset = int(sys.argv[sys.argv.index("--offset") + 1]) if "--offset" in sys.argv else None
# "tag": the auto-run.ps1 -Tag of the run this drives (two runs can share a checkout).
tag = "." + spec["tag"] if spec.get("tag") else ""
ini = root / "captures" / f"auto{tag}.ini"
log = root / f"bg3rtx{tag}.log"
marks = root / "captures" / f"phases{tag}.log"
# Inherited from the queue, including nested QA commands. One immutable per-run
# request remains present through owned-game cleanup and later scene attempts.
quit_file = Path(os.environ["BG3RTX_QUEUE_QUIT_FILE"]) if os.environ.get("BG3RTX_QUEUE_QUIT_FILE") else None


def cancelled():
    return quit_file is not None and quit_file.exists()



def apply(settings, name):
    if cancelled():
        return
    lines = ini.read_text().splitlines()
    keys = set(settings)
    lines = [l for l in lines if l.split("=", 1)[0].strip() not in keys]
    lines += [f"{k}={v}" for k, v in settings.items()]
    if cancelled():
        return
    ini.write_text("\n".join(lines) + "\n")
    msg = f"{time.strftime('%H:%M:%S')} [phase] {name}: {settings}"
    print(msg, flush=True)
    with marks.open("a") as f:
        f.write(msg + "\n")


t_launch = time.time()
start_re = re.compile(spec["start"])
pos = offset or 0
t0 = None
phases = list(spec.get("phases", []))
after = list(spec.get("after_trace", []))
deadline = time.time() + spec.get("timeout", 600)
while time.time() < deadline and (phases or after) and not cancelled():
    time.sleep(0.5)
    if cancelled():
        break
    if not log.exists() or (offset is None and log.stat().st_mtime < t_launch):
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
