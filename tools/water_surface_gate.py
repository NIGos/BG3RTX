"""Validate every wsf1 post-water float shot; report model energy without an equal-mean gate."""
import argparse
from array import array
import json
import math
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.m4_promotion_gate import Run, water_fs_prefixes
from tools.m5_promotion_gate import traces_of

# Half-open boxes in the fixed 1280 x 533 S8 preview, from wsf1 043141 review.
ROIS = {'pool_rim': (490, 75, 825, 285), 'open_water': (0, 75, 245, 340)}


def regional_colour(cap, rois=ROIS, preview_size=(1280, 533)):
    """Read only explicitly paired post-water float previews; never PNG radiance."""
    dumps = json.loads((cap / 'dumps.json').read_text(encoding='utf-8'))
    scene = [d for d in dumps.values() if str(d.get('pass_fs', '')).startswith('5c3fdabc')
             and d.get('slot') == 0]
    if not scene:
        raise ValueError('missing regional scene image')
    image = scene[-1]['image']
    frame = json.loads((cap / 'frame.json').read_text(encoding='utf-8'))
    paired = []
    for submit in frame.get('submits', []):
        for cmd in submit.get('cmds', []):
            fs, before = set(), []
            for event in cmd.get('events', []):
                op = event.get('op')
                if op == 'pass':
                    fs, before = set(), []
                elif op == 'draw':
                    fs.add(str(event.get('fs', ''))[:8])
                elif op == 'end':
                    before = [dumps[k] for k in event.get('dumps', []) if k in dumps
                              and dumps[k].get('image') == image and dumps[k].get('slot') == 0]
                elif op == 'post_core' and fs & water_fs_prefixes() and len(before) == 1:
                    after = [dumps[k] for k in event.get('dumps', []) if k in dumps
                             and dumps[k].get('image') == image and dumps[k].get('slot') == 0]
                    if (len(after) != 1 or 'pass_ordinal' not in before[0]
                            or any(after[0].get(k) != before[0].get(k)
                                   for k in ('pass_ordinal', 'pass_fs', 'w', 'h', 'fmt'))):
                        raise ValueError('missing/mismatched paired post-water region dump')
                    paired.extend(after)
    if not paired:
        raise ValueError('missing explicit post-water region pairing')
    d = paired[-1]
    preview_width = preview_size[0]
    w, h = min(d['w'], preview_width), d['h'] * min(d['w'], preview_width) // d['w']
    if (w, h) != preview_size:
        raise ValueError('post-water preview shape differs from S8 ROI framing')
    source = cap / Path(d['png']).with_name(Path(d['png']).stem + '_p.f32')
    if source.stat().st_size != w * h * 16:
        raise ValueError('missing/truncated or wrong-size post-water float preview')
    data = array('f')
    with source.open('rb') as stream:
        data.fromfile(stream, w * h * 4)
    if sys.byteorder != 'little':
        data.byteswap()
    result = {}
    for name, (x0, y0, x1, y1) in rois.items():
        if not (0 <= x0 < x1 <= w and 0 <= y0 < y1 <= h):
            raise ValueError('regional float ROI outside preview')
        sums = [0., 0., 0.]
        for y in range(y0, y1):
            for x in range(x0, x1):
                rgb = data[(y*w+x)*4:(y*w+x)*4+3]
                if not all(math.isfinite(v) and v >= 0 for v in rgb):
                    raise ValueError('invalid regional post-water float pixel')
                for c in range(3):
                    sums[c] += rgb[c]
        pixels = (x1-x0)*(y1-y0)
        result[name] = dict(luminance=sum(v*c for v,c in zip(sums, (.2126,.7152,.0722))) / pixels,
                            pixels=pixels, box=[x0,y0,x1,y1], source=str(source),
                            preview_size=[w,h], render_size=[d['w'],d['h']],
                            sampling='render texel floor(preview coordinate * render size / preview size)')
    return result


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
        regions = []
        for frame, cap in traces:
            exposure = run.pre_exposure(frame)
            if exposure is None or not math.isfinite(exposure) or exposure <= 0:
                raise ValueError(f'{label}: missing/invalid lighting exposure')
            values.append(post_colour(cap) / exposure)
            regional = regional_colour(cap)
            for row in regional.values():
                row['luminance_per_exposure'] = row['luminance'] / exposure
            regions.append(regional)
        print(f'{label}: post-water float luminance/exposure {values}')
        print(f'{label}: paired post-water regions {json.dumps(regions)}')
    print('Float evidence valid; shading-model energy may change. Image quality remains reviewable, not auto-qualified.')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (ValueError, OSError, KeyError, TypeError) as error:
        print(f'INVALID: {error}')
        sys.exit(1)
