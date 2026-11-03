"""Same-run S8 glint energy and smooth-turn residual gates. Never launches BG3.

The second difference is computed within each burst, using exact water coverage.
It is a smooth-motion proxy, not a correspondence claim between different pans.
"""
import argparse
import json
import math
from pathlib import Path
import re
import statistics
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.m4_promotion_gate import ALPHA_FW, Run, t_quantile
from tools.m5_promotion_gate import traces_of
from tools.water_surface_gate import post_colour


def energy_gate(phases):
    if len(phases) != 4 or any(len(p) < 3 for p in phases):
        raise ValueError('four static phases with at least three float shots each required')
    if any(not math.isfinite(v) or v <= 0 for p in phases for v in p):
        raise ValueError('nonfinite/nonpositive static energy')
    off = phases[0] + phases[2]
    mean = statistics.mean(off)
    sigma = statistics.stdev(off)
    comparisons = len(phases[1]) + len(phases[3])
    # Existing project's 5% family-wise Student-t rule, same-run OFF samples
    # only. Variance is not estimated from ON, so dimming cannot widen the gate.
    band = t_quantile(1-ALPHA_FW/(2*comparisons), len(off)-1)*sigma*math.sqrt(1+1/len(off))
    differences = [v-mean for p in (phases[1], phases[3]) for v in p]
    return dict(pass_energy=all(abs(d) <= band for d in differences),
                off_mean=mean, off_sd=sigma, off_samples=len(off),
                bound=band, on_minus_off=differences,
                method='same-run OFF SD, Student t, 5% family-wise, ON minus estimated OFF mean')


def residual_gate(scores):
    if set(scores) != {f'{d}{i}' for d in ('E', 'Q') for i in range(4)}:
        raise ValueError('two OFF and two ON bursts for each E/Q required')
    if any(not math.isfinite(v) or v < 0 for v in scores.values()):
        raise ValueError('nonfinite/negative residual')
    rows = {}
    for d in ('E', 'Q'):
        a, b = scores[d+'0'], scores[d+'2']
        spread = abs(a-b)
        limit = min(a, b)-spread
        rows[d] = dict(off=[a, b], off_repeat_spread=spread,
                       on=[scores[d+'1'], scores[d+'3']], strict_upper_bound=limit,
                       passed=all(scores[d+str(i)] < limit for i in (1, 3)))
    return dict(pass_residual=all(r['passed'] for r in rows.values()), directions=rows,
                method='each ON mean absolute second difference improves beyond same-direction OFF/OFF spread')


def burst_residual(directory):
    import numpy as np
    from scripts.qa.reflection_toggle import load_stream, load_coverage
    rgb, ids, _ = load_stream(directory, 'water_game')
    if len(ids) != 8 or not np.isfinite(rgb).all() or np.any(rgb < 0):
        raise ValueError('K8 finite nonnegative raw water colour required')
    mask = load_coverage(directory, ids, rgb.shape[1:3]).all(axis=0)
    if not mask.any():
        raise ValueError('empty all-frame common exact water coverage')
    y = rgb @ np.array([.2126, .7152, .0722])
    residual = np.abs(y[1:-1]-(y[:-2]+y[2:])*.5)
    return dict(residual=float(residual[:, mask].mean()), pixels=int(mask.sum()),
                mean=float(y[:, mask].mean()), frames=ids,
                limit='same-screen smooth-turn proxy; no cross-pan correspondence')


def evaluate(run):
    if run.failed_drift():
        raise ValueError(run.drift)
    phases = []
    for i in range(4):
        label = f'glint-static-{i}'
        captures = traces_of(run, label)
        if len(captures) != 3:
            raise ValueError(f'{label}: expected exactly three float shots')
        values = []
        for frame, cap in captures:
            exposure = run.pre_exposure(frame)
            if exposure is None or not math.isfinite(exposure) or exposure <= 0:
                raise ValueError(f'{label}: missing/invalid lighting exposure')
            values.append(post_colour(cap)/exposure)
        phases.append(values)
    bursts = {}
    for direction in ('E', 'Q'):
        for i in range(4):
            key = f'{direction}{i}'
            phase = run.phase_of('glint-turn-'+key)
            if phase is None:
                raise ValueError('missing motion phase '+key)
            paths = [Path(m.group(1).strip()) for t, line in run.lines if phase[1] <= t < phase[2]
                     for m in [re.search(r'\[pan\] burst \d+ complete: 8 frames, 16 samples, transport ok -> (.+)$', line)] if m]
            if len(paths) != 1:
                raise ValueError('missing/duplicate complete motion burst '+key)
            bursts[key] = burst_residual(paths[0])
    energy = energy_gate(phases)
    residual = residual_gate({k: v['residual'] for k, v in bursts.items()})
    return dict(status='PASS' if energy['pass_energy'] and residual['pass_residual'] else 'FAIL',
                energy=energy, residual=residual, bursts=bursts,
                quality_limit='Image gates do not replace the dependent offline native-BRDF reference. Review framing and coverage; smooth-turn residual is a proxy.',
                cost_limit='Compose timer excludes added forward FS work; capture windows are excluded from cost.')


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('log', type=Path)
    args = ap.parse_args(argv)
    try:
        report = evaluate(Run(args.log))
    except (ValueError, OSError, KeyError, TypeError) as error:
        report = dict(status='INVALID', error=str(error))
    print(json.dumps(report, indent=2))
    return int(report['status'] != 'PASS')


if __name__ == '__main__':
    sys.exit(main())
