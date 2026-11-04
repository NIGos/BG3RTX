"""Two bounded release diagnostics using the shared retimer; never queues."""
import copy
from pathlib import Path
from tools.live_batches import live_flow as flow

HERE = Path(__file__).resolve().parent


def phase(label, seconds, settings, **extra):
    return dict(label=label, kind='static', speed='static', seconds=seconds,
                repeat_ms=0, scan=16, capture_at=0, serial=0,
                motion_start=seconds, motion_end=seconds, pixels_per_second=0,
                direction=0, set=settings, **extra)


def make():
    template = flow.read(HERE.parent / 'm4_generated/jobs/hgn1-generated-camp-fg2.json')
    for name, sun in [('cs1-current-sun-s8', True), ('as1-alpha-spill-s8', False)]:
        job = copy.deepcopy(template)
        job.update(name=name, save='Water Queens House', live_batch='backlog-diagnostics',
                   window_mode='visible_unfocused_fg', hold=120, timeout=900,
                   args=['-RealPresent', '-Move', '-MovePlan',
                         f'captures/live_batches/backlog_diagnostics/plans/{name}.json'])
        state = dict(item.split('=', 1) for item in job['set'].split(','))
        state.update(rt_sun_cascade_current=str(int(sun)), rt_alpha_spill='0',
                     rt_hit_material_generated='0', rt_native_hair='0',
                     rt_hair_groom_primary='0', rt_hair_groom='0', rt_xfb_mb='1024',
                     rt_stats='0', rt_primary_check='0', rt_omm='0', qa_exposure_hold='0')
        job['set'] = ','.join(f'{k}={v}' for k,v in state.items())
        job['live_flow'] = dict(expected_fps=30, settings_latency_s=1, trace_stall_s=4,
                                startup_only=['rt_xfb_mb', 'rt_sun_cascade_current'])
        job.pop('post', None)
        plan = [phase('warmup', 30, 'rt_stats=0,qa_exposure_hold=0')]
        job['expect'] = [r'\[auto\] scene ready', r'\[rrpost\] exposure held']
        job['expect_every'] = []
        if sun:
            # sun_trust.cpp accumulates 600 frames, also while rt_stats is off.
            for label in ['diag-on-a', 'diag-on-b']:
                plan.append(phase(label, 170, 'rt_stats=1,qa_exposure_hold=1'))
            select = r'sun current native-domain coverage \(\d+ frames\):'
            match = (r'certified [1-9]\d* pixels, fallback \d+, scene unready 0; '
                     r'.*actual selected/next ray samples [1-9]\d*')
            job['patterns'] += ['sun current', 'sun stats', 'sun check', 'sun current scene']
            job['expect_every'].append(dict(name='current sun ready with real rays',
                select=select, match=match, windows=['diag-on-a','diag-on-b'],
                min=1, period_frames=600))
            job['purpose'] = ('Fix-or-reject diagnostic only: conjunctive sun_current_scene_ready predicate; '
                              'positive scene-unready or zero rays rejects release inclusion. No image qualification.')
        else:
            plan += [phase('warmup-spill-on', 5, 'rt_alpha_spill=1,rt_stats=0,qa_exposure_hold=0'),
                     phase('warmup-spill-off', 5, 'rt_alpha_spill=0,rt_stats=0,qa_exposure_hold=0')]
            for value, tag in [(0,'off-a'),(1,'on'),(0,'off-b')]:
                plan.append(phase('warmup-'+tag, 5, f'rt_alpha_spill={value},rt_stats=0,qa_exposure_hold=1'))
                plan.append(phase('diag-'+tag, 36, f'rt_alpha_spill={value},rt_stats=1,qa_exposure_hold=1'))
                plan.append(phase('cost-'+tag, 26, f'rt_alpha_spill={value},rt_stats=0,qa_exposure_hold=1'))
                plan.append(phase('shot-'+tag, 16, f'rt_alpha_spill={value},rt_stats=0,qa_exposure_hold=1', trace_at=[4,9]))
            job['patterns'] += ['alpha partition spill']
            job['expect_every'] += [
                dict(name='real overflow OFF controls', select=r'\[geo\] RT capture, last \d+ frames:',
                     match=r'alpha draws made opaque \(region full\) (?!0\.00)[0-9.]+, opaque draws dropped \(buffer full\) 0\.00',
                     windows=['diag-off-a','diag-off-b'], min=1),
                dict(name='rescue without residual substitution', select=r'\[geo\] alpha partition spill:',
                     match=r'spill: (?!0\.00)[0-9.]+ draws/frame, .*residual made opaque 0\.00 draws/frame',
                     windows=['diag-on'], min=1, period_frames=120)]
            job['purpose'] = 'Fix-or-reject alpha spill at historically overflowing S8; absent OFF overflow is INVALID, never a pass.'
        costs = [p['label'] for p in plan if p['label'].startswith('cost-')]
        for select, match, title in [
            (r'\[rt\] scene wait: \d+ frames', r"got this frame's scene, 0 timeouts", 'same frame scene'),
            (r'\[geo\] RT capture, last \d+ frames:', r'opaque draws dropped \(buffer full\) 0\.00,', 'no opaque loss'),
            (r'\[cascade\] casters: cascade \d+, \d+ frames;', r', buffer full 0\.0, added ', 'cascade capacity'),
            (r'rt passes: GPU cost', r'GPU cost [0-9.]+ ms/frame', 'RT cost')]:
            if costs:
                job['expect_every'].append(dict(name=title, select=select, match=match, windows=costs,min=1, **({'when_present': True} if title == 'cascade capacity' else {})))
        plan.append(phase('restore', 2, 'rt_stats=0,rt_alpha_spill=0,qa_exposure_hold=0'))
        job['expect_runner'] = [r'\[window-mode\].*visible_unfocused', '^Run ended: done',
                               rf'\[motion-plan\] complete phases={len(plan)}\b']
        for i,p in enumerate(plan):
            if p.get('trace_at'):
                job['expect_runner'].append(rf'\[motion-trace\] phase={i} label={p["label"]} repeat=0')
        flow.write(HERE / 'jobs' / (name+'.json'),job)
        flow.write(HERE / 'plans' / (name+'.json'),plan)
    flow.tighten_files(HERE)
    # Bound cold/unknown readiness too: fail closed instead of a >15 minute launch.
    for path in flow.job_files(HERE):
        job, plan, target = flow.load_job(path)
        for p in plan:
            if p.get('wait_log'):
                p['wait_log_timeout'] = min(p['wait_log_timeout'], 60)
        job['timeout'] = int(sum(p['seconds'] + p.get('wait_log_timeout', 0) for p in plan) + 121)
        flow.write(target, plan)
        flow.write(path, job)


if __name__ == '__main__':
    make()
