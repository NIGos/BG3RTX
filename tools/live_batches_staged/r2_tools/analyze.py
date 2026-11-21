"""r2-d1-roof-em verdict: gi_far_roof_emission 1 (ON) against the OFF pair (0) of the same run in Test Dialogue1.

    py -3 tools/live_batches_staged/r2_tools/analyze.py --job <job.json> --log <game.log> --runner <runner.txt> --out <md>

Reads a finished run; no game or queue access. Window values are perf_ab's (complete periods after each window's
acknowledged stats reset), the verdicts handout_qual's (tools/live_batches/sane_gates.py: OFF noise plus
max(0.1 ms, 1%)): GI GPU ms, the GI trace median and RT passes "not higher". Energy: omm_ab's float pre-post means
(dumps.json, exposure held) of d-energy-on against the OFF samples, floor 0.5%.
"""
import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.live_batches import live_flow as flow, sane_gates as sane
from tools.live_batches_staged.handout_qual import analyze as hq
from tools.live_batches_staged.omm_ab import analyze as oa
from tools.live_batches_staged.perf_ab import analyze as pab

hq.JUDGED = [("GI", "not higher"), ("GI trace", "not higher"), ("RT", "not higher"), ("scene wait timeouts", "zero")]
hq.DECISIVE = ("GI", "GI trace", "RT")
hq.PREDICTED_LOWER = ()   # the roof rule only skips emission; a cost drop is not predicted: EXPLAIN
oa.PRESETS = {"d": "Ultra"}


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
    efound, etexts, enotes = oa.energy(a.log, a.runner, plan_path, plan)
    for status, texts in efound.items():
        found[status] += texts
    notes += enotes
    verdict = hq.verdict_of(found, not decisive)
    text = "\n".join([f"# {job['name']}: {verdict}", ""] +
                     [f"- {s}: {t}" for s in (sane.FAIL, sane.EXPLAIN) for t in found[s]] +
                     [f"- not judged: {n}" for n in notes] + [f"- INFO: {t}" for t in found[sane.INFO]] +
                     ["", sane.floors_text(), "", "## ON (gi_far_roof_emission=1) against the OFF pair (0)", "",
                      "| Group | Line | Rule | Values | |", "| --- | --- | --- | --- | --- |"] + rows +
                     ["", "## Energy (static, exposure held, three float traces per state)", ""] + etexts + ["", table])
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
