"""Per-phase D-LS timing and check lines: phase_times.py <bg3rtx.log> <phases_X.txt>"""
import re
import statistics
import sys
from datetime import datetime

log, ph = sys.argv[1], sys.argv[2]
phases = []
for l in open(ph, errors="replace"):
    m = re.match(r"(\d\d:\d\d:\d\d) \[phase\] ([^:]+):", l)
    if m:
        phases.append((datetime.strptime(m.group(1), "%H:%M:%S"), m.group(2)))
rows = {}
order = []
last = None
for l in open(log, errors="replace"):
    m = re.match(r"(\d\d:\d\d:\d\d)\.\d+\s+\[core\] (ls: local-light.*median ([\d.]+) ms.*join ([\d.]+)|ls check \(120 frames\): (\d+) px.*any bit on (\d+) px, by more than 1e-5 relative on (\d+), written by only one of them on (\d+)|ls check \(120 frames\): join: (\d+) slot.*, (\d+) of them)", l)
    if not m:
        continue
    t = datetime.strptime(m.group(1), "%H:%M:%S")
    cur = None
    for pt, name in phases:
        if pt <= t:
            cur = (pt, name)
    if cur is None:
        continue
    name = cur[1]
    if name not in rows:
        rows[name] = {"ms": [], "join": [], "chk": [], "jchk": [], "first": True}
        order.append(name)
    r = rows[name]
    if m.group(3):
        if r["first"]:   # the window that straddles the switch
            r["first"] = False
            continue
        r["ms"].append(float(m.group(3)))
        r["join"].append(float(m.group(4)))
    elif m.group(5):
        r["chk"].append((int(m.group(5)), int(m.group(6)), int(m.group(7)), int(m.group(8))))
    elif m.group(9):
        r["jchk"].append((int(m.group(9)), int(m.group(10))))
for n in order:
    r = rows[n]
    s = f"{n:32s}"
    if r["ms"]:
        s += f" ms median {statistics.median(r['ms']):.3f} ({' '.join(f'{x:.3f}' for x in r['ms'])}), join {statistics.median(r['join']):.3f}"
    if r["chk"]:
        c = r["chk"]
        s += f" | check windows {len(c)}: px {sum(x[0] for x in c)}, differ {[x[1] for x in c]}, >1e-5 {[x[2] for x in c]}, one-sided {sum(x[3] for x in c)}"
    if r["jchk"]:
        s += f" | join kept {sum(x[0] for x in r['jchk'])}, differ {sum(x[1] for x in r['jchk'])}"
    print(s)
