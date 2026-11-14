"""vram_rightsize: perf_ab's table per window plus the pools' reserved and used MB, the event counts and the energy.

    py -3 tools/live_batches_staged/vram_rightsize/analyze.py --job <job.json> --log <game.log> --runner <runner.txt> --out <md>

Reads a finished run's log; no game or queue access. Rows come from the '[mem] pools reserved/used MB' line (one per
120-frame window with detailed_logging); events are counted over the whole window, settling included.
"""
import argparse
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts.queue_gates import stamped_lines, windows
from tools.live_batches import live_flow as flow
from tools.live_batches_staged.perf_ab import analyze as base

P = r"\[mem\] pools reserved/used MB: .*?"
base.ROWS += [
    ("cascade capture reserved", "MB", P + r"cascade capture (\d+)/"), ("cascade capture used", "MB", P + r"cascade capture \d+/(\d+)"),
    ("RT capture reserved", "MB", P + r"RT capture (\d+)/"), ("RT capture used", "MB", P + r"RT capture \d+/(\d+)"),
    ("BLAS chunks reserved", "MB", P + r"BLAS chunks (\d+)/"), ("BLAS chunks used", "MB", P + r"BLAS chunks \d+/(\d+)"),
    ("BLAS chunks pending", "MB", P + r"pending (\d+),"), ("BLAS empty chunks", "MB", P + r"empty chunks (\d+),"),
    ("mesh pool reserved", "MB", P + r"mesh pool (\d+)/"), ("mesh pool used (pools line)", "MB", P + r"mesh pool \d+/(\d+)"),
    ("hit-material recycled", "MB", P + r"hit-material recycled (\d+)"), ("OMM", "MB", P + r"OMM (\d+)"),
    ("hair in the pool", "MB", P + r"hair (\d+) in the pool"), ("hair BLAS", "MB", P + r"\+ (\d+) BLAS in the chunks"),
    ("frame time max", "ms", r"\[pace\] rendered frame time .*max ([\d.]+) ms"),
    ("opaque draws dropped (buffer full)", "per frame", r"\[geo\] RT capture, last \d+ frames:.*opaque draws dropped \(buffer full\) ([\d.]+)"),
    ("cascade casters buffer full", "per frame", r"\[cascade\] casters.*buffer full ([\d.]+)"),
]
# Events of a window (not period lines): how often a pool was replaced, refused or left something out.
EVENTS = [("RT capture replaced", r"\[geo\] RT capture capacity \d+ -> \d+ MB"),
          ("cascade capture replaced", r"\[cascade\] capture capacity \d+ -> \d+ MB"),
          ("capture resize deferred", r"capture resize deferred"),
          ("mesh pool grown", r"\[bvh\] mesh pool grown"), ("mesh pool trimmed or repacked", r"\[bvh\] mesh pool capacity: (?:trimmed|repacked)"),
          ("guard refusals", r"\[vram\] guard: refused"), ("guard LOW", r"guard LOW"),
          ("region full warnings", r"\[geo\] WARNING (?:alpha-tested|opaque) region full"),
          ("device lost or crash", r"VK_ERROR_DEVICE_LOST|\[crash\] exception")]


def events(log, runner, plan):
    lines = stamped_lines(Path(log).read_text(encoding="utf-8-sig", errors="replace"))
    run = Path(runner).read_text(encoding="utf-8-sig", errors="replace")
    rows = []
    for w in windows(run, plan, lines):
        if not (0 <= w["index"] < len(plan)):
            continue
        inside = [s for t, s in lines if w["start"] <= t < w["end"]]
        rows.append((w["label"], [sum(bool(re.search(rx, s)) for s in inside) for _, rx in EVENTS]))
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ("--job", "--log", "--runner"):
        ap.add_argument(name, required=True)
    ap.add_argument("--out")
    a = ap.parse_args()
    job, plan, plan_path = flow.load_job(a.job)
    text, errors = base.report(job, plan, *base.measure(a.log, a.runner, plan))
    text += "\n| Window | " + " | ".join(n for n, _ in EVENTS) + " |\n| --- | " + " | ".join("---:" for _ in EVENTS) + " |\n"
    for label, counts in events(a.log, a.runner, plan):
        text += f"| {label} | " + " | ".join(str(c) for c in counts) + " |\n"
    try:   # the energy of the ON state against the OFF pair (handout_qual's helper; needs the traces' dumps.json)
        from tools.live_batches_staged.handout_qual.analyze import energy
        etext, efound, _ = energy(a.log, a.runner, plan_path, plan, None)
        text += "\nEnergy (static, exposure held): " + str(etext) + "\n"
        for status, found in (efound or {}).items():
            text += "".join(f"- {status}: {f}\n" for f in found)
    except Exception as e:   # a missing trace folder must not hide the tables
        text += f"\nEnergy not evaluated: {e}\n"
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
