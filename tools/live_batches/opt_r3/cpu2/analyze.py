"""CPU2 cost receipts; reuse the normalized CPU parser and reject missing timers."""
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
}
base.CPU.update(EXTRA)


def missing_required(rows):
    labels = [x+'--static-a' for x in ('off-a', 'on-a', 'off-b', 'on-b')]
    required = tuple(EXTRA) + ('capture pre-draw', 'capture pass after (includes scene recording)',
                              'bvh per-draw BLAS keys', 'bvh TLAS instances')
    return [label+': missing '+name for label in labels for name in required
            if name not in rows.get(label, {}) or rows[label][name]['n'] < 2]


def main():
    # Preserve the existing report and OFF-repeat comparison implementation.
    result = base.main()
    args = dict(zip(sys.argv[1::2], sys.argv[2::2]))
    rows = base.analyze(args['--log'], args['--runner'],
                        json.loads(Path(args['--plan']).read_text(encoding='utf-8-sig')))
    errors = missing_required(rows)
    if errors:
        print('INVALID CPU2: '+'; '.join(errors))
    return bool(result or errors)


if __name__ == '__main__':
    sys.exit(main())
