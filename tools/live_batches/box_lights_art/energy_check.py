"""Read-only S6 energy check; uses the existing scene-mean reader and noise quantile."""
import argparse
import json
import math
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from tools.m4_promotion_gate import Run, t_quantile, ALPHA_FW
from scripts import queue_gates


def evaluate(values):
    """Same-run energy differences; unresolved recovery is reported separately."""
    if any(not math.isfinite(v) or v <= 0 for v in values.values()):
        raise ValueError('nonpositive or nonfinite scene mean')
    failures = []
    inconclusive = []
    off = [values[f'shot-off-{c}'] for c in 'abcde']
    sigma = math.sqrt(sum((b-a)**2 for a,b in zip(off,off[1:])) / 8)
    # Existing family-wise false alarm and Student quantile, four comparisons, four OFF pairs.
    band = t_quantile(1 - ALPHA_FW / 8, 4) * sigma * math.sqrt(1.5)
    deltas = {}
    for label,a,b in [('old-a',0,1),('fixed-a',1,2),('old-b',2,3),('fixed-b',3,4)]:
        deltas[label] = values['shot-'+label] - (off[a]+off[b])/2
    for repeat in 'ab':
        old, fixed = deltas['old-'+repeat], deltas['fixed-'+repeat]
        if fixed < -band:
            failures.append(f'fixed-{repeat}: energy below OFF minus noise')
        if abs(old) - abs(fixed) <= 2*band:
            inconclusive.append(f'{repeat}: recovery not resolved above noise (inconclusive)')
    return sigma, band, deltas, failures, inconclusive


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('log', type=Path)
    ap.add_argument('--captures-root', type=Path)
    args = ap.parse_args()
    batch = Path(__file__).resolve().parent
    job = json.loads((batch / 'jobs/ba1-box-art-s6-fg2.json').read_text())
    plan = json.loads((batch / 'plans/art_s6.json').read_text())
    game = args.log.read_text(encoding='utf-8-sig', errors='replace')
    runner = args.log.with_suffix('.runner.txt').read_text(encoding='utf-8-sig', errors='replace')
    failures = queue_gates.check(job['expect_every'], game, runner, plan)
    run = Run(args.log, captures_root=args.captures_root, plan=batch / 'plans/art_s6.json')
    if run.failed_drift():
        raise ValueError(run.drift)
    values = {}
    for label in run.labels:
        if not label.startswith('shot-'):
            continue
        value = run.scene_colour(label)
        if not value or isinstance(value[0], str) or value[1] or not math.isfinite(value[0]) or value[0] <= 0:
            raise ValueError(f'{label}: invalid scene mean: {value}')
        values[label] = value[0]   # exposure held across every shot; never PNG or bloom
    sigma, band, deltas, energy_failures, inconclusive = evaluate(values)
    failures.extend(energy_failures)
    print(json.dumps(dict(scene_means=values, sigma=sigma, band=band, deltas=deltas, failures=failures,
                         inconclusive=inconclusive), indent=1))
    return int(bool(failures))


if __name__ == '__main__':
    sys.exit(main())
