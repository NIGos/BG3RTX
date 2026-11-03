"""Validate every wsf1 post-water float shot; report model energy without an equal-mean gate."""
import argparse
import json
import math
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.m4_promotion_gate import Run
from tools.m5_promotion_gate import traces_of


def post_colour(cap):
    dumps = json.loads((cap / 'dumps.json').read_text(encoding='utf-8'))
    scene = [d for d in dumps.values() if str(d.get('pass_fs', '')).startswith('5c3fdabc')
             and d.get('slot') == 0 and d.get('mean')]
    if not scene:
        raise ValueError('missing render-resolution scene dimensions')
    shape = {(d.get('w'), d.get('h'), d.get('fmt')) for d in scene}
    post = [d for d in dumps.values() if str(d.get('pass_fs', '')).startswith('00000000')
            and d.get('slot') == 0 and (d.get('w'), d.get('h'), d.get('fmt')) in shape]
    if not post:
        raise ValueError('missing post-water RR float colour')
    values = []
    for d in post:
        rgb = d.get('mean', [])[:3]
        if len(rgb) != 3 or not all(math.isfinite(v) and v >= 0 for v in rgb):
            raise ValueError('invalid post-water float mean')
        if 'nonfinite' not in d or d['nonfinite'] != 0:
            raise ValueError('missing/nonzero post-water nonfinite count')
        values.append(sum(v * w for v, w in zip(rgb, (.2126, .7152, .0722))))
    return values[-1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    args = parser.parse_args()
    run = Run(args.log)
    if run.failed_drift():
        raise ValueError(run.drift)
    for i in range(4):
        label = f'partition-static-{i}'
        traces = traces_of(run, label)
        if len(traces) < 2:
            raise ValueError(f'{label}: fewer than two float shots')
        values = []
        for frame, cap in traces:
            exposure = run.pre_exposure(frame)
            if exposure is None or not math.isfinite(exposure) or exposure <= 0:
                raise ValueError(f'{label}: missing/invalid lighting exposure')
            values.append(post_colour(cap) / exposure)
        print(f'{label}: post-water float luminance/exposure {values}')
    print('Float evidence valid; shading-model energy may change. Image quality remains reviewable, not auto-qualified.')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (ValueError, OSError, KeyError, TypeError) as error:
        print(f'INVALID: {error}')
        sys.exit(1)
