"""Presented-frame flicker of the water (fg_snap PNG sequences: what the display shows, generated frames included).

py -3 -m scripts.qa.fg_present_flicker <captures/fg_<stamp> dirs> --mask <rt_debug 21 PNG> --out <report.json>

pan_artifacts scores RENDERED frames; with DLSS-G (fg=2) every second PRESENTED frame is generated from the motion
vectors, and the water's are the bed's (it writes none). Per sequence of presents P_t (half-resolution PNGs written by
layer/streamline.cpp snap_present, real and generated alternating), the second difference
    r_t = P_t - (P_{t-1} + P_{t+1}) / 2
is ~0 for content that moves smoothly (a turn at constant speed) and large for anything that is on in one present and
off in the next. Reported per region (the water mask, the rest of the image) and per present parity (even / odd t:
one of them is the generated frames when FG runs): mean |r_t| and its 99th percentile, in UNORM8 codes of the
displayed image. FG implicated: the water's residual in one parity well above the other, with the rest of the image
symmetric, and above the FG-off sequence's water residual. Nothing is thresholded here: the comparison is between the
sequences of one run (FG on / off, keys on / off), the same turn protocol.
The mask (coverage_mask, the rt_debug 21 codes) is mapped onto the presents' content rectangle (the bounding box of
the pixels that are not black in every present: letterbox bars excluded).
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import numpy as np
from PIL import Image
from .pan_artifacts import coverage_mask

Y = np.array([.2126, .7152, .0722], dtype='f4')


def load_sequence(directory: Path):
    files = sorted(directory.glob('p*.png'), key=lambda p: int(p.stem[1:]))
    if len(files) < 5:
        raise ValueError(f'{directory}: {len(files)} presents (need 5)')
    return [np.asarray(Image.open(f).convert('RGB'), dtype='f4') @ Y for f in files], [f.name for f in files]


def content_rect(frames):
    lit = np.any(np.stack(frames) > 0.5, axis=0)
    ys, xs = np.nonzero(lit)
    if not len(ys):
        raise ValueError('every present is black')
    return int(ys.min()), int(ys.max()) + 1, int(xs.min()), int(xs.max()) + 1


def analyse(directory: Path, mask_rgb):
    frames, names = load_sequence(directory)
    y0, y1, x0, x1 = content_rect(frames)
    frames = [f[y0:y1, x0:x1] for f in frames]
    water = coverage_mask(mask_rgb, frames[0].shape) if mask_rgb is not None else np.zeros(frames[0].shape, bool)
    rest = ~water
    rows = {'water': {0: [], 1: []}, 'rest': {0: [], 1: []}}
    per_present = []
    for t in range(1, len(frames) - 1):
        r = np.abs(frames[t] - 0.5 * (frames[t - 1] + frames[t + 1]))
        w = r[water] if water.any() else np.zeros(1, 'f4')
        o = r[rest]
        rows['water'][t & 1].append(w)
        rows['rest'][t & 1].append(o)
        per_present.append({'present': names[t], 'water_mean': float(w.mean()), 'rest_mean': float(o.mean())})
    out = {'directory': str(directory.resolve()), 'presents': len(frames), 'content_rect': [y0, y1, x0, x1],
           'water_pixels': int(water.sum()), 'per_present': per_present}
    for region, by in rows.items():
        for parity, parts in by.items():
            v = np.concatenate(parts) if parts else np.zeros(1, 'f4')
            out[f'{region}_{"even" if parity == 0 else "odd"}'] = {'mean': float(v.mean()), 'p99': float(np.percentile(v, 99))}
    we, wo = out['water_even']['mean'], out['water_odd']['mean']
    re_, ro = out['rest_even']['mean'], out['rest_odd']['mean']
    out['water_parity_ratio'] = max(we, wo) / max(min(we, wo), 1e-9)
    out['rest_parity_ratio'] = max(re_, ro) / max(min(re_, ro), 1e-9)
    out['water_over_rest'] = (we + wo) / max(re_ + ro, 1e-9)
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('dirs', nargs='+', type=Path)
    ap.add_argument('--mask', type=Path, help='rt_debug 21 water-coverage PNG (the scene dump after the water pass)')
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args(argv)
    mask = np.asarray(Image.open(a.mask).convert('RGB')) if a.mask else None
    res = []
    for d in a.dirs:
        try:
            res.append(analyse(d, mask))
        except (ValueError, OSError) as e:
            res.append({'directory': str(d), 'error': str(e)})
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps(res, indent=2))
    for r in res:
        if 'error' in r:
            print(r['directory'], 'ERROR', r['error'])
        else:
            print(Path(r['directory']).name, f"water even/odd {r['water_even']['mean']:.3f}/{r['water_odd']['mean']:.3f}",
                  f"rest even/odd {r['rest_even']['mean']:.3f}/{r['rest_odd']['mean']:.3f}",
                  f"parity ratio water {r['water_parity_ratio']:.2f} rest {r['rest_parity_ratio']:.2f}",
                  f"water/rest {r['water_over_rest']:.2f}")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
