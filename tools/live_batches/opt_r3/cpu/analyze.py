"""Normalize existing CPU receipts by rendered presents; never add nested timers."""
import argparse
import json
from pathlib import Path
import re
import statistics
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT))
from scripts.queue_gates import stamped_lines, windows, complete_period_lines

CPU = {
    'capture total (includes scene recording and driver draw)': r'capture hooks over \d+ frames.*total ([\d.]+)',
    'capture driver draw (nested)': r'driver draw \(XFB\) ([\d.]+)',
    'capture pre-draw': r'pre-draw ([\d.]+)',
    'capture pass after (includes scene recording)': r'pass after ([\d.]+)',
    'draw bookkeeping': r'draw bookkeeping ([\d.]+)',
    'hit material prepass (nested)': r'hit material over.*prepass hooks ([\d.]+)',
    'scene bvh_build': r'scene build recording.*bvh_build ([\d.]+)',
    'scene alpha_commit': r'scene build recording.*alpha_commit \(records, set\) ([\d.]+)',
    'scene capture end to commit (includes bvh/alpha)': r'scene build recording.*capture end to commit ([\d.]+)',
    'bvh decide': r'bvh_build phases.*decide ([\d.]+)',
    'bvh per-draw BLAS keys': r'bvh_build phases.*per-draw BLAS keys ([\d.]+)',
    'bvh TLAS instances': r'bvh_build phases.*TLAS instances ([\d.]+)',
    'submit matrix copies (nested)': r'submit-time instance matrix copies: ([\d.]+)',
}
WATCH = re.compile(r'\[watch\] presents (\d+).*submit CPU ([\d.]+) ms \| descriptor tracker CPU ([\d.]+) ms \| alpha descriptors ([\d.]+) ms / (\d+) calls')


def summarize(values):
    return dict(median=statistics.median(values), min=min(values), max=max(values), n=len(values))


def analyze(log, runner, plan):
    lines = stamped_lines(Path(log).read_text(encoding='utf-8-sig', errors='replace'))
    run = Path(runner).read_text(encoding='utf-8-sig', errors='replace')
    result = {}
    for w in windows(run, plan, lines):
        if not plan[w['index']].get('frames'):
            continue
        if not w['ready']:
            raise ValueError('unacknowledged window '+w['label'])
        picked = complete_period_lines([(t, s) for t, s in lines if w['applied'] <= t < w['end']], w)
        row = {}
        for name, rx in CPU.items():
            values = [float(m[1]) for s in picked if (m := re.search(rx, s))]
            # These producers do not consume rt_stats_reset. Drop their first
            # receipt; 300-build accumulators need >=600 builds per window.
            if name.startswith(('scene ', 'bvh ', 'submit matrix')):
                values = values[1:]
            if values:
                row[name] = summarize(values)
        # Both numbers are means over the same frames/threads in ONE line.
        # This subtraction removes only the captured native driver draw call.
        bodies = []
        for line in picked:
            total = re.search(CPU['capture total (includes scene recording and driver draw)'], line)
            driver = re.search(CPU['capture driver draw (nested)'], line)
            if total and driver:
                bodies.append(float(total[1])-float(driver[1]))
        if bodies:
            row['capture hook bodies (driver draw excluded)'] = summarize(bodies)
        watches = [m for s in picked if (m := WATCH.search(s)) and int(m[1]) > 0]
        for i, name in enumerate(('submit inclusive (driver included)', 'descriptor tracker', 'alpha clone driver updates'), 2):
            values = [float(m[i])/int(m[1]) for m in watches]
            if values:
                row[name] = summarize(values)
        if watches:
            row['alpha clone calls/frame'] = summarize([int(m[5])/int(m[1]) for m in watches])
        result[w['label']] = row
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for key in ('log', 'runner', 'plan', 'out'):
        ap.add_argument('--'+key, required=True)
    a = ap.parse_args()
    result = analyze(a.log, a.runner, json.loads(Path(a.plan).read_text(encoding='utf-8-sig')))
    text = '# CPU receipts\n\nms/frame, except scene/bvh/matrix timers (ms/build) and alpha calls/frame. '
    text += 'Do not sum nested rows. Submit includes the driver; capture includes scene recording.\n\n'
    text += '| Window | CPU timer | Median | Range | Receipts |\n|---|---|---:|---:|---:|\n'
    for label, row in result.items():
        for name, v in row.items():
            text += f"| {label} | {name} | {v['median']:.4f} | {v['min']:.4f}-{v['max']:.4f} | {v['n']} |\n"
    labels = [x+'--static-a' for x in ('off-a', 'on-a', 'off-b', 'on-b')]
    if all(label in result for label in labels):
        text += '\nSigned ON deltas against the median of both OFF medians; negative saves CPU.\n\n'
        text += '| Timer | OFF repeat spread | ON a delta | ON b delta |\n|---|---:|---:|---:|\n'
        for name in result[labels[0]]:
            if not all(name in result[label] for label in labels):
                continue
            offa, ona, offb, onb = [result[label][name]['median'] for label in labels]
            baseline = (offa+offb)/2
            text += f'| {name} | {abs(offa-offb):.4f} | {ona-baseline:+.4f} | {onb-baseline:+.4f} |\n'
    Path(a.out).write_text(text, encoding='utf-8')
    # This lane's invariant is exact CPU decisions. Live is a cost receipt,
    # never a visual/energy qualification or a claim of exclusive layer time.
    required = ('capture total (includes scene recording and driver draw)', 'bvh per-draw BLAS keys',
                'submit inclusive (driver included)', 'descriptor tracker', 'alpha clone driver updates')
    errors = [label+': missing '+key for label, row in result.items() for key in required
              if key not in row or row[key]['n'] < 2]
    if not result:
        errors.append('no measured windows')
    print('INVALID: '+ '; '.join(errors) if errors else 'REVIEW_REQUIRED: '+a.out)
    return bool(errors)


if __name__ == '__main__':
    sys.exit(main())
