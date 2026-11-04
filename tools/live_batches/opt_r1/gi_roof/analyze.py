"""Fail-closed roof receipts; uses the existing queue, float-energy and QA readers."""
import argparse
import json
import math
from pathlib import Path
import re
import statistics
import sys

import numpy as np

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT))
from scripts import queue_gates as gates
from scripts.qa import detectors as det
from tools.live_batches import live_flow as flow
from tools.live_batches.edge_flicker.analyze import burst
from tools.live_batches.release_qual.analyze_final import energy, COSTS
from tools.live_batches.opt_r1.gi_roof.make_jobs import STATES, PASSES

OFF = ['off-a', 'off-b']
ON = ['on-a', 'on-b']
TIMERS = {k: COSTS[k] for k in ('GI', 'GI trace', 'RT')}
TIMERS.update({p: 'gi: '+p+r' pass GPU cost ([\d.]+) ms/frame' for p in PASSES})


def energy_gate(rows):
    result = {}
    for channel in range(4):
        values = {k: v['mean_rgb'][channel] if channel < 3 else v['luminance'] for k, v in rows.items()}
        if not all(math.isfinite(v) for v in values.values()):
            raise ValueError('nonfinite pre-post energy')
        noise = abs(values['off-a'] - values['off-b'])
        for label in ON:
            reference = ((values['off-a']+values['off-b'])/2 if label == 'on-a' else values['off-b'])
            delta = values[label]-reference
            result[label+' '+['r', 'g', 'b', 'luminance'][channel]] = dict(
                delta=delta, off_repeat_noise=noise, pass_=abs(delta)<=noise)
    return result


def analyze(job, plan, game, runner):
    errors = gates.check(job['expect_every'], game, runner, plan)
    for field, text in [('expect', game), ('expect_runner', runner)]:
        errors += ['missing '+rx for rx in job[field] if not re.search(rx, text, re.M)]
    errors += ['forbidden '+rx for rx in job['forbid'] if re.search(rx, game, re.M|re.I)]
    lines = gates.stamped_lines(game)
    wins = {w['label']: w for w in gates.windows(runner, plan, lines)}

    def selected(label):
        w = wins[label]
        if not w['ready']:
            raise ValueError('unacknowledged window '+label)
        return [(t, s) for t, s in lines if w['applied'] <= t < w['end']]

    costs, floats, captures = {}, {}, {}
    for i, (label, _) in enumerate(STATES):
        window = label+'--static-a'
        picked = gates.complete_period_lines(selected(window), wins[window])
        costs[label] = {}
        for name, rx in TIMERS.items():
            values = [float(m[1]) for _, s in picked if (m := re.search(rx, s))]
            if len(values) < 2 or not all(math.isfinite(v) and v >= 0 for v in values):
                raise ValueError('missing/nonfinite complete '+label+' '+name+' GPU periods')
            costs[label][name] = values
        exports = [m[1].strip() for _, s in selected('energy-'+label)
                   if (m := re.search(r'\[trace\] frame \d+ written:.* -> ([^\r\n]+)', s))]
        if len(exports) != 1:
            raise ValueError(label+': require one float pre-post export')
        floats[label] = energy(exports[0])
        serial = job['candidate']['serial_base']+i
        paths = [m[1].strip() for _, s in selected('qa-'+label)
                 if (m := re.search(rf'\[pan\] burst {serial} complete:.*transport ok -> (.+)$', s))]
        if len(paths) != 1:
            raise ValueError(label+': require one rendered K8 burst')
        captures[label] = burst(Path(paths[0]))
    if len({tuple(v['dimensions']) for v in floats.values()}) != 1:
        errors.append('pre-post dimensions changed')
    energies = energy_gate(floats)
    errors += ['energy outside OFF-repeat noise: '+k for k, v in energies.items() if not v['pass_']]
    quality = {}
    for on, off in ((on, off) for on in ON for off in OFF):
        a, b = captures[on], captures[off]
        shape = a['codes'][0].shape[:2]
        if any(f.shape[:2] != shape or not np.isfinite(f).all() for f in a['codes']+b['codes']+a['albedo']+b['albedo']):
            raise ValueError('nonfinite/mismatched QA frames')
        shift = det.view_shift(b['albedo'][0], a['albedo'][0])
        if max(abs(shift[0]), abs(shift[1])) >= 1:
            errors.append('camera drift '+on+'/'+off)
        moving = det.animated(b['albedo']+a['albedo'], [], shape)
        q = det.temporal(a['codes'], b['codes'], moving=moving)
        q.pop('map', None)
        quality[on+'/'+off] = dict(shift=shift, qa=q)
        if not q['pass']:
            errors.append('QA flicker worse/inconclusive '+on+'/'+off)
    savings = {}
    med = lambda label, name: statistics.median(costs[label][name])
    for on in ON:
        savings[on] = {}
        for name in TIMERS:
            a, b = [med(off, name) for off in OFF]
            baseline = (a+b)/2 if on == 'on-a' else b
            saved = baseline-med(on, name)
            savings[on][name] = dict(off_ms=baseline, on_ms=med(on, name), saving_ms=saved, off_repeat_noise=abs(a-b))
            if name in ('GI', 'GI trace') and saved <= abs(a-b):
                errors.append(on+' '+name+' saving does not exceed OFF-repeat noise')
    return dict(status='FAIL' if errors else 'PASS', errors=errors, gpu_periods=costs,
                savings=savings, energy=energies, qa=quality)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for key in ('job', 'log', 'runner', 'out'):
        ap.add_argument('--'+key, required=True)
    a = ap.parse_args()
    try:
        job, plan, _ = flow.load_job(a.job)
        result = analyze(job, plan, Path(a.log).read_text(encoding='utf-8-sig', errors='replace'),
                         Path(a.runner).read_text(encoding='utf-8-sig', errors='replace'))
    except (OSError, ValueError, KeyError, TypeError) as e:
        result = dict(status='INVALID', errors=[str(e)])
    flow.write(a.out, result)
    print(json.dumps(dict(status=result['status'], errors=result['errors'])))
    return int(result['status'] != 'PASS')


if __name__ == '__main__':
    sys.exit(main())
