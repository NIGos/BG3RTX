"""Export reset-aligned scene/preset/window costs as optimization Markdown rows."""
import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.live_batches import live_flow as flow
from tools.live_batches.release_qual.analyze_final import costs

EXTRA = {
    'FG copy': r'\[sl\] gpu FG input copy: median ([\d.]+) ms',
    'emissive': r'\[em\] per build:.*GPU: build median ([\d.]+) ms',
    'CPU submit': r'\[watch\].*submit CPU ([\d.]+) ms',
    'driver': r'\[watch\].*pipelines \d+ \(([\d.]+) ms in driver\)',
}
PASSES = ['scene build', 'GI', 'GI trace', 'GI denoise', 'local shadows', 'reflections',
          'water build', 'water reflections', 'water compose', 'RR', 'post chain',
          'post grade', 'post temporal', 'FG copy', 'emissive', 'CPU submit', 'driver']


def disabled(name, state):
    if name.startswith('water '):
        return state.get('rt_water_geometry') == '0'
    if name == 'emissive':
        return state.get('rt_emissive') == '0'
    if name == 'post temporal':
        return state.get('rr_post_temporal') == '0'
    return False


def analyze(job, plan, measured):
    rows, errors = [], []
    states = {p['label']: s for p, s in zip(plan, flow.phase_states(job, plan))}
    for phase in plan:
        if not phase.get('frames'):
            continue
        label = phase['label']
        preset, motion = label.split('--')
        state = states[label]
        window = measured.get(label, {})
        for name in PASSES:
            value = window.get(name)
            units = 'ms/frame'
            if name == 'emissive':
                units = 'ms/build'
            if name in ('CPU submit', 'driver'):
                # Watch values are totals across the 120-frame interval, not
                # per-frame costs or pipeline-call averages.
                units = 'ms/120-frame period'
            status = ''
            if name == 'GI denoise':
                status = 'shared RR (no separate timer)'
                value = None
            elif disabled(name, state):
                status = 'disabled by preset'
                value = None
            elif value is None or value['samples'] < 2:
                status = 'MISSING' if value is None else 'INSUFFICIENT (< 2 periods)'
                # Water/emissive can have no work in this particular view.
                # Preserve absence; do not turn missing active work into zero.
                if name.startswith('water ') or name == 'emissive':
                    status += '; inspect pass participation'
                else:
                    errors.append(f'{label}: {name}: {status}')
            elif name == 'CPU submit' and value['max_ms'] == 0:
                status = 'INVALID: CPU timing unavailable (requires diagnostics build and cpu_timing=1)'
                errors.append(f'{label}: {status}')
            result = (f"{value['median_ms']:.3f} | {value['min_ms']:.3f}–{value['max_ms']:.3f} | {value['samples']}"
                      if value else '— | — | 0')
            rows.append(f"| {job['save']} | {preset} | {motion} | {name} | {result} | {units} | {status or 'measured'} |")
    header = ('| Scene | Preset | Window | Pass | Median | Range | Periods | Unit | Status |\n'
              '| --- | --- | --- | --- | ---: | --- | ---: | --- | --- |\n')
    return header+'\n'.join(rows)+'\n', errors


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--job', required=True)
    ap.add_argument('--log', required=True)
    ap.add_argument('--runner', required=True)
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    job, plan, _ = flow.load_job(a.job)
    # Streamline's 120-sample copy accumulator does not consume rt_stats_reset.
    # Its first line can straddle motion-ready; all other timers here do reset.
    table, errors = analyze(job, plan, costs(a.log, a.runner, plan, EXTRA, skip_first=('FG copy',)))
    Path(a.out).write_text(('INVALID: '+ '; '.join(errors)+'\n\n' if errors else '')+table, encoding='utf-8')
    print(f"{job['name']}: {'INVALID' if errors else 'REVIEW_REQUIRED'}; {len(errors)} missing cost observations; {a.out}")
    return int(bool(errors))


if __name__ == '__main__':
    sys.exit(main())
