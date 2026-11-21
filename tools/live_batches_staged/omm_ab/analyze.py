"""omm_ab verdict: rt_omm_reference 0 (ON, accelerated) against the OFF pair (1) of the same run, per preset and motion.

    py -3 tools/live_batches_staged/omm_ab/analyze.py --job <job.json> --log <game.log> --runner <runner.txt> --out <md>

Reads a finished run; no game or queue access. Window values are perf_ab's (complete periods after each window's
acknowledged stats reset), groups and verdicts handout_qual's (tools/live_batches/sane_gates.py): RT passes GPU ms
"lower" (the design), GI GPU ms "not higher", handout fallbacks and wait timeouts events against the OFF windows.
OMM downgrades are INFO with their count: a downgraded slot traces the reference scene (a correct image). Energy:
the float pre-post means (dumps.json, exposure held) of each preset's energy-<state> phases against its OFF samples.
"""
import argparse
from pathlib import Path
import re
import statistics
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts.queue_gates import stamped_lines, windows
from tools.live_batches import live_flow as flow, sane_gates as sane
from tools.live_batches_staged.handout_qual import analyze as hq   # adds the handout rows to perf_ab's
from tools.live_batches_staged.perf_ab import analyze as pab

hq.JUDGED = [("RT", "lower"), ("GI", "not higher"), ("GI trace", "not higher"), ("scene wait timeouts", "zero"),
             ("handout fallbacks", "zero"), ("handout pairing mismatches", "zero"), ("hit material fallbacks (query)", "zero"),
             ("hit material fallbacks (camera)", "zero")]
hq.DECISIVE = ("RT",)
hq.PREDICTED_LOWER = ("RT", "GI", "GI trace")   # the OMM variants: a drop is what the micro-maps are for
PRESETS = {"u": "Ultra", "h": "High"}


def energy(log, runner, plan_path, plan):
    """({status: [text]}, [text], ["not judged"]): per preset, each state's float mean against its OFF samples."""
    from tools import m4_promotion_gate as m4
    from tools.live_batches.gi_1ray_qual.analyze import float_samples
    found, texts, notes = {sane.FAIL: [], sane.EXPLAIN: [], sane.INFO: []}, [], []
    run = m4.Run(Path(log), None, Path(plan_path))
    lines = stamped_lines(Path(log).read_text(encoding="utf-8-sig", errors="replace"))
    run.phases = [(w["label"], w["applied"], w["end"])
                  for w in windows(Path(runner).read_text(encoding="utf-8-sig", errors="replace"), plan, lines)]
    for preset, name in PRESETS.items():
        v = {}
        for p in plan:
            m = re.fullmatch(preset + r"-energy-(.+)", p["label"])
            if m:
                try:
                    v[m[1]] = [x[0] / x[2] for x in float_samples(run, p["label"], None)]
                except (Exception, SystemExit) as e:
                    notes.append(f"{name} {p['label']}: energy not judged ({e})")
        off = [x for s, xs in v.items() if s.startswith("off") for x in xs]
        if len(off) < 2 or len(v.get("on", [])) < 2:
            notes.append(f"{name}: energy not judged (fewer than 2 float traces in the OFF repeats or in ON)")
            continue
        mean = statistics.mean(v["on"])
        status, rel = sane.energy(off, mean)
        text = (f"{name} energy-on: mean {mean:.5f}, {100 * rel:+.2f}% of the OFF mean, OFF samples {min(off):.5f} to "
                f"{max(off):.5f}, floor {sane.ENERGY_FLOOR_REL:.1%}")
        texts.append(f"{text}: {status}")
        if status in found:
            found[status].append(text)
    return found, texts, notes


def downgrades(measured, plan):
    """INFO rows: OMM accelerated commits and downgrades per window (sums of the window's complete periods)."""
    rows = []
    for p in plan:
        row = measured.get(p["label"], {})
        if p.get("frames") and not p.get("trace_at") and row.get("OMM accelerated commits"):
            rows.append(f"| {p['label']} | {sum(row['OMM accelerated commits']):g} | {sum(row.get('OMM downgraded', [0])):g} |")
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--job", required=True)
    ap.add_argument("--log", required=True)
    ap.add_argument("--runner", required=True)
    ap.add_argument("--out")
    a = ap.parse_args()
    job, plan, plan_path = flow.load_job(a.job)
    measured, guard, guard_all, wide = pab.measure(a.log, a.runner, plan)
    timed = [p for p in plan if p.get("frames") and not p.get("trace_at")]
    table, _ = pab.report(job, timed, measured, guard, guard_all, wide)
    rows, found, decisive, notes = hq.judge(measured, plan)
    efound, etexts, enotes = energy(a.log, a.runner, plan_path, plan)
    for status, texts in efound.items():
        found[status] += texts
    notes += enotes
    down = downgrades(measured, plan)
    total = sum(float(r.split("|")[3]) for r in down)
    found[sane.INFO].append(f"OMM downgrades (the slot traced the reference scene, a correct image): {total:g} in the "
                            f"measured windows; per window below")
    verdict = hq.verdict_of(found, not decisive)
    text = "\n".join([f"# {job['name']}: {verdict}", ""] +
                     [f"- {s}: {t}" for s in (sane.FAIL, sane.EXPLAIN) for t in found[s]] +
                     [f"- not judged: {n}" for n in notes] + [f"- INFO: {t}" for t in found[sane.INFO]] +
                     ["", sane.floors_text(), "", "## ON (rt_omm_reference=0) against the OFF pair (1)", "",
                      "| Group | Line | Rule | Values | |", "| --- | --- | --- | --- | --- |"] + rows +
                     ["", "## Energy (static, exposure held, three float traces per state)", ""] + etexts +
                     ["", "## OMM accelerated commits and downgrades per window (INFO)", "",
                      "| Window | accelerated | downgraded |", "| --- | --- | --- |"] + down + ["", table])
    if a.out:
        Path(a.out).write_text(text, encoding="utf-8")
    else:
        sys.stdout.reconfigure(encoding="utf-8")
        print(text)
    print(f"{job['name']}: {verdict}; {len(found[sane.FAIL])} failing, {len(found[sane.EXPLAIN])} to explain, "
          f"{len(notes)} not judged" + (f"; {a.out}" if a.out else ""))
    return int(not decisive or bool(found[sane.FAIL]))


if __name__ == "__main__":
    sys.exit(main())
