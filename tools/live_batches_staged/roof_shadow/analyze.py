"""roof_shadow: perf_ab's table per window plus the sun stats roof numbers and the energy of ON against the OFF pair.

    py -3 tools/live_batches_staged/roof_shadow/analyze.py --job <job.json> --log <game.log> --runner <runner.txt> --out <md>

Reads a finished run's log; no game or queue access. The sun stats line has a 600-frame period and no stats reset:
the table takes each window's last line (its counters cover that window only). The band itself is judged on the
PNGs of the energy windows (4 per state, 5 s apart); this report gives the numbers around it.
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

SUN = r"sun stats \(\d+ frames, replace 1, "
COLUMNS = [("roof volumes", SUN + r"(\d+) roof volumes"), ("roof query m", r"roof query (-?\d+) m"),
           ("px under roofs %", r"px under roofs ([\d.]+)%"), ("kept by roofs %", r"kept by roofs ([\d.]+)%"),
           ("game dark & RT lit %", r"game dark & RT lit ([\d.]+)%"),
           ("mean brightening", r"mean brightening ([\d.]+) per traced px"),
           ("over cap frames", r"over kSunMaxVolumes on (\d+) frames")]


def sun_rows(log, runner, plan):
    """[(window label, lines in the window, {column: value of the last line})] for the static windows."""
    lines = stamped_lines(Path(log).read_text(encoding="utf-8-sig", errors="replace"))
    run = Path(runner).read_text(encoding="utf-8-sig", errors="replace")
    rows = []
    for w in windows(run, plan, lines):
        if not (0 <= w["index"] < len(plan)) or not w["label"].startswith("static-"):
            continue
        inside = [s for t, s in lines if w["applied"] <= t < w["end"] and re.search(SUN, s)]
        last = inside[-1] if inside else ""
        rows.append((w["label"], len(inside), {n: (m.group(1) if (m := re.search(rx, last)) else "-") for n, rx in COLUMNS}))
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ("--job", "--log", "--runner"):
        ap.add_argument(name, required=True)
    ap.add_argument("--out")
    a = ap.parse_args()
    job, plan, plan_path = flow.load_job(a.job)
    text, errors = base.report(job, plan, *base.measure(a.log, a.runner, plan))
    rows = sun_rows(a.log, a.runner, plan)
    text += ("\n| Window | sun stats lines | " + " | ".join(n for n, _ in COLUMNS) + " |\n| --- | ---: | " +
             " | ".join("---:" for _ in COLUMNS) + " |\n")
    for label, n, values in rows:
        text += f"| {label} | {n} | " + " | ".join(values[c] for c, _ in COLUMNS) + " |\n"
    errors += [f"{label}: no sun stats line" for label, n, _ in rows if not n]
    world = re.findall(r"sun: world roof selection: level (\S+), (\d+) volumes \((\d+) uploaded\)",
                       Path(a.log).read_text(encoding="utf-8-sig", errors="replace"))
    text += "\nWorld selection lines: " + ("; ".join(f"{lv} {t} volumes, {u} uploaded" for lv, t, u in world) or "none") + "\n"
    if not world or any(t != u for _, t, u in world):
        errors.append("world roof selection line missing or total != uploaded")
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
