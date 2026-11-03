"""Same-run edge attribution; offline, no game control.

python tools/live_batches/edge_flicker/analyze.py --game-log RUN.bg3rtx.log
    --runner-log RUN.runner.log --out report.json [--capture-root relocated-captures]
Requires HDR10 PQ captures: native tonemap signature d25b8086. Half-size
fg_snap PNGs cannot resolve every full-resolution hair strand.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import re
import sys

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from scripts import queue_gates as gates
from scripts.qa import detectors as det, imgops as im
from scripts.qa.pan_artifacts import decode
from tools.live_batches.dialogue_shadows.pair_registration import registration
from tools.live_batches.edge_flicker.make_job import WINDOWS

HERE = Path(__file__).resolve().parent
Y = np.array([.2627, .678, .0593], dtype='f4')


def resize_mask(mask, shape):
    return np.asarray(Image.fromarray(mask.astype('uint8')).resize(
        (shape[1], shape[0]), Image.Resampling.NEAREST), dtype=bool)


def masks(depths, materials):
    """One fixed baseline mask: silhouettes plus hair; skin interior control.

    Compare each adjacent depth pair relative to its local depth. The sum of
    the two outward slopes bounds a smooth turn of the surface; unlike the old
    second-difference > epsilon test it does not call any curvature an edge.
    The remaining threshold is six depth conversion intervals: reversed D24
    near=0.1 -> R32F, exactly depth_prec in shaders/rt/depth_tol.hlsli.
    One half-grid pixel covers the sampling footprint; no brightness threshold.
    """
    char = np.zeros(depths[0].shape, bool)
    hair = char.copy()
    edge = char.copy()
    for m in materials:
        cls = np.rint(m[..., 3]*255).astype('uint8') & 7
        char |= np.isin(cls, [3, 5])
        hair |= cls == 5
    for d in depths:
        if not np.isfinite(d).all():
            raise ValueError('nonfinite depth')
        for axis in (0, 1):
            a, b = np.roll(d, 1, axis), np.roll(d, -1, axis)
            r = np.roll(d, -2, axis)
            local = np.maximum.reduce([np.abs(a), np.abs(b), np.abs(d), np.abs(r)])
            local = np.maximum(local, np.finfo('f4').tiny)
            # All terms are dimensionless relative to the same local depth.
            step = np.abs(d-b)/local
            slope = (np.abs(d-a)+np.abs(r-b))/local
            precision = sum(np.abs(v)**2 * (2**-25/.1) + np.abs(v)*2**-24
                            for v in (a, d, d, b, b, r))/local
            pair_edge = step > slope + precision
            edge |= pair_edge | np.roll(pair_edge, 1, axis)
    edge[:2, :] = edge[-2:, :] = False
    edge[:, :2] = edge[:, -2:] = False
    edge = (im.dilate(edge, 1) & im.dilate(char, 1)) | hair
    interior = im.erode(char & ~hair & ~edge, 2)
    if not edge.any() or not interior.any():
        raise ValueError('no character edge or interior support (classes 3/5)')
    return edge, interior


def burst(directory):
    meta = json.loads((directory/'burst.json').read_text(encoding='utf-8'))
    if not (meta.get('valid_transport') and meta.get('edge_only') and
            meta.get('K') == 8 and meta.get('sample_stride') == 2):
        raise ValueError(f'{directory}: invalid K8 edge transport')
    by = {}
    for e in meta['samples']:
        if e['submit_frame'] == 2**64-1 or not e['command_recording']:
            raise ValueError('unsubmitted sample')
        f = by.setdefault(e['record_frame'], {})
        if e['name'] in f:
            raise ValueError('duplicate sample')
        f[e['name']] = decode(directory/e['file'], e)
    ids = sorted(by)
    if len(ids) != 8 or ids != list(range(ids[0], ids[0]+8)):
        raise ValueError('rendered frames not eight consecutive record frames')
    for i, f in enumerate(ids):
        expected = {'depth', 'final', 'material', 'albedo'} if i in (0, 7) else {'depth', 'final'}
        if set(by[f]) != expected:
            raise ValueError(f'incomplete frame {f}')
    return dict(codes=[by[f]['final'][..., :3] for f in ids],
                depth=[by[f]['depth'][..., 0] for f in ids],
                material=[by[f]['material'] for f in (ids[0], ids[-1])],
                albedo=[by[f]['albedo'][..., :3] for f in (ids[0], ids[-1])])


def presents(directory):
    files = sorted(directory.glob('p*.png'), key=lambda p: int(p.stem[1:]))
    ids = [int(p.stem[1:]) for p in files]
    if len(ids) != 16 or ids != list(range(ids[0], ids[0]+16)):
        raise ValueError(f'{directory}: need 16 consecutive presented frames')
    return [np.asarray(Image.open(p).convert('RGB'), dtype='f4')/255 for p in files]


def registered_run(anchor, frames):
    """Retain one consecutive registered run; cuts never bridge a temporal gap.

    Rendered frames use lighting-independent depth. Presented frames have only
    PQ beauty; phase correlation rejects translations without warping colours.
    A global exposure/brightness change retains its phase correlation location.
    """
    def structure(frame):
        grey = frame.mean(-1) if frame.ndim == 3 else frame
        dy, dx = np.gradient(grey.astype('f8'))
        return np.hypot(dx, dy)
    anchor = structure(anchor)
    regs = []
    for frame in frames:
        try:
            regs.append(registration(anchor, structure(frame)))
        except ValueError as error:
            regs.append(dict(valid=False, error=str(error)))
    runs, current = [], []
    for i, r in enumerate(regs):
        if r['valid']:
            current.append(i)
        elif current:
            runs.append(current)
            current = []
    if current:
        runs.append(current)
    keep = max(runs, key=len, default=[])
    if len(keep) < 4:
        raise ValueError('dialogue shot moved: fewer than four consecutive registered frames')
    return keep, dict(frames=regs, included=keep,
                     excluded=[i for i in range(len(frames)) if i not in keep])


def registered_endpoints(values, keep):
    endpoints = [values[j] for j, frame in enumerate((0, 7)) if frame in keep]
    if not endpoints:
        raise ValueError('registered run has no material/albedo endpoint support')
    return endpoints


def energy(codes, mask):
    y = np.stack([c @ Y for c in codes])
    if not np.isfinite(y).all() or not mask.any():
        raise ValueError('nonfinite colour or empty region')
    variance = y.var(axis=0, ddof=1)
    residual = y[1:-1] - (y[:-2]+y[2:])/2
    return dict(pixels=int(mask.sum()), variance=float(variance[mask].mean()),
                mean_code=float(y[:, mask].mean()),
                residual_even=float(np.abs(residual[1::2, mask]).mean()),
                residual_odd=float(np.abs(residual[::2, mask]).mean()))


def score(codes, references, edge, interior):
    shape = codes[0].shape[:2]
    if any(c.shape != codes[0].shape for c in codes):
        raise ValueError('frame dimensions changed')
    out = {}
    for name, mask in [('edge', edge), ('interior', interior)]:
        mask = resize_mask(mask, shape)
        out[name] = energy(codes, mask)
        # Reuse the QA statistical test. Thin masks can be INCONCLUSIVE because
        # they cannot hold its 81-pixel footprint; never reinterpret as a pass.
        tests = []
        for ref in references:
            if ref[0].shape != codes[0].shape:
                raise ValueError('baseline framing dimensions differ')
            result = det.temporal(ref, codes, moving=~mask)
            result.pop('map', None)
            tests.append(result)
        out[name]['baseline_excess_qa'] = tests
    return out


def rank(rows, domain):
    baseline = [rows[k][domain]['edge']['variance'] for k in ('base-a', 'base-b')]
    floor, spread = min(baseline), abs(baseline[0]-baseline[1])
    ranking = []
    for label, _ in WINDOWS[1:-1]:
        value = rows[label][domain]['edge']['variance']
        ranking.append(dict(toggle=label, edge_variance=value, reduction=floor-value,
                            beyond_repeat_spread=floor-value > spread))
    ranking.sort(key=lambda x: -x['reduction'])
    return dict(baseline_variances=baseline, repeat_spread=spread, ranking=ranking,
                largest_observed_reduction=ranking[0]['toggle'],
                candidate=ranking[0]['toggle'] if ranking[0]['beyond_repeat_spread'] else None)


def analyze(game_text, runner_text, resolve):
    job = json.loads((HERE/'jobs/ef1-dialogue3-attribution.json').read_text())
    plan = json.loads((HERE/'plans/ef1.json').read_text())
    failures = gates.check(job['expect_every'], game_text, runner_text, plan)
    for pattern in job['expect']:
        if not re.search(pattern, game_text, re.M):
            failures.append('missing game expectation: '+pattern)
    for pattern in job['expect_runner']:
        if not re.search(pattern, runner_text, re.M):
            failures.append('missing runner expectation: '+pattern)
    for pattern in job['forbid']:
        if re.search(pattern, game_text, re.M):
            failures.append('forbidden: '+pattern)
    if failures:
        raise ValueError('; '.join(failures))
    lines = gates.stamped_lines(game_text)
    wins = gates.windows(runner_text, plan, lines)
    paths, evidence = {}, {}
    for label, _ in WINDOWS:
        paths[label] = {}
        for kind in ('rendered', 'presented'):
            win = next(w for w in wins if w['label'] == kind+'-'+label)
            selected = [line for t, line in lines if win['applied'] <= t < win['end']]
            pattern = (r'\[pan\] burst .*transport ok -> (.+)$' if kind == 'rendered'
                       else r'\[sl\] capturing 16 presented frames to (.+)$')
            found = [m.group(1).strip() for line in selected if (m := re.search(pattern, line))]
            if len(found) != 1:
                raise ValueError(f'{label}/{kind}: expected one capture path, got {found}')
            paths[label][kind] = resolve(found[0])
            if kind == 'presented' and not any(re.search(r'\[sl\] edge capture format 64,', l) for l in selected):
                raise ValueError(label+': presented encoding is not confirmed HDR10 RGB10A2 (format 64)')
            evidence[kind+'-'+label] = [l for l in selected if '[watch]' in l or
                                       'DLSS-G frame' in l or '[pace]' in l]
    all_paths = [p for row in paths.values() for p in row.values()]
    if len(set(all_paths)) != 2*len(WINDOWS):
        raise ValueError('capture path reused across windows')
    bases = [burst(paths[label]['rendered']) for label in ('base-a', 'base-b')]
    depth_anchor = bases[0]['depth'][0]
    for b in bases:
        keep, _ = registered_run(depth_anchor, b['depth'])
        for field in ('codes', 'depth'):
            b[field] = [b[field][i] for i in keep]
        # Material/albedo exist only at original endpoints; discard endpoints
        # outside the registered run rather than importing a cut's classes.
        for field in ('material', 'albedo'):
            b[field] = registered_endpoints(b[field], keep)
    edge, interior = masks([d for b in bases for d in b['depth']],
                           [m for b in bases for m in b['material']])
    ref_present = [presents(paths[label]['presented']) for label in ('base-a', 'base-b')]
    present_anchor = ref_present[0][0]
    ref_present = [[frames[i] for i in registered_run(present_anchor, frames)[0]]
                   for frames in ref_present]
    refs = [b['codes'] for b in bases]
    rows = {}
    for label, _ in WINDOWS:
        print('analyzing '+label, flush=True)
        b = burst(paths[label]['rendered'])
        keep, rendered_regs = registered_run(depth_anchor, b['depth'])
        for field in ('codes', 'depth'):
            b[field] = [b[field][i] for i in keep]
        presented = presents(paths[label]['presented'])
        shown, present_regs = registered_run(present_anchor, presented)
        rows[label] = dict(registration=dict(rendered=rendered_regs, presented=present_regs),
                           rendered=score(b['codes'], refs, edge, interior),
                           presented=score([presented[i] for i in shown], ref_present, edge, interior),
                           depth_edge_variance=float(np.stack(b['depth']).var(0, ddof=1)[edge].mean()))
        albedos = [b['albedo'][j] for j, frame in enumerate((0, 7)) if frame in keep]
        moving = det.animated(albedos, [], edge.shape) if len(albedos) > 1 else None
        rows[label]['animated_edge_fraction'] = float(moving[edge].mean()) if moving is not None else None
        depth_var = np.stack(b['depth']).var(0, ddof=1)
        colour_var = np.stack([c @ Y for c in b['codes']]).var(0, ddof=1)
        # Spatial association, not a proof of DoF: depth and colour are captured
        # in different passes of each record frame, without motion compensation.
        h, w = colour_var.shape
        fitted = depth_var[(np.arange(h)*depth_var.shape[0]//h)[:, None],
                           np.arange(w)*depth_var.shape[1]//w]
        support = resize_mask(edge, (h, w))
        dv, cv = fitted[support], colour_var[support]
        rows[label]['depth_colour_variance_correlation'] = (
            float(np.corrcoef(dv, cv)[0, 1]) if dv.std() > 0 and cv.std() > 0 else None)
    return dict(status='ATTRIBUTION_ONLY', quality_pass=False, windows=rows,
                rendered=rank(rows, 'rendered'), presented=rank(rows, 'presented'),
                rate_evidence=evidence, capture_paths={k: {d: str(p) for d, p in v.items()} for k,v in paths.items()},
                limits=['PQ code variance, not float pre-post energy; no quality/brightness claim.',
                        'Presented PNG variance includes UI, frame pacing and base-FPS changes.',
                        'Parity is unlabeled; generated-frame identity is not inferred.',
                        'Half-size nearest samples can miss a full-resolution strand.',
                        'Per-frame depth/PQ registration excludes cuts; rotation/zoom or animation may remain.',
                        'Class 3/5 support covers skin/hair, not unclassified armour silhouette.',
                        'RR-off changes RT denoising too; post-late does not disable DoF.',
                        'Largest reduction is a candidate, never proof of one root cause.'])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--game-log', type=Path, required=True)
    p.add_argument('--runner-log', type=Path, required=True)
    p.add_argument('--capture-root', type=Path)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    def resolve(path):
        return a.capture_root / path.replace('\\', '/').split('/')[-1] if a.capture_root else Path(path)
    try:
        report = analyze(a.game_log.read_text(errors='replace'),
                         a.runner_log.read_text(errors='replace'), resolve)
    except (ValueError, OSError, KeyError, StopIteration) as error:
        report = dict(status='INVALID', quality_pass=False, error=str(error))
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps(report, indent=2, allow_nan=False)+'\n', encoding='utf-8')
    print(report['status'])
    return 1 if report['status'] == 'INVALID' else 0


if __name__ == '__main__':
    raise SystemExit(main())
