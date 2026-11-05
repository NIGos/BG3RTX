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


def burst(directory, extras=()):
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
        f[e['name']] = None if e['name'] in extras else decode(directory/e['file'], e)
    ids = sorted(by)
    if len(ids) != 8 or ids != list(range(ids[0], ids[0]+8)):
        raise ValueError('rendered frames not eight consecutive record frames')
    for i, f in enumerate(ids):
        expected = {'depth', 'final', 'material', 'albedo'} if i in (0, 7) else {'depth', 'final'}
        if set(by[f]) - set(extras) != expected:
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


# Dialogue3 subtitle region in the 1920x800 presented capture. Normalize the
# top of the region to the current capture height (rendered bursts are half-size).
SUBTITLE_REGION = (0, 590, 1920, 800)
DIALOGUE_ROIS = dict(hair_box=(640, 20, 880, 330),
                     rigid_control=(720, 380, 900, 560))
SPARKLE_CODE = .02  # Scratch metric: ~5 eight-bit PQ codes, not a quality gate.


def registration_structure(frame):
    stop = round(frame.shape[0] * SUBTITLE_REGION[1] / SUBTITLE_REGION[3])
    scene = frame[:stop]
    grey = scene.mean(-1) if scene.ndim == 3 else scene
    dy, dx = np.gradient(grey.astype('f8'))
    return np.hypot(dx, dy)


def registered_run(anchor, frames, config=None):
    """Register each sub-window frame; translation alone never rejects a window."""
    from tools.live_batches.edge_flicker.drift import register_window
    return register_window(anchor, frames, config)


def aligned_frames(frames, report):
    from tools.live_batches.edge_flicker.drift import translate
    return [translate(frames[i], tuple(-v for v in report['frames'][i]['shift_px']))
            for i in report['included']]


def shifted_mask(mask, shift):
    """Integer mask translation with clipped borders, never toroidal wrapping."""
    dy, dx = shift
    h, w = mask.shape
    out = np.zeros_like(mask)
    sy0, sy1 = max(0, -dy), min(h, h-dy)
    sx0, sx1 = max(0, -dx), min(w, w-dx)
    if sy1 > sy0 and sx1 > sx0:
        out[sy0+dy:sy1+dy, sx0+dx:sx1+dx] = mask[sy0:sy1, sx0:sx1]
    return out


def scaled_shift(shift, source_shape, target_shape):
    return tuple(round(v*t/s) for v, s, t in zip(shift, source_shape, target_shape))


def dialogue_metrics(codes, shift):
    shape = codes[0].shape[:2]
    h, w = shape
    out = {}
    for name, (x0, y0, x1, y1) in DIALOGUE_ROIS.items():
        mask = np.zeros(shape, bool)
        mask[round(y0*h/800):round(y1*h/800),
             round(x0*w/1920):round(x1*w/1920)] = True
        mask = shifted_mask(mask, shift)
        out[name] = energy(codes, mask)
    return out


def registered_endpoints(values, keep):
    endpoints = [values[j] for j, frame in enumerate((0, 7)) if frame in keep]
    if not endpoints:
        raise ValueError('registered run has no material/albedo endpoint support')
    return endpoints


def energy(codes, mask):
    y = np.stack([c @ Y for c in codes])
    mask = mask & np.isfinite(y).all(axis=0)
    if not mask.any():
        raise ValueError('nonfinite colour or empty region')
    variance = y.var(axis=0, ddof=1)
    residual = y[1:-1] - (y[:-2]+y[2:])/2
    return dict(pixels=int(mask.sum()), variance=float(variance[mask].mean()),
                mean_code=float(y[:, mask].mean()),
                residual=float(np.abs(residual[:, mask]).mean()),
                residual_x1000=float(np.abs(residual[:, mask]).mean()*1000),
                sparkle_percent=float((np.abs(residual[:, mask]) > SPARKLE_CODE).mean()*100),
                sparkle_threshold_code=SPARKLE_CODE,
                residual_even=float(np.abs(residual[1::2, mask]).mean()),
                residual_odd=float(np.abs(residual[::2, mask]).mean()))


def score(codes, references, edge, interior, shift=(0, 0)):
    shape = codes[0].shape[:2]
    if any(c.shape != codes[0].shape for c in codes):
        raise ValueError('frame dimensions changed')
    out = {}
    for name, mask in [('edge', edge), ('interior', interior)]:
        mask = shifted_mask(resize_mask(mask, shape), shift)
        out[name] = energy(codes, mask)
        # Reuse the QA statistical test. Thin masks can be INCONCLUSIVE because
        # they cannot hold its 81-pixel footprint; never reinterpret as a pass.
        tests = []
        for ref in references:
            if ref[0].shape != codes[0].shape:
                raise ValueError('baseline framing dimensions differ')
            result = (det.temporal(ref, codes, moving=~mask) if tuple(shift) == (0, 0) and all(np.isfinite(f).all() for f in ref+codes) else
                      dict(status='INCONCLUSIVE', reason='cross-window translation; use local ROI residual'))
            result.pop('map', None)
            tests.append(result)
        out[name]['baseline_excess_qa'] = tests
    return out


def rank(rows, domain):
    baseline = [row[domain]['edge']['variance'] for label, row in rows.items()
                if label.startswith('base-')]
    floor, spread = min(baseline), max(baseline)-min(baseline)
    ranking = []
    for label in rows:
        if label.startswith('base-'):
            continue
        value = rows[label][domain]['edge']['variance']
        ranking.append(dict(toggle=label, edge_variance=value, reduction=floor-value,
                            beyond_repeat_spread=floor-value > spread))
    ranking.sort(key=lambda x: -x['reduction'])
    return dict(baseline_variances=baseline, repeat_spread=spread, ranking=ranking,
                largest_observed_reduction=ranking[0]['toggle'],
                candidate=ranking[0]['toggle'] if ranking[0]['beyond_repeat_spread'] else None)


def roi_comparison(rows, domain):
    """Report local hair/rigid metrics against every baseline repeat.

    These are PQ-code diagnostics. Their repeat spread is empirical; the
    sparkle threshold defines the scratch statistic and is never a pass gate.
    """
    out = {}
    for region in DIALOGUE_ROIS:
        values = {label: row['dialogue_roi'][domain][region] for label, row in rows.items()}
        baseline = [v for label, v in values.items() if label.startswith('base-')]
        out[region] = dict(baseline_labels=[k for k in values if k.startswith('base-')],
            baseline_range={metric: [min(v[metric] for v in baseline), max(v[metric] for v in baseline)]
                            for metric in ('residual_x1000', 'sparkle_percent', 'mean_code')},
            windows=values)
    return out


def analyze(game_text, runner_text, resolve, job=None, plan=None):
    if job is None:
        job = json.loads((HERE/'jobs/ef1-dialogue3-attribution.json').read_text())
    if plan is None:
        plan = json.loads((HERE/'plans/ef1.json').read_text())
    labels = [step['label'][len('rendered-'):] for step in plan
              if step['label'].startswith('rendered-')]
    if not labels or len(set(labels)) != len(labels):
        raise ValueError('missing or duplicate rendered windows')
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
    presented_extents = []
    for label in labels:
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
            if kind == 'presented':
                extents = [tuple(map(int, m.groups())) for l in selected
                           if (m := re.search(r'edge capture format 64, extent (\d+)x(\d+)', l))]
                presented_extents.extend(extents)
            evidence[kind+'-'+label] = [l for l in selected if '[watch]' in l or
                                       'DLSS-G frame' in l or '[pace]' in l]
    all_paths = [p for row in paths.values() for p in row.values()]
    if len(set(all_paths)) != 2*len(labels):
        raise ValueError('capture path reused across windows')
    if job.get('analysis'):
        from tools.live_batches.edge_flicker.drift import decision_report
        config = dict(job['analysis'])
        if config.get('full_resolution_presented'):
            if len(presented_extents) != len(labels) or len(set(presented_extents)) != 1:
                raise ValueError('missing or changing full presented extent')
            config['presented_extent'] = list(presented_extents[0])
        return decision_report(paths, config, evidence, job.get('artifact_dir'))
    bases = [burst(paths[label]['rendered']) for label in ('base-a', 'base-b')]
    depth_anchor = bases[0]['depth'][0]
    base_regs = []
    for b in bases:
        keep, reg = registered_run(depth_anchor, b['depth'])
        base_regs.append(reg)
        for field in ('codes', 'depth'):
            b[field] = aligned_frames(b[field], reg)
        # Material/albedo exist only at original endpoints; discard endpoints
        # outside the registered run rather than importing a cut's classes.
        for field in ('material', 'albedo'):
            from tools.live_batches.edge_flicker.drift import translate
            b[field] = [translate(b[field][j], tuple(-v for v in reg['frames'][frame]['shift_px']))
                        for j, frame in enumerate((0, 7)) if frame in keep]
    edge, interior = masks([np.nan_to_num(d) for d in bases[0]['depth']], [np.nan_to_num(m) for m in bases[0]['material']])
    inverse = tuple(-v for v in base_regs[0]['shift_px'])
    edge, interior = shifted_mask(edge, inverse), shifted_mask(interior, inverse)
    ref_present = [presents(paths[label]['presented']) for label in ('base-a', 'base-b')]
    present_anchor = ref_present[0][0]
    ref_present = [aligned_frames(frames, registered_run(present_anchor, frames)[1])
                   for frames in ref_present]
    refs = [b['codes'] for b in bases]
    rows = {}
    for label in labels:
        print('analyzing '+label, flush=True)
        b = burst(paths[label]['rendered'])
        keep, rendered_regs = registered_run(depth_anchor, b['depth'])
        for field in ('codes', 'depth'):
            b[field] = aligned_frames(b[field], rendered_regs)
        render_shift = scaled_shift(rendered_regs['shift_px'], b['depth'][0].shape, b['codes'][0].shape[:2])
        presented = presents(paths[label]['presented'])
        shown, present_regs = registered_run(present_anchor, presented)
        presented_aligned = aligned_frames(presented, present_regs)
        rows[label] = dict(registration=dict(rendered=rendered_regs, presented=present_regs),
                           rendered=score(b['codes'], refs, edge, interior, render_shift),
                           presented=score(presented_aligned, ref_present, edge, interior, present_regs['shift_px']),
                           dialogue_roi=dict(rendered=dialogue_metrics(b['codes'], render_shift),
                                             presented=dialogue_metrics(presented_aligned, present_regs['shift_px'])),
                           depth_edge_variance=float(np.stack(b['depth']).var(0, ddof=1)[
                               shifted_mask(edge, rendered_regs['shift_px'])].mean()))
        albedos = [b['albedo'][j] for j, frame in enumerate((0, 7)) if frame in keep]
        moving = det.animated(albedos, [], edge.shape) if len(albedos) > 1 else None
        rows[label]['animated_edge_fraction'] = float(moving[shifted_mask(edge, rendered_regs['shift_px'])].mean()) if moving is not None else None
        depth_var = np.stack(b['depth']).var(0, ddof=1)
        colour_var = np.stack([c @ Y for c in b['codes']]).var(0, ddof=1)
        # Spatial association, not a proof of DoF: depth and colour are captured
        # in different passes of each record frame, without motion compensation.
        h, w = colour_var.shape
        fitted = depth_var[(np.arange(h)*depth_var.shape[0]//h)[:, None],
                           np.arange(w)*depth_var.shape[1]//w]
        support = shifted_mask(resize_mask(edge, (h, w)), render_shift)
        dv, cv = fitted[support], colour_var[support]
        rows[label]['depth_colour_variance_correlation'] = (
            float(np.corrcoef(dv, cv)[0, 1]) if dv.std() > 0 and cv.std() > 0 else None)
    return dict(status='ATTRIBUTION_ONLY', quality_pass=False, windows=rows,
                rendered=rank(rows, 'rendered'), presented=rank(rows, 'presented'),
                dialogue_roi=dict(rendered=roi_comparison(rows, 'rendered'),
                                  presented=roi_comparison(rows, 'presented')),
                rate_evidence=evidence, capture_paths={k: {d: str(p) for d, p in v.items()} for k,v in paths.items()},
                limits=['PQ code variance, not float pre-post energy; no quality/brightness claim.',
                        'Presented PNG variance includes UI, frame pacing and base-FPS changes.',
                        'Parity is unlabeled; generated-frame identity is not inferred.',
                        'Half-size nearest samples can miss a full-resolution strand.',
                        'Per-window registration excludes subtitle rows and cuts; rotation/zoom or animation may remain.',
                        'Dialogue3 fixed hair/armour ROIs are normalized from 1920x800; PQ mean is not float energy.',
                        'Class 3/5 support covers skin/hair, not unclassified armour silhouette.',
                        'RR-off changes RT denoising too; post-late does not disable DoF.',
                        'Largest reduction is a candidate, never proof of one root cause.'])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--game-log', type=Path, required=True)
    p.add_argument('--runner-log', type=Path, required=True)
    p.add_argument('--capture-root', type=Path)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--job', type=Path, help='Queue job JSON (default: ef1)')
    p.add_argument('--plan', type=Path, help='Matching plan JSON (default: ef1)')
    a = p.parse_args()
    def resolve(path):
        return a.capture_root / path.replace('\\', '/').split('/')[-1] if a.capture_root else Path(path)
    try:
        report = analyze(a.game_log.read_text(errors='replace'),
                         a.runner_log.read_text(errors='replace'), resolve,
                         dict(json.loads(a.job.read_text()), artifact_dir=str(a.out.with_suffix(''))) if a.job else None,
                         json.loads(a.plan.read_text()) if a.plan else None)
    except (ValueError, OSError, KeyError, StopIteration) as error:
        report = dict(status='INVALID', quality_pass=False, error=str(error))
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps(report, indent=2, allow_nan=False)+'\n', encoding='utf-8')
    print(report['status'])
    return 1 if report['status'] == 'INVALID' else 0


if __name__ == '__main__':
    raise SystemExit(main())
