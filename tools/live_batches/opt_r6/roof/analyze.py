"""Reset-aligned GPU receipts. Only missing evidence returns nonzero."""
import argparse
import json
import math
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT))
from tools.live_batches import live_flow as flow
from tools.live_batches.release_qual.analyze_final import costs
from tools.live_batches.opt_r6.roof.make_job import STATES, TIMERS

LABELS = [label+'--static-a' for label, _ in STATES]


def analyze(measured):
    errors, passes = [], {}
    for name, _ in TIMERS:
        for label in LABELS:
            row = measured.get(label, {}).get(name, {})
            if row.get('samples', 0) < 3 or not math.isfinite(row.get('median_ms', math.nan)):
                errors.append(label+': missing three complete finite periods for '+name)
        if any(name not in measured.get(label, {}) for label in LABELS):
            continue
        med = {label: measured[label][name]['median_ms'] for label in LABELS}
        off = [med[LABELS[i]] for i in (0, 2)]
        on = [med[LABELS[i]] for i in (1, 3)]
        # Same median/spread definition as ab_receipts.summary; compare every pair.
        spread = max(off)-min(off)
        passes[name] = dict(window_ms=med, off_repeat_spread_ms=spread,
            on_delta_ms={LABELS[i]: med[LABELS[i]]-sum(off)/2 for i in (1, 3)},
            paired_delta_ms={LABELS[i]: med[LABELS[i]]-med[LABELS[i-1]] for i in (1, 3)},
            every_on_beats_every_off=all(o-n > spread for o in off for n in on),
            unchanged_within_off_spread=all(abs(n-sum(off)/2) <= spread for n in on))
    decision = ('INVALID' if errors else 'SAVING' if passes['GI trace']['every_on_beats_every_off'] else 'NO_SAVING')
    return dict(status=decision, errors=errors, gpu=passes,
                scope='GPU cost only. Image exactness is offline evidence, not a live image measurement.')


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    for key in ('job', 'log', 'runner', 'out'):
        ap.add_argument('--'+key, required=True)
    a = ap.parse_args(argv)
    try:
        job, plan, _ = flow.load_job(a.job)
        result = analyze(costs(a.log, a.runner, plan))
        result['job'] = job['name']
    except (ValueError, OSError) as error:
        result = dict(status='INVALID', errors=[str(error)])
    Path(a.out).write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    for label, delta in result.get('gpu', {}).get('GI trace', {}).get('paired_delta_ms', {}).items():
        print(f'GI trace {label} ON-OFF: {delta:+.6f} ms')
    print(result['status']+': '+ '; '.join(result['errors']))
    return int(bool(result['errors']))


if __name__ == '__main__':
    sys.exit(main())
