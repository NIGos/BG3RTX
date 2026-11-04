"""Read final receipts: reset-aligned GPU costs and float pre-post energy.

No game/queue access. --capture is the one energy export, never a PNG proxy.
"""
import argparse
import json
import math
from pathlib import Path
import re
import statistics
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts.queue_gates import stamped_lines, windows, complete_period_lines
from tools.live_batches import live_flow as flow

COSTS = {
    'RT': r'rt passes: GPU cost ([\d.]+) ms/frame',
    'GI': r'gi: GPU cost ([\d.]+) ms/frame',
    'GI trace': r'gi: trace pass .*GPU median ([\d.]+) ms',
    'local shadows': r'ls: local-light shadow pass GPU median ([\d.]+) ms',
    'reflections': r'rf: reflection pass GPU median ([\d.]+) ms',
    'water reflections': r'\[water\] reflections GPU ([\d.]+) ms median',
    'water compose': r'\[water\] compose GPU ([\d.]+) ms median',
    'water build': r'\[water\] build GPU ([\d.]+) ms median',
    'scene build': r'scene build GPU .*total median ([\d.]+) ms',
    'RR': r'gpu RR:.*= ([\d.]+) ms',
    'post chain': r'gpu: render-res chain ([\d.]+) ms/frame',
    'post grade': r'gpu:.*output-res grade ([\d.]+) ms/frame',
    'post temporal': r'gpu temporal: ([\d.]+) ms/frame',
}


def costs(log, runner, plan=None):
    lines = stamped_lines(Path(log).read_text(encoding='utf-8-sig', errors='replace'))
    run = Path(runner).read_text(encoding='utf-8-sig', errors='replace')
    if plan is None:
        labels = {int(i): label for i, label in re.findall(r'\[motion-ready\] phase=(\d+) label=(\S+)', run)}
        plan = [dict(label=labels.get(i, str(i))) for i in range(max(labels)+1)]
    out = {}
    for w in windows(run, plan, lines):
        label = w['label'].rsplit('--', 1)[-1]
        if label not in ('static-a', 'static-b', 'pan-q', 'pan-e', 'rotate-r', 'rotate-l', 'cost-hair'):
            continue
        if not w['ready']:
            raise ValueError('unacknowledged cost window: '+w['label'])
        picked = [(t, s) for t, s in lines if w['applied'] <= t < w['end']]
        picked = complete_period_lines(picked, w)
        row = {}
        for name, rx in COSTS.items():
            values = [float(m[1]) for s in picked if (m := re.search(rx, s))]
            if values:
                row[name] = dict(median_ms=statistics.median(values), min_ms=min(values),
                                 max_ms=max(values), samples=len(values))
        out[w['label']] = row
    return out


def energy(capture):
    cap = Path(capture)
    frame, dumps = flow.read(cap/'frame.json'), flow.read(cap/'dumps.json')
    # Slot 2 is scene C before post, not AC, bloom or the display image.
    rows = []
    for submit in frame['submits']:
        for cmd in submit['cmds']:
            events = cmd['events']
            for i, e in enumerate(events):
                if e.get('op') == 'pass' and e.get('tag') == 'rrpost':
                    end = next(x for x in events[i+1:] if x.get('op') == 'end')
                    rows += [dumps[k] for k in end.get('dumps', []) if k in dumps and dumps[k]['slot'] == 2]
    if len(rows) != 1 or rows[0].get('nonfinite') != 0:
        raise ValueError('expected one finite rrpost scene-colour dump')
    mean = rows[0]['mean'][:3]
    if len(mean) != 3 or not all(math.isfinite(x) for x in mean):
        raise ValueError('invalid scene float mean')
    return dict(mean_rgb=mean, luminance=sum(x*w for x, w in zip(mean, (.2126, .7152, .0722))),
                dimensions=[rows[0]['w'], rows[0]['h']], source=str(cap/'dumps.json'))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--job', required=True)
    ap.add_argument('--log', required=True)
    ap.add_argument('--runner', required=True)
    ap.add_argument('--capture', nargs='+')
    ap.add_argument('--baseline-capture', nargs='*', default=[])
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    job, plan, _ = flow.load_job(a.job)
    captures = a.capture or re.findall(r'\[trace\] frame \d+ written:.* -> ([^\r\n]+)',
        Path(a.log).read_text(encoding='utf-8-sig', errors='replace'))
    expected = sum(len(p.get('trace_at', [])) for p in plan)
    if len(captures) != expected:
        raise ValueError(f'expected {expected} energy exports, got {len(captures)}')
    result = dict(job=job['name'], gpu_costs=costs(a.log, a.runner, plan),
                  energy=[energy(p) for p in captures],
                  status='REVIEW_REQUIRED', visual='Inspect cutout edges / stock cards and face-neck shadows.')
    predicted = flow.read(Path(__file__).with_name('predicted_final.json'))
    result['cost_vs_reference'] = {}
    for label, passes in result['gpu_costs'].items():
        key = 'rel3' if 'dialogue' in job['name'] else ('rel2' if 'rel2' in label else 'rel1')
        reference = predicted['references'][key]['gpu_costs']
        baseline = reference.get(label.rsplit('--', 1)[-1], {})
        # Some historical phases ended before the core timers emitted a whole
        # period. Preserve that absence rather than borrowing a different view.
        result['cost_vs_reference'][label] = {name: dict(reference=key,
            delta_ms=value['median_ms']-baseline[name]['median_ms'],
            outside_observed_range=not baseline[name]['min_ms'] <= value['median_ms'] <= baseline[name]['max_ms'])
            for name, value in passes.items() if name in baseline}
    result['baseline_energy'] = [energy(p) for p in a.baseline_capture]
    if not a.baseline_capture:
        keys = ['rel3'] if 'dialogue' in job['name'] else ['rel1', 'rel2']
        result['baseline_energy'] = [predicted['references'][key]['energy'] for key in keys]
    result['signed_energy_delta'] = [dict(delta=n['luminance']-b['luminance'],
        percent=100*(n['luminance']/b['luminance']-1) if b['luminance'] else None)
        if b else dict(status='UNAVAILABLE: historical smoke exported no float energy')
        for n, b in zip(result['energy'], result['baseline_energy'])]
    result['energy_limitations'] = ('Historical scene comparisons are descriptive, never cross-run image A/B. '
        'No scene/profile-matched baseline or repeats means no energy/noise pass. '
        'Read predicted_final.json cost references; explain each signed change outside observed range.')
    flow.write(a.out, result)


if __name__ == '__main__':
    main()
