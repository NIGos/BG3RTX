"""D-EM ours/game gate with a derived tolerance (QUALITY.md R0, docs/QA.md thresholds).

Per family with >= 1 bright px per frame, from the `[em] ours/game per family` lines of a log:
  r_w     the family's ratio in window w (the `bright` ratio when logged, else the all-hit one)
  s_w     sample std of ln r_w over the windows after the first (the first holds the
          calibration's start), at the run's fixed view: the measured temporal noise
  N       bright px per frame (independent-pixel upper bound: neighbours share texels, so the
          true count is lower and the tolerance larger: the gate errs toward failing)
  exact families:  test the mean of ln r_w;   sigma^2 = s_w^2 / W + 1 / N
  approx families: test the last window;      sigma^2 = s_w^2 + 1 / N
  pass when |ln r| <= z sigma, z two-sided at ALPHA_FW / families (Bonferroni), as
  scripts/qa/thresholds.py.

    python -m scripts.qa.em_gate captures/queue/logs/em10-af-check.*.log ...

The 0.8-1.25 band and the 200 px cut it replaces were not derived (HANDOFF.md, TRACK_D 13.15).
"""
import math, re, sys
from statistics import NormalDist, mean, stdev

from .thresholds import ALPHA_FW
pat = re.compile(r"([0-9a-f]{8}) (\S+) ([0-9.]+)(?:, bright ([0-9.]+))? \(([0-9]+) bright px of ([0-9]+)(, approx)?")


def windows(path):
    out = []
    for line in open(path, encoding="utf-8", errors="replace"):
        if "[em] ours/game per family" not in line:
            continue
        fam = {}
        for m in pat.finditer(line):
            f, name, r, rb, nb, _, approx = m.groups()
            fam[f] = (name, float(rb) if rb else float(r), int(nb), bool(approx))
        if "(part 1/" in line or "(part" not in line:
            out.append(fam)
        else:
            out[-1].update(fam)
    return out


def evaluate(path):
    ws = windows(path)
    fams = {}
    for i, w in enumerate(ws):
        for f, v in w.items():
            fams.setdefault(f, []).append((i, v))
    gated = {f: v for f, v in fams.items() if mean(x[1][2] for x in v) >= 1 and all(x[1][1] > 0 for x in v)}
    z = NormalDist().inv_cdf(1 - ALPHA_FW / (2 * max(len(gated), 1)))
    print(f"{path}: {len(ws)} windows, {len(gated)} families gated, z = {z:.2f}")
    ok_all = True
    for f, v in sorted(gated.items()):
        name, approx = v[-1][1][0], v[-1][1][3]
        lr = [math.log(x[1][1]) for x in v]
        later = lr[1:] if len(lr) > 2 else lr
        s_w = stdev(later) if len(later) > 1 else float("nan")
        n = mean(x[1][2] for x in v)
        if math.isnan(s_w):
            print(f"  {f} {name}: one window only, not evaluated")
            continue
        if approx:
            stat, sig = lr[-1], math.sqrt(s_w ** 2 + 1.0 / n)
        else:
            stat, sig = mean(later), math.sqrt(s_w ** 2 / len(later) + 1.0 / n)
        ok = abs(stat) <= z * sig
        ok_all &= ok
        print(f"  {f} {name[:40]:40s} {'approx' if approx else 'exact '} ratio {math.exp(stat):.3f} "
              f"s_w {s_w:.3f} N {n:.0f} sigma {sig:.3f} band {math.exp(-z*sig):.3f}-{math.exp(z*sig):.3f} "
              f"{'PASS' if ok else 'FAIL'}")
    return ok_all


if __name__ == "__main__":
    ok = True
    for p in sys.argv[1:]:
        ok &= evaluate(p)
    sys.exit(0 if ok else 1)
