"""Statistical equivalence gate of two replay pipelines from a frame_replay --seed-sweep run.

    py -3 tools/replay_seed_gate.py <out dir> [--json report.json]

The sweep (tools/frame_replay.md "Seed sweep") gives each side (A, B) two independent sets of N draws (set 0: quartet
phases 0-1, set 1: phases 2-3), each draw with its own random streams. The control is compute against compute
(A1 vs A0); the candidate is B against A on the same set (B0 vs A0, B1 vs A1). Every threshold comes from the
control's draws or the encoding, none is tuned:
  (a) energy: per output channel, the image mean of each draw; |mean(B) - mean(A)| must be within 3 standard
      errors of the difference of two N-draw means, from the pooled compute draws (A0 and A1);
  (b) per pixel: a component exceeds when |mean_B - mean_A| is above one encoding step (fp16 ulp of the value, or
      float32 for 32-bit outputs) and above 4 sigma of its own draws (sqrt((var_A + var_B) / N)); the candidate's
      exceed fraction must not pass the control's by more than 3 standard errors of a difference of two binomial
      fractions over the same pixels (floor 1 / pixels);
  (c) counters: per counter total (rt_stats sweeps), the same 3-standard-error rule as (a); the number of failing
      counters must stay within 3 Poisson standard errors of the expected count (the control's, or 0.27% of the counters).
"""
from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path

import numpy as np


def read_index(out: Path) -> dict:
    index = {}
    for line in (out / 'seed_index.txt').read_text().split('\n'):
        if line.strip():
            key, w, h, comps, n = line.split()
            index[key] = dict(w=int(w), h=int(h), comps=int(comps), n=int(n))
    return index


def load(out: Path, key: str, meta: dict) -> tuple[np.ndarray, np.ndarray]:
    shape = (meta['h'] * meta['w'], meta['comps'])
    mean = np.fromfile(out / f'seed_{key}.mean.f32', np.float32).astype(np.float64).reshape(shape)
    var = np.fromfile(out / f'seed_{key}.var.f32', np.float32).astype(np.float64).reshape(shape)
    return mean, var


def encoding_step(mean: np.ndarray, half: bool) -> np.ndarray:
    """One step of the stored encoding at the value (fp16 or float32 ulp)."""
    if half:
        return np.spacing(np.abs(mean).astype(np.float16)).astype(np.float64)
    return np.spacing(np.abs(mean).astype(np.float32)).astype(np.float64)


def exceed_fraction(ma, va, mb, vb, n: int, half: bool) -> np.ndarray:
    """Per component: fraction of pixels whose mean difference passes both one step and 4 sigma."""
    d = np.abs(mb - ma)
    sigma = np.sqrt((va + vb) / n)
    step = np.maximum(encoding_step(ma, half), encoding_step(mb, half))
    ex = (d > step) & (d > 4.0 * sigma)
    return ex.mean(axis=0)


def fraction_bound(control: float, pixels: int) -> float:
    # Difference of two proportions over the same pixel count: both the control's and the candidate's counting noise.
    f = max(control, 1.0 / pixels)
    return control + 3.0 * math.sqrt(2.0 * f * (1.0 - min(f, 0.5)) / pixels)


def mean_test(a: list[float], b: list[float], pooled_var: float) -> tuple[float, float]:
    """Difference of the two draw means and its 3-standard-error bound from the compute draws."""
    n = min(len(a), len(b))
    return float(np.mean(b) - np.mean(a)), 3.0 * math.sqrt(2.0 * pooled_var / n)


def read_means(out: Path) -> dict:
    rows = {}
    with (out / 'seed_means.csv').open(newline='') as f:
        for r in csv.DictReader(f):
            rows.setdefault((r['side'] + r['set'], r['output']), []).append([float(r[f'c{k}']) for k in range(4)])
    return rows


def read_counters(out: Path) -> dict:
    rows: dict = {}
    path = out / 'seed_counters.csv'
    if not path.exists():
        return rows
    with path.open(newline='') as f:
        for r in csv.DictReader(f):
            rows.setdefault(int(r['counter']), {}).setdefault(r['side'] + r['set'], {})[int(r['seed'])] = int(r['total'])
    return rows


def evaluate(out: Path) -> dict:
    index = read_index(out)
    means = read_means(out)
    outputs = sorted({k.split('_', 1)[1] for k in index})
    report = {'outputs': {}, 'counters': None, 'passed': True}
    for name in outputs:
        meta = index[f'A0_{name}']
        n = meta['n']
        half = name == 'giRaw'
        a0, va0 = load(out, f'A0_{name}', meta)
        a1, va1 = load(out, f'A1_{name}', meta)
        b0, vb0 = load(out, f'B0_{name}', index[f'B0_{name}'])
        b1, vb1 = load(out, f'B1_{name}', index[f'B1_{name}'])
        pixels = a0.shape[0]
        ctrl = exceed_fraction(a0, va0, a1, va1, n, half)
        cand = [exceed_fraction(a0, va0, b0, vb0, n, half), exceed_fraction(a1, va1, b1, vb1, n, half)]
        chans = []
        for k in range(meta['comps']):
            dA0 = [r[k] for r in means[('A0', name)]]
            dA1 = [r[k] for r in means[('A1', name)]]
            dB0 = [r[k] for r in means[('B0', name)]]
            dB1 = [r[k] for r in means[('B1', name)]]
            pooled = float(np.var(dA0 + dA1, ddof=1)) if len(dA0 + dA1) > 1 else 0.0
            c_d, c_b = mean_test(dA0, dA1, pooled)
            e0, bound = mean_test(dA0, dB0, pooled)
            e1, _ = mean_test(dA1, dB1, pooled)
            fb = fraction_bound(float(ctrl[k]), pixels)
            ok_energy = abs(e0) <= bound and abs(e1) <= bound if bound > 0 else e0 == 0 and e1 == 0
            ok_pixels = cand[0][k] <= fb and cand[1][k] <= fb
            chans.append(dict(channel=k, mean_a=float(np.mean(dA0 + dA1)), control_energy=c_d, energy=[e0, e1],
                              energy_bound=bound, control_energy_ok=abs(c_d) <= c_b, energy_ok=ok_energy,
                              control_exceed=float(ctrl[k]), exceed=[float(cand[0][k]), float(cand[1][k])],
                              exceed_bound=fb, pixels_ok=ok_pixels))
            report['passed'] &= ok_energy and ok_pixels
        report['outputs'][name] = dict(draws=n, pixels=pixels, channels=chans)
    counters = read_counters(out)
    if counters:
        fails = {'control': 0, 'B0': 0, 'B1': 0}
        worst = []
        for c, sets in counters.items():
            vals = {s: list(sets.get(s, {}).values()) for s in ('A0', 'A1', 'B0', 'B1')}
            n = max(len(v) for v in vals.values())
            vals = {s: v + [0] * (n - len(v)) for s, v in vals.items()}   # a missing row is a zero total
            pooled = float(np.var(vals['A0'] + vals['A1'], ddof=1))
            for label, (x, y) in {'control': ('A0', 'A1'), 'B0': ('A0', 'B0'), 'B1': ('A1', 'B1')}.items():
                d, bound = mean_test(vals[x], vals[y], pooled)
                bad = abs(d) > bound if bound > 0 else d != 0
                fails[label] += bad
                if bad and label != 'control':
                    worst.append((c, label, d, bound))
        # Expected failures of a 3-standard-error rule: 0.27% of the counters (two-sided normal), or the control's.
        m = max(fails['control'], 0.0027 * len(counters))
        limit = m + 3.0 * math.sqrt(m)
        ok = fails['B0'] <= limit and fails['B1'] <= limit
        report['counters'] = dict(counters=len(counters), fails=fails, limit=limit, passed=ok,
                                  failing=[dict(counter=c, set=s, diff=d, bound=b) for c, s, d, b in worst[:20]])
        report['passed'] &= ok
    return report


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('out', type=Path)
    ap.add_argument('--json', type=Path)
    a = ap.parse_args()
    report = evaluate(a.out)
    for name, o in report['outputs'].items():
        for c in o['channels']:
            print(f"{name} c{c['channel']} (N {o['draws']}): mean {c['mean_a']:.6g}; energy B-A {c['energy'][0]:+.3g} / "
                  f"{c['energy'][1]:+.3g}, bound {c['energy_bound']:.3g} (control {c['control_energy']:+.3g}) "
                  f"{'ok' if c['energy_ok'] else 'FAIL'}; pixels > 4 sigma {c['exceed'][0]:.2e} / {c['exceed'][1]:.2e}, "
                  f"control {c['control_exceed']:.2e}, bound {c['exceed_bound']:.2e} {'ok' if c['pixels_ok'] else 'FAIL'}")
    if report['counters']:
        k = report['counters']
        print(f"counters: {k['counters']} nonzero; failing control {k['fails']['control']}, B0 {k['fails']['B0']}, "
              f"B1 {k['fails']['B1']}, limit {k['limit']:.1f} {'ok' if k['passed'] else 'FAIL'}")
        for f in k['failing'][:8]:
            print(f"  counter {f['counter']} {f['set']}: diff {f['diff']:+.4g}, bound {f['bound']:.4g}")
    print('PASS' if report['passed'] else 'FAIL')
    if a.json:
        a.json.write_text(json.dumps(report, indent=1, default=lambda o: o.item() if hasattr(o, 'item') else str(o)))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
