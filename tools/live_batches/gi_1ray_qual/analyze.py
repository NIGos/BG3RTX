"""Thin adapters to existing energy and QA temporal gates. Never controls the game."""
import argparse
import json
from pathlib import Path
import re

import numpy as np
from scripts import queue_gates as gates
from scripts.qa import detectors as det
from tools import m4_promotion_gate as m4
from tools.live_batches.edge_flicker.analyze import burst, aligned_frames
from tools.live_batches.edge_flicker.drift import register_window, translate
from tools.live_batches.sharc_far.rays_flicker import frames, traces
from tools.live_batches.gi_1ray_qual.make_job import HERE, STATES


def temporal_rule(sets, moving):
    """Exactly the sh2 floor rule, with INCONCLUSIVE rejected on every comparison."""
    pairs = [('off-b', 'off-a'), ('off-a', 'off-b')]
    pairs += [(on, off) for on in ('on-a', 'on-b') for off in ('off-a', 'off-b')]
    rows = []
    for on, off in pairs:
        r = det.temporal(sets[on], sets[off], moving)
        r.pop('map', None)
        rows.append(dict(on=on, off=off, **r))
    if any(r['metrics']['inconclusive'] for r in rows):
        raise ValueError('QA temporal INCONCLUSIVE: insufficient static support')
    floor = max(r['metrics']['excess_area'] for r in rows[:2])
    null_pass = all(r['pass'] for r in rows[:2])
    ok = all(r['pass'] if null_pass else
             r['metrics']['excess_area'] <= floor and r['score'] <= r['threshold']
             for r in rows[2:])
    return dict(ok=ok, off_floor_per_mille=floor, null_pass=null_pass, comparisons=rows)


def analyze(log, captures_root=None):
    run = m4.Run(log, captures_root)
    job = json.loads((HERE/'jobs/gi1-ultra-1ray.json').read_text())
    plan = json.loads((HERE/'plans/gi1.json').read_text())
    runner = log.with_suffix('.runner.txt').read_text(encoding='utf-8-sig')
    game = log.read_text(encoding='utf-8-sig')
    failures = gates.check(job['expect_every'], game, runner, plan)
    for key, text in [('expect', game), ('expect_runner', runner)]:
        failures += ['missing '+p for p in job[key] if not re.search(p, text, re.M)]
    failures += ['forbidden '+p for p in job['forbid'] if re.search(p, game, re.M)]
    if failures or run.failed_drift():
        raise ValueError('; '.join(failures) or 'plan drift')
    windows = gates.windows(runner, plan, gates.stamped_lines(game))
    run.phases = [(w['label'], w['applied'], w['end']) for w in windows]

    static = {}; all_frames = []
    samples = {}
    original = run.scene_colour
    for label, _ in STATES:
        phase = 'shot-rays-'+label
        paths = traces(run, phase)
        if len(paths) != 3 or len(set(paths)) != 3:
            raise ValueError(phase+': need exactly three distinct traces')
        fs = frames(paths)
        if any(not f.t.find('d25b8086', 'RGB10A2') or not np.isfinite(f.final_code()).all() for f in fs):
            raise ValueError(phase+': missing/nonfinite native PQ final')
        static[label] = [f.final_code() for f in fs]
        all_frames += fs
        # Keep the existing float dump selection/exposure reader, for every trace.
        p = run.phase_of(phase)
        selected = [(t, txt) for t, txt in run.lines if p[1] <= t < p[2] and m4.TRACE.search(txt)]
        values = []
        saved = run.lines
        for line in selected:
            run.lines = [line]
            v = original(phase)
            if not v or isinstance(v[0], str) or v[1] or not v[2] or v[0] <= 0 or not np.isfinite([v[0], v[2]]).all():
                raise ValueError(phase+': missing/nonfinite float colour or exposure')
            values.append(v)
        run.lines = saved
        if len(values) != 3:
            raise ValueError(phase+': missing float sample')
        samples[phase] = values
    # Existing energy gate on means of ALL three normalized float observations.
    run.scene_colour = lambda label: (float(np.mean([v[0]/v[2] for v in samples[label]])), 0, 1.0, {})
    energy = m4.energy(run, pool='run')
    if len(energy) != 2 or any(r.get('band') is None for r in energy):
        raise ValueError('missing OFF-repeat energy band or ON repeat')
    moving = det.animated([f.albedo() for f in all_frames], [f.sun_mask() for f in all_frames],
                          static['off-a'][0].shape[:2], [f.normal() for f in all_frames])
    static_result = temporal_rule(static, moving)

    # K8 edge decoder + existing integer registration. No smoothing/optical flow.
    pans = {}; registrations = {}; support = None; albedos = []
    for label, _ in STATES:
        win = next(w for w in windows if w['label'] == 'pan-'+label)
        found = [m[1].strip() for t,line in gates.stamped_lines(game)
                 if win['applied'] <= t < win['end']
                 and (m := re.search(r'\[pan\] burst .*transport ok -> (.+)$', line))]
        if len(found) != 1:
            raise ValueError(label+': missing/duplicate pan burst')
        path = Path(found[0]); path = captures_root/path.name if captures_root else path
        meta = json.loads((path/'burst.json').read_text())
        if any(e['format'] != 64 for e in meta['samples'] if e['name'] == 'final'):
            raise ValueError(label+': final burst is not RGB10A2 PQ')
        data = burst(path)
        if any(not np.isfinite(f).all() for key in ('codes','depth','albedo') for f in data[key]):
            raise ValueError(label+': nonfinite pan data')
        if label == 'off-a':
            anchor = data['codes'][0]
        h,w = anchor.shape[:2]
        config = dict(roi_extent=(w,h), subtitle_top=h,
                      rois=dict(rigid_control=(0,0,w,h)))
        keep, reg = register_window(anchor, data['codes'], config)
        # Require all eight motion samples; do not silently select a quieter subset.
        if keep != list(range(8)):
            raise ValueError(label+': incomplete pan registration')
        if not any(any(v != 0 for v in r['shift_px']) for r in reg['frames'][1:]):
            raise ValueError(label+': no measured camera displacement during the pan burst')
        local = aligned_frames(data['codes'], reg)
        offset = tuple(-v for v in reg['shift_px'])
        pans[label] = [translate(c, offset) for c in local]
        registrations[label] = reg
        finite = np.stack([np.isfinite(c).all(-1) for c in pans[label]]).all(0)
        support = finite if support is None else support & finite
        for endpoint, source in zip((0,7), data['albedo']):
            shift = tuple(-v for v in reg['frames'][endpoint]['shift_px'])
            aligned = translate(translate(source, shift), offset)
            support &= np.isfinite(aligned).all(-1)
            albedos.append(np.nan_to_num(aligned))
    # Albedo endpoints exclude animation using the same QA jitter/code-step rule.
    # Registration failure (rotation/parallax) invalidates rather than changing metric.
    # Finite overlap is masked before temporal(), never scored as black borders.
    for label in pans:
        pans[label] = [np.nan_to_num(c) for c in pans[label]]
    pan_result = temporal_rule(pans, ~support | det.animated(albedos, [], support.shape))
    timed = {label: run.cost('cost-'+label).get('gi: trace pass') for label,_ in STATES}
    if any(v is None for v in timed.values()):
        raise ValueError('missing per-state GI trace GPU timing')
    cost = {label: value[0] for label,value in timed.items()}
    baseline = (cost['off-a']+cost['off-b'])/2
    saving = {label: baseline-cost[label] for label in ('on-a','on-b')}
    ok = all(r['ok'] for r in energy) and static_result['ok'] and pan_result['ok'] and min(saving.values()) > 0
    return dict(status='PASS' if ok else 'FAIL', energy=energy, float_samples=samples,
                static=static_result, pan=pan_result, registration=registrations,
                gi_trace_ms=cost, saving_ms=saving)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log', type=Path)
    ap.add_argument('--captures-root', type=Path)
    ap.add_argument('--out', type=Path)
    a = ap.parse_args()
    try:
        result = analyze(a.log, a.captures_root)
    except (OSError, ValueError, KeyError, TypeError, IndexError) as e:
        result = dict(status='INVALID', reason=str(e))
    output = a.out or a.log.with_suffix('.gi1.json')
    output.write_text(json.dumps(result, indent=2, default=lambda x: x.item())+'\n')
    output.with_suffix('.md').write_text('# One-ray Ultra GI: '+result['status']+'\n\n'+
        (result.get('reason') or 'See sibling JSON for every comparison, float energy band and per-state GPU savings.')+'\n')
    print(json.dumps(dict(status=result['status'], report=str(output))))
    return 0 if result['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
