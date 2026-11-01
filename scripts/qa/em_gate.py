"""D-EM ours/game gate with a derived tolerance (QUALITY.md R0, docs/QA.md thresholds).

Per family with >= 1 bright px per frame, from the `[em] ours/game per family` lines of a log:
  r_w     the family's ratio in window w. Exact families: the `bright` ratio (their radiance
          is modelled per pixel, so the visible emission is compared); approximate
          (calibrated) families: the all-hit ratio (only their mean is modelled and
          calibrated, so their energy is compared: what the light list carries)
  s_w     sample std of ln r_w over the windows after the first (the first holds the
          calibration's start), at the run's fixed view: the measured temporal noise
  N       bright px per full-screen check (independent-pixel upper bound: neighbours share texels, so the
          true count is lower and the tolerance larger: the gate errs toward failing)
  exact families:  test the mean of ln r_w;   sigma^2 = s_w^2 / W + 1 / N
  approx families: test the last window;      sigma^2 = s_w^2 + 1 / N
  pass when |ln r| <= t sigma, t two-sided at ALPHA_FW / families (Bonferroni, as
  scripts/qa/thresholds.py), with the Welch-Satterthwaite degrees of freedom of sigma: the
  temporal term is estimated from W - 1 window differences, the counting term is known.
  Fewer than 2 windows after the first: not evaluated (no temporal spread measured).

    python -m scripts.qa.em_gate captures/queue/logs/em10-af-check.*.log ...

The 0.8-1.25 band and the 200 px cut it replaces were not derived (HANDOFF.md, TRACK_D 13.15).
"""
import math, re, sys
from statistics import NormalDist, mean, stdev

from .thresholds import ALPHA_FW
pat = re.compile(r"([0-9a-f]{8}) (\S+) ([0-9.]+)(?:, bright ([0-9.]+))? \(([0-9]+) bright px of ([0-9]+)(?:, [0-9]+ overlapped)?(, approx)?")


def t_quantile(p, dof):
    """Student t quantile: exact for 1 and 2 degrees of freedom, the Cornish-Fisher expansion
    in 1/dof above (error < 1e-3 for dof >= 3 at the tails used here), normal at dof -> inf."""
    if dof <= 1.0:
        return math.tan(math.pi * (p - 0.5))
    if dof <= 2.0:
        return (2 * p - 1) / math.sqrt(2 * p * (1 - p))
    z = NormalDist().inv_cdf(p)
    v = dof
    return (z + (z ** 3 + z) / (4 * v) + (5 * z ** 5 + 16 * z ** 3 + 3 * z) / (96 * v ** 2)
            + (3 * z ** 7 + 19 * z ** 5 + 17 * z ** 3 - 15 * z) / (384 * v ** 3))


slice_pat = re.compile(r"\[em\] check GPU cost .*schedule rt_em_check_frames -?\d+, slice 1/(\d+)")


def windows(path):
    """Per window {family: (name, ratio, bright px per full-screen check, approx)}. Since
    rt_em_check_slices (TRACK_D_EMISSIVE 13.24) a run of the check covers 1/S of the pixels and the
    layer's "bright px" is per run: x S from the latest `[em] check GPU cost` line (S = 1 without
    one, as in older logs; a job must keep that line in its patterns)."""
    out = []
    slices = 1
    for line in open(path, encoding="utf-8", errors="replace"):
        m = slice_pat.search(line)
        if m:
            slices = max(int(m.group(1)), 1)
            continue
        if "[em] ours/game per family" not in line:
            continue
        fam = {}
        for m in pat.finditer(line):
            f, name, r, rb, nb, _, approx = m.groups()
            fam[f] = (name, float(rb) if (rb and not approx) else float(r), int(nb) * slices, bool(approx))
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
        later = lr[1:]
        s_w = stdev(later) if len(later) > 1 else float("nan")
        n = mean(x[1][2] for x in v)
        if math.isnan(s_w):
            print(f"  {f} {name}: fewer than 2 windows after the first, not evaluated")
            continue
        var_t = s_w ** 2 if approx else s_w ** 2 / len(later)
        stat = lr[-1] if approx else mean(later)
        sig = math.sqrt(var_t + 1.0 / n)
        dof = (sig ** 4) / (var_t ** 2 / (len(later) - 1)) if var_t > 0 else float("inf")
        t = z if math.isinf(dof) else t_quantile(1 - ALPHA_FW / (2 * max(len(gated), 1)), dof)
        ok = abs(stat) <= t * sig
        ok_all &= ok
        print(f"  {f} {name[:40]:40s} {'approx' if approx else 'exact '} ratio {math.exp(stat):.3f} "
              f"s_w {s_w:.3f} N {n:.0f} sigma {sig:.3f} dof {dof:.1f} band {math.exp(-t*sig):.3f}-{math.exp(t*sig):.3f} "
              f"{'PASS' if ok else 'FAIL'}")
    return ok_all


if __name__ == "__main__":
    ok = True
    for p in sys.argv[1:]:
        ok &= evaluate(p)
    sys.exit(0 if ok else 1)
