"""One table per perf_ab job: per measured window the median of every cost line (range and period count beside it).

    py -3 tools/live_batches_staged/perf_ab/analyze.py --job <job.json> --log <game.log> --runner <runner.txt> --out <md>

Reads a finished run's log; no game or queue access. Windows are the plan's frame windows, lines are the complete
120-frame periods after each window's acknowledged stats reset (scripts/queue_gates.py, as release_qual/analyze_final).
"""
import argparse
from pathlib import Path
import re
import statistics
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts.queue_gates import stamped_lines, windows, complete_period_lines
from tools.live_batches import live_flow as flow
from tools.live_batches.release_qual.analyze_final import COSTS
from tools.live_batches.opt_profile.analyze import EXTRA

# (row, unit, regex with one number). The GPU pass timers come from the shared tables (COSTS, EXTRA).
ROWS = [(name, "ms/period" if name in ("CPU submit", "driver") else "ms", rx) for name, rx in {**COSTS, **EXTRA}.items()]
ROWS += [
    ("scene build convert+extract", "ms", r"scene build GPU .*median convert\+extract ([\d.]+) ms"),
    ("scene build BLAS", "ms", r"scene build GPU .*BLAS builds/refits ([\d.]+) ms"),
    ("scene build TLAS", "ms", r"scene build GPU .*TLAS ([\d.]+) ms"),
    ("strand G-buffer draws", "ms", r"\[hair primary\] strand G-buffer draws GPU ([\d.]+) ms/frame"),
    ("DLSS 5 NR", "ms", r"\[dlss5\] gpu NR: ([\d.]+) ms/frame"),
    ("scene wait mean", "ms", r"\[rt\] scene wait:.*wait mean ([\d.]+) ms"),
    ("scene wait max", "ms", r"\[rt\] scene wait:.*max ([\d.]+) ms"),
    ("scene wait timeouts", "per period", r"\[rt\] scene wait:.* (\d+) timeouts"),
    ("frame time median", "ms", r"\[pace\] rendered frame time over \d+ frames: median ([\d.]+) ms"),
    ("frame time p99", "ms", r"\[pace\] rendered frame time .*p99 ([\d.]+) ms"),
    ("FG displayed/rendered", "ratio", r"DLSS-G frame \d+:.*displayed/rendered over the last \d+ frames ([\d.]+)"),
    ("draws", "per period", r"\[watch\].*\| draws (\d+)"),
    ("CPU capture hooks", "ms/frame", r"\[cpu\] capture hooks over \d+ frames.*total ([\d.]+);"),
    ("CPU dispatch tracking pre", "ms/frame", r"\[cpu\] capture detail .*dispatch tracking pre ([\d.]+)"),
    ("CPU bvh_build", "ms/build", r"\[cpu\] scene build recording.*bvh_build ([\d.]+)"),
    ("hold pack jobs", "per period", r"\[bvh\] last \d+ builds.*hold pack (\d+) jobs"),
    ("hold copy jobs", "per period", r"\[bvh\] last \d+ builds.*, copy (\d+) jobs"),
    ("hold copy bytes", "bytes/period", r"\[bvh\] last \d+ builds.*, copy \d+ jobs \((\d+) bytes\)"),
    ("groom instances", "count", r"\[hair groom\] frame \d+: (\d+) groom instances"),
    ("grooms published", "count", r"\[hair primary\] frame \d+: (?:RTX Hair off \(live\): )?(\d+) grooms published"),
    ("G-buffer strand draws", "per period", r"\[hair primary\] frame \d+: .*?(\d+) G-buffer and \d+ velocity strand draws"),
    ("FG presented per app frame", "count", r"DLSS-G frame \d+:.*presented per app frame (\d+)"),
    ("VRAM used", "MB", r"\[mem\] device-local VRAM of this process: used (\d+) MB"),
    ("VRAM budget", "MB", r"\[mem\] device-local VRAM of this process: used \d+ MB, budget (\d+) MB"),
    ("mesh pool", "MB", r"\[mem\] ours.*mesh pool (\d+) MB"),
    ("mesh pool used", "MB", r"\[mem\] ours.*mesh pool \d+ MB \((\d+) used"),
]
FIBRE = r"\[hair fibre\] (.+?) GPU median ([\d.]+) ms"
REQUIRED = ("RT", "GI", "scene build", "scene wait mean", "frame time median", "CPU submit", "VRAM used")
WHOLE = ("MB", "count", "per period", "bytes/period")


def measure(log, runner, plan):
    lines = stamped_lines(Path(log).read_text(encoding="utf-8-sig", errors="replace"))
    run = Path(runner).read_text(encoding="utf-8-sig", errors="replace")
    out, guard = {}, {}
    for w in windows(run, plan, lines):
        if not (0 <= w["index"] < len(plan)) or not plan[w["index"]].get("frames"):
            continue
        inside = [(t, s) for t, s in lines if w["applied"] <= t < w["end"]]
        row = {}
        for s in (complete_period_lines(inside, w) if w["ready"] else []):
            hits = [(name, m[1]) for name, _, rx in ROWS if (m := re.search(rx, s))]
            if (m := re.search(FIBRE, s)):
                hits.append(("hair fibre " + m[1], m[2]))
            for name, value in hits:
                row.setdefault(name, []).append(float(value))
        out[w["label"]] = row
        # The guard state and every [vram] guard line of the window (settling included): suspect 1 is an event.
        states = sorted({m[1] for _, s in inside if (m := re.search(r"\[mem\] ours.*; guard (\w+)", s))})
        guard[w["label"]] = (states, [s.strip() for _, s in inside if "[vram] guard" in s])
    return (out, guard, [s.strip() for _, s in lines if "[vram] guard" in s],
            sum("[geo] rt_capture_wide_split on" in s for _, s in lines))


def cell(values, digits):
    if not values:
        return "-"
    return f"{statistics.median(values):.{digits}f} [{min(values):.{digits}f}-{max(values):.{digits}f}] n{len(values)}"


def report(job, plan, measured, guard, guard_all, wide_split):
    labels = [p["label"] for p in plan if p.get("frames")]
    fibre = sorted({n for row in measured.values() for n in row if n.startswith("hair fibre ")})
    names = [(n, u) for n, u, _ in ROWS] + [(n, "ms") for n in fibre]
    errors = [f"{label}: no acknowledged window" for label in labels if label not in measured]
    errors += [f"{label}: {name} has {len(measured[label].get(name, []))} complete periods (< 2)"
               for label in labels if label in measured for name in REQUIRED if len(measured[label].get(name, [])) < 2]
    noise = "off-a" in labels and "off-b" in labels
    unfocused = str(job.get("window_mode", "")).startswith("visible_unfocused")
    text = [f"# {job['name']} ({job['save']})", "",
            "Median [min-max] nPeriods of the complete 120-frame periods per window" +
            (" (unfocused: frame time is the 30 fps cap, not a cost)." if unfocused else " (focused, no frame cap)."), "",
            "| Line | Unit | " + " | ".join(labels) + (" | off-b - off-a (noise) |" if noise else " |"),
            "| --- | --- | " + " | ".join("---:" for _ in labels) + (" | ---: |" if noise else " |")]
    for name, unit in names:
        rows = [measured.get(label, {}).get(name, []) for label in labels]
        if not any(rows):
            continue
        digits = 0 if unit in WHOLE else 3
        line = f"| {name} | {unit} | " + " | ".join(cell(v, digits) for v in rows)
        if noise:
            a, b = measured.get("off-a", {}).get(name), measured.get("off-b", {}).get(name)
            line += f" | {statistics.median(b) - statistics.median(a):+.{digits}f}" if a and b else " | -"
        text.append(line + " |")
    text += ["", "| Window | [mem] guard state | [vram] guard lines |", "| --- | --- | ---: |"]
    text += [f"| {label} | {', '.join(guard[label][0]) or '-'} | {len(guard[label][1])} |" for label in labels if label in guard]
    text += ["", f"Startup line '[geo] rt_capture_wide_split on': {wide_split} (0: the split is off)",
             "", f"[vram] guard lines in the whole run: {len(guard_all)}"] + ["- `" + s[:300] + "`" for s in guard_all[:20]]
    return "\n".join(text) + "\n", errors


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--job", required=True)
    ap.add_argument("--log", required=True)
    ap.add_argument("--runner", required=True)
    ap.add_argument("--out")
    a = ap.parse_args()
    job, plan, _ = flow.load_job(a.job)
    text, errors = report(job, plan, *measure(a.log, a.runner, plan))
    text = ("INVALID: " + "; ".join(errors) + "\n\n" if errors else "") + text
    if a.out:
        Path(a.out).write_text(text, encoding="utf-8")
    else:
        sys.stdout.reconfigure(encoding="utf-8")
        print(text)
    print(f"{job['name']}: {'INVALID' if errors else 'REVIEW_REQUIRED'}; {len(errors)} missing observations" +
          (f"; {a.out}" if a.out else ""))
    return int(bool(errors))


if __name__ == "__main__":
    sys.exit(main())
