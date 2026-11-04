"""Split the retained source with shared timing/readiness. Never queues."""
import copy
import json
import tempfile
from pathlib import Path

BASE = Path(__file__).resolve().parent


def main():
    source = json.loads((BASE / 'seed_job.json').read_text())
    phases = json.loads((BASE / 'seed_plan.json').read_text())
    predictions = {'_rule': 'Prepared 2026-11-04 before live; no authorization. Bits judged independently.'}
    for group, bit in [('sparse', 1), ('cone', 2), ('order', 4)]:
        name = 'wo1-s8-hit-shade' if bit == 1 else f'wo1-s8-bit{bit}'
        plan = copy.deepcopy([p for p in phases if p['label'] in ('warmup', 'producer-sanity-warmup', f'prefetch-{group}', 'restore') or p['label'].startswith((f'shot-{group}-', f'cost-{group}-'))])
        labels = {p['label'] for p in plan}
        job = copy.deepcopy(source)
        job.update(name=name, timeout=840, max_minutes=15, purpose=f'Decide water hit cost bit {bit} independently: OFF/ON/OFF/ON energy, coverage, zero truncation and GPU cost.')
        job['args'][-1] = f'captures/live_batches/water_offscreen/plans/{name}.json'
        job['expect_every'] = [dict(g, windows=[w for w in g['windows'] if w in labels]) for g in job['expect_every'] if any(w in labels for w in g['windows'])]
        job['expect_every'].append(dict(name='water geometry coverage', select=r'\[water\] coverage \(', match=r'captured [1-9]\d* of [1-9]\d* water draws \(100\.0%\).*triangles [1-9]\d* of [1-9]\d* supported \(100\.0%\).*non-finite 0, collapsed 0', period_frames=120, min=1, windows=[p['label'] for p in plan if p['label'].startswith(('shot-', 'cost-'))]))
        job['expect_runner'] = [r'\[window-mode\].*visible_unfocused', '^Run ended: done', rf'\[motion-plan\] complete phases={len(plan)}\b']
        for i, phase in enumerate(plan):
            for repeat, _ in enumerate(phase.get('trace_at', [])):
                job['expect_runner'].append(rf'\[motion-trace\] phase={i} label={phase["label"]} repeat={repeat}')
        job['set'] = ','.join(s for s in job['set'].split(',') if not s.startswith(('rt_water_rf_edge=', 'rt_water_rr_hitdist=')))
        (BASE / 'jobs' / f'{name}.json').write_text(json.dumps(job, indent=2) + '\n')
        (BASE / 'plans' / f'{name}.json').write_text(json.dumps(plan, indent=2) + '\n')
        predictions[name] = {p['label']: dict(predicted=0.0, source='Same-run OFF-repeat noise and float encoding uncertainty; water-compose mean divided by exposure.', deciding_observable=f'bit {bit}: every ON energy, coverage, truncation and reflections GPU median/p90', pass_rule='Every ON within same-bit OFF energy and coverage repeat noise; invalid=0 and world truncated=0. Both ON cost medians and p90 improve beyond OFF-repeat spread; otherwise reject the bit. Missing observations INVALID.') for p in plan if p['label'].startswith(f'shot-{group}-on')}
    (BASE / 'predicted.json').write_text(json.dumps(predictions, indent=2) + '\n')


if __name__ == '__main__':
    main()
    from tools.live_batches.live_flow import load_job, tighten_files
    tighten_files(BASE, cache=Path(tempfile.gettempdir()) / 'bg3rtx-backlog-water-corpus.json')
    for path in (BASE / 'jobs').glob('*.json'):
        job, plan, plan_path = load_job(path)
        for phase in plan:
            if phase.get('wait_log_timeout') and phase['label'] != 'warmup':
                phase['wait_log_timeout'] = min(30, phase['wait_log_timeout'])
        plan_path.write_text(json.dumps(plan, indent=2) + '\n')
        job['timeout'] = 840
        path.write_text(json.dumps(job, indent=2) + '\n')
