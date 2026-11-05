"""Reset-aligned GPU receipts. Only missing evidence returns nonzero."""
import argparse
import json
import math
import re
import statistics
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT))
from tools.live_batches import live_flow as flow
from tools.live_batches.release_qual.analyze_final import costs, energy
from scripts import queue_gates as gates
from tools.m4_promotion_gate import t_quantile, ALPHA_FW
from tools.live_batches.opt_r5.compact_rf.make_job import STATES, TIMERS

LABELS = [label+'--static-a' for label, _ in STATES]


def read_energy(log, runner, plan):
    lines = gates.stamped_lines(Path(log).read_text(encoding='utf-8-sig', errors='replace'))
    windows = {w['label']: w for w in gates.windows(
        Path(runner).read_text(encoding='utf-8-sig', errors='replace'), plan, lines)}
    rows = {}
    for label, _ in STATES:
        w = windows['energy-'+label]
        if not w['ready']:
            raise ValueError('energy window not acknowledged: '+label)
        exports = [m[1].strip() for t, text in lines if w['applied'] <= t < w['end']
                   and (m := re.search(r'\[trace\] frame \d+ written:.* -> ([^\r\n]+)', text))]
        if len(exports) != 1:
            raise ValueError(label+': require one float pre-post export')
        rows[label] = energy(exports[0])
    return rows


def energy_gate(rows):
    # Adapt scene_holds' OFF-repeat Student band to two OFF replicates (df=1).
    # Eight two-sided checks (RGB+luminance, two pairs) share the existing family alpha.
    if len({tuple(row['dimensions']) for row in rows.values()}) != 1:
        raise ValueError('pre-post energy dimensions changed')
    result = {}
    for ch, name in enumerate(('r', 'g', 'b', 'luminance')):
        values = {label: row['mean_rgb'][ch] if ch < 3 else row['luminance']
                  for label, row in rows.items()}
        if not all(math.isfinite(v) for v in values.values()):
            raise ValueError('nonfinite pre-post energy')
        sigma = statistics.stdev([values['off-a'], values['off-b']])
        band = t_quantile(1-ALPHA_FW/16, 1)*sigma*math.sqrt(2)
        pairs = {on: dict(delta=values[on]-values[off], pass_=abs(values[on]-values[off]) <= band)
                 for off, on in [('off-a','on-a'), ('off-b','on-b')]}
        result[name] = dict(window_mean=values, sigma=sigma, band=band, pairs=pairs,
                            pass_=all(p['pass_'] for p in pairs.values()))
    return result


def analyze(measured, energies=None):
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
            preceding_off_delta_ms={LABELS[i]: med[LABELS[i]]-med[LABELS[i-1]] for i in (1, 3)},
            pair_verdict={LABELS[i]: 'SAVING' if med[LABELS[i-1]]-med[LABELS[i]] > spread else 'NO_SAVING' for i in (1, 3)},
            every_on_beats_every_off=all(o-n > spread for o in off for n in on),
            unchanged_within_off_spread=all(abs(n-sum(off)/2) <= spread for n in on))
    energy_result = {}
    try:
        if energies is None or set(energies) != {label for label, _ in STATES}:
            raise ValueError('missing four float pre-post energy receipts')
        energy_result = energy_gate(energies)
    except (ValueError, KeyError, TypeError, IndexError) as error:
        errors.append(str(error))
    energy_ok = bool(energy_result) and all(v['pass_'] for v in energy_result.values())
    decision = ('INVALID' if errors else 'ENERGY_CHANGE' if not energy_ok else 'SAVING' if passes['reflections']['every_on_beats_every_off'] else 'NO_SAVING')
    return dict(status=decision, errors=errors, gpu=passes, energy=energy_result, energy_windows=energies,
                scope='GPU cost plus pre-post float energy. Construction/SPIR-V exactness; no live image bit-equality claim.')


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    for key in ('job', 'log', 'runner', 'out'):
        ap.add_argument('--'+key, required=True)
    a = ap.parse_args(argv)
    try:
        job, plan, _ = flow.load_job(a.job)
        result = analyze(costs(a.log, a.runner, plan), read_energy(a.log, a.runner, plan))
        result['job'] = job['name']
    except (ValueError, OSError, KeyError, TypeError, IndexError) as error:
        result = dict(status='INVALID', errors=[str(error)])
    Path(a.out).write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(result['status']+': '+ '; '.join(result['errors']))
    return int(bool(result['errors']) or result['status'] == 'ENERGY_CHANGE')


if __name__ == '__main__':
    sys.exit(main())
