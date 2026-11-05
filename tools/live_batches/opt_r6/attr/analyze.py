"""Reset-aligned diagnostic attribution. Only missing evidence returns nonzero."""
import argparse
import math
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT))
from tools.live_batches import live_flow as flow
from tools.live_batches.release_qual.analyze_final import costs
from tools.live_batches.opt_r6.attr.make_job import STATES, TIMERS

LABELS = [label+'--static-a' for label, _ in STATES]
EXTRA_COSTS = {name: rx+r' ([\d.]+) ms/frame' for name, rx in TIMERS
              if name in ('origin', 'sun trace')}


def analyze(measured, states=STATES):
    errors, rows = [], []
    labels = [label+'--static-a' for label, _ in states]
    for name, _ in TIMERS:
        med = {}
        for label in labels:
            receipt = measured.get(label, {}).get(name, {})
            value = receipt.get('median_ms', math.nan)
            if receipt.get('samples', 0) < 3 or not math.isfinite(value):
                errors.append(label+': missing three complete finite periods for '+name)
            else:
                med[label] = value
        for i, (label, overrides) in enumerate(states):
            if not overrides:
                continue
            before, variant, after = labels[i-1:i+2]
            if all(k in med for k in (before, variant, after)):
                mean = (med[before]+med[after])/2
                rows.append(dict(variant=label, timer=name, before_ms=med[before],
                    variant_ms=med[variant], after_ms=med[after], baseline_mean_ms=mean,
                    delta_ms=med[variant]-mean, baseline_drift_ms=med[after]-med[before]))
    return dict(status='INVALID' if errors else 'ATTRIBUTION', errors=errors, rows=rows,
                measured=measured, labels=labels)


def markdown(result):
    lines = ['# '+result['status'], '',
             'Diagnostic only; image changes are expected. Deltas interact and must not be summed.',
             'Delta = variant minus mean of adjacent baselines; drift = after minus before. All values are ms.', '',
             '| Variant | Timer | Before | Variant | After | Baseline mean | Delta | Drift |',
             '|---|---|---:|---:|---:|---:|---:|---:|']
    for row in result['rows']:
        lines.append('| '+row['variant']+' | '+row['timer']+' | '+ ' | '.join(
            f'{row[k]:.4f}' for k in ('before_ms', 'variant_ms', 'after_ms',
                                     'baseline_mean_ms', 'delta_ms', 'baseline_drift_ms'))+' |')
    lines += ['', '## Window receipts', '', '| Window | Timer | Samples | Median ms |',
              '|---|---|---:|---:|']
    for label in result.get('labels', LABELS):
        for name, _ in TIMERS:
            row = result['measured'].get(label, {}).get(name, {})
            lines.append(f"| {label} | {name} | {row.get('samples', 0)} | {row.get('median_ms', 'missing')} |")
    if result['errors']:
        lines += ['', 'Missing evidence:', ''] + ['- '+e for e in result['errors']]
    return '\n'.join(lines)+'\n'


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    for key in ('job', 'log', 'runner', 'out'):
        ap.add_argument('--'+key, required=True)
    a = ap.parse_args(argv)
    try:
        _, plan, _ = flow.load_job(a.job)
        labels = {p['label'] for p in plan if p.get('frames')}
        states = [(label, values) for label, values in STATES if label+'--static-a' in labels]
        result = analyze(costs(a.log, a.runner, plan, extra_costs=EXTRA_COSTS), states)
    except (ValueError, OSError) as error:
        result = dict(status='INVALID', errors=[str(error)], rows=[], measured={})
    Path(a.out).write_text(markdown(result), encoding='utf-8')
    print(result['status']+': '+ '; '.join(result['errors']))
    return int(bool(result['errors']))


if __name__ == '__main__':
    sys.exit(main())
