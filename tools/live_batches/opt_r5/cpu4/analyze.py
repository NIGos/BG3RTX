"""CPU4 cost receipts; reuse the normalized CPU parser and reject missing timers."""
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT))
from tools.live_batches.opt_r3.cpu import analyze as base

EXTRA = {
    'capture pipeline lookup (nested)': r'capture detail over.*pipeline lookup ([\d.]+)',
    'capture cascade identity (nested)': r'capture detail over.*cascade identity ([\d.]+)',
    'capture bvh classify (nested)': r'capture detail over.*bvh classify ([\d.]+)',
    'capture alpha records (nested)': r'capture detail over.*alpha records ([\d.]+)',
    'capture material hooks (nested)': r'capture detail over.*material hooks ([\d.]+)',
    'dispatch tracking pre (driver excluded)': r'capture detail over.*dispatch tracking pre ([\d.]+)',
    'dispatch tracking post (driver excluded)': r'capture detail over.*dispatch tracking pre [\d.]+, post ([\d.]+)',
    'hit material G-buffer hooks (nested)': r'hit material over.*G-buffer material hooks ([\d.]+)',
    'hit material query (nested)': r'hit material over.*query \(lighting thread\) ([\d.]+)',
    'hit material key construction (nested)': r'hit material over.*draw keys ([\d.]+)',
    'hit material mutex waits (nested)': r'hit material over.*inside them:.*mutex waits ([\d.]+)',
    'hit material collect (nested)': r'hit material over.*collect ([\d.]+)',
    'capture prepass observe (attribution)': r'capture attribution over.*prepass observe ([\d.]+)',
    'capture cascade sets (attribution)': r'capture attribution over.*cascade sets ([\d.]+)',
    'capture draw plan (attribution)': r'capture attribution over.*draw plan ([\d.]+)',
    'capture alpha lookup (attribution)': r'capture attribution over.*alpha lookup ([\d.]+)',
    'capture opaque material (attribution)': r'capture attribution over.*opaque material ([\d.]+)',
    'capture bvh record (attribution)': r'capture attribution over.*bvh record ([\d.]+)',
    'capture xfb commands (attribution)': r'capture attribution over.*xfb commands ([\d.]+)',
    'capture cascade draw (attribution)': r'capture attribution over.*cascade draw ([\d.]+)',
    'capture alpha material (attribution)': r'capture attribution over.*alpha material ([\d.]+)',
    'capture emissive draw (attribution)': r'capture attribution over.*emissive draw ([\d.]+)',
}
base.CPU.update(EXTRA)


def missing_required(rows):
    labels = [x+'--static-a' for x in ('off-a', 'on-a', 'off-b', 'on-b')]
    required = tuple(EXTRA) + ('capture pre-draw', 'capture pass after (includes scene recording)',
                              'bvh per-draw BLAS keys', 'bvh TLAS instances')
    return [label+': missing '+name for label in labels for name in required
            if name not in rows.get(label, {}) or rows[label][name]['n'] < 2]



def cost_decision(rows):
    """Every ON window must beat both OFF windows beyond measured OFF spread."""
    errors = []
    labels = [x+'--static-a' for x in ('off-a', 'on-a', 'off-b', 'on-b')]
    for name in ('capture pre-draw', 'capture hook bodies (driver draw excluded)'):
        if any(name not in rows.get(label, {}) or rows[label][name]['n'] < 2 for label in labels):
            errors.append(name+': incomplete CPU evidence')
            continue
        offa, ona, offb, onb = [rows[label][name]['median'] for label in labels]
        spread = abs(offa-offb)
        for label, on in ((labels[1], ona), (labels[3], onb)):
            if min(offa-on, offb-on) <= spread:
                errors.append(f'{label}: {name} savings {offa-on:+.4f}/{offb-on:+.4f} ms '
                              f'do not exceed OFF repeat spread {spread:.4f} ms against both OFF windows')
    return ('NO_SAVING' if errors else 'KEPT_CPU_COST'), errors

def main():
    # Preserve the existing report and OFF-repeat comparison implementation.
    result = base.main()
    args = dict(zip(sys.argv[1::2], sys.argv[2::2]))
    rows = base.analyze(args['--log'], args['--runner'],
                        json.loads(Path(args['--plan']).read_text(encoding='utf-8-sig')))
    errors = missing_required(rows)
    decision, cost_errors = cost_decision(rows)
    if errors or result:
        decision = 'INVALID'
    with Path(args['--out']).open('a', encoding='utf-8') as out:
        out.write('\nCPU4 decision: '+decision+' (CPU cost only; live quality/energy unqualified).\n')
        for error in errors+cost_errors:
            out.write('- '+error+'\n')
    print(decision+': '+'; '.join(errors+cost_errors))
    return bool(result or errors or cost_errors)



if __name__ == '__main__':
    sys.exit(main())
